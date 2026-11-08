#!/usr/bin/env python3
"""Audit owned SSHD stage metadata in memory; emit factual destination IDs only.

Requires local PyYAML for public randomizer definitions. No game/assets/strings
are extracted to disk. This script neither executes a game nor makes requests.
"""
import argparse
import ast
from collections import Counter, defaultdict
import hashlib
import json
import math
from pathlib import Path
import re
import struct
import sys

MAX_ARCHIVE = 128 * 1024 * 1024
MAX_ROWS = 32768
STAGE = re.compile(r"[A-Za-z][A-Za-z0-9_]{0,7}\Z")
VERIFIED, UNTESTED, STORY, ALTERNATE, DUPLICATE, ARENA, EVENT, UNNAMED, REFERENCED = [1 << n for n in range(9)]
NO_SPAWN = 1 << 9
SHARED_RESOURCES = 1 << 10
FALLBACK_FIRST = 1 << 11

# Exact prepared1.0.0 main-image facts; addresses are image relative. These
# prove the native operation, not a successful/safe arrival in any room.
FIRST_PLY_PROOF = dict(
    native_build_id="d824d4b1bbd45d03668380d6d041ba4ca00a64ff",
    dispatch_tag="PLY ", dispatch_relocation="0x1678828", handler="0xe0c070",
    requested_entrance_bits=8, native_record_id_bits=16, record_stride=24,
    trigger="0xdf4fdc", trigger_entrance_byte_store="0xdf5128",
    trigger_sha256="3e1c9324e02f92d6bcdb4fcdf1500d7a072d01b9bb494c54f79fd600a1c46c18",
    lookup="0xe0c4d0", unmatched_first_record="0xe0c5c0",
    lookup_sha256="5fa855d2917058b67d1a44c2befffb87fbdce6fea547ab8583d85b771014d8a5",
    semantics="byte255 cannot equal native u16FFFF; unmatched lookup uses first PLY record")


def sha(data):
    return hashlib.sha256(data).hexdigest()


class Rom:
    def __init__(self, path):
        self.stream = Path(path).open("rb")
        self.header = struct.unpack("<10Q", self.stream.read(80))
        self.files = {}
        self._directory(0, "", set())

    def _directory(self, offset, prefix, visited):
        assert offset not in visited and len(visited) < 100000
        visited.add(offset)
        self.stream.seek(self.header[3] + offset)
        _, _, child, file, _, length = struct.unpack("<6I", self.stream.read(24))
        assert length < 256
        name = self.stream.read(length).decode()
        prefix += name + "/" if name else ""
        file_seen = set()
        while file != 0xffffffff:
            assert file not in file_seen
            file_seen.add(file)
            self.stream.seek(self.header[7] + file)
            _, following, data, size, _, count = struct.unpack("<IIQQII", self.stream.read(32))
            assert count < 256
            name = self.stream.read(count).decode()
            self.files[prefix + name] = (self.header[9] + data, size)
            file = following
        while child != 0xffffffff:
            self.stream.seek(self.header[3] + child + 4)
            following = struct.unpack("<I", self.stream.read(4))[0]
            self._directory(child, prefix, visited)
            child = following

    def read(self, path):
        offset, size = self.files[path]
        assert size <= MAX_ARCHIVE
        self.stream.seek(offset)
        data = self.stream.read(size)
        assert len(data) == size
        return data


def unlz(data):
    assert data[0] == 0x11
    size = int.from_bytes(data[1:4], "little")
    at = 4
    if not size:
        size = int.from_bytes(data[4:8], "little")
        at = 8
    assert 0 < size <= MAX_ARCHIVE
    out = bytearray()
    while len(out) < size:
        flags = data[at]
        at += 1
        for bit in range(7, -1, -1):
            if len(out) >= size:
                break
            if not flags & (1 << bit):
                out.append(data[at])
                at += 1
                continue
            a = data[at]
            at += 1
            high = a >> 4
            if high == 0:
                x, y = data[at:at + 2]
                at += 2
                length = ((a & 15) << 4 | x >> 4) + 17
                distance = ((x & 15) << 8 | y) + 1
            elif high == 1:
                x, y, z = data[at:at + 3]
                at += 3
                length = ((a & 15) << 12 | x << 4 | y >> 4) + 273
                distance = ((y & 15) << 8 | z) + 1
            else:
                x = data[at]
                at += 1
                length = high + 1
                distance = ((a & 15) << 8 | x) + 1
            assert distance <= len(out) and len(out) + length <= size
            block = out[-distance:]
            out.extend((block * ((length + distance - 1) // distance))[:length])
    return out


def u8_index(data):
    assert data[:4] == b"U\xaa8-"
    root = struct.unpack_from(">I", data, 4)[0]
    count = struct.unpack_from(">I", data, root + 8)[0]
    assert 0 < count < 65536 and root + count * 12 <= len(data)
    strings = root + count * 12
    stack, result = [], {}
    for i in range(1, count):
        while stack and i >= stack[-1][1]:
            stack.pop()
        kind, offset, size = struct.unpack_from(">III", data, root + i * 12)
        start = strings + (kind & 0xffffff)
        end = data.find(b"\0", start, start + 256)
        assert end >= start
        name = data[start:end].decode()
        path = "/".join([x[0] for x in stack] + [name])
        if kind >> 24:
            assert i < size <= count
            stack.append((name, size))
        else:
            assert offset + size <= len(data)
            result[path] = (offset, size)
    return result


def bzs_metadata(data):
    name, count, ff, start = struct.unpack_from(">4sHhI", data)
    assert name == b"V001" and ff == -1
    plies, scenes, selections, layer_sections = [], [], [], []

    def walk(base, quantity, layer, depth):
        assert quantity < 1024 and depth <= 1 and base + quantity * 12 <= len(data)
        for k in range(quantity):
            at = base + k * 12
            kind, total, sentinel, relative = struct.unpack_from(">4sHhI", data, at)
            assert sentinel == -1
            pos = at + relative
            if kind == b"LAY ":
                assert total == 29 and pos + 29 * 8 <= len(data)
                for n in range(29):
                    address = pos + n * 8
                    number, _, rel = struct.unpack_from(">HhI", data, address)
                    if number:
                        layer_sections.append(n)
                        walk(address + rel, number, n, depth + 1)
            elif kind == b"PLY ":
                assert total <= 4096 and pos + total * 24 <= len(data)
                for n in range(total):
                    address = pos + n * 24
                    policy, cutscene, other = struct.unpack_from(">hbb", data, address)
                    values = struct.unpack_from(">3f", data, address + 4)
                    assert all(math.isfinite(x) for x in values)
                    entrance = struct.unpack_from(">h", data, address + 22)[0]
                    plies.append(dict(layer=layer, entrance=entrance, record=n,
                                      policy=policy, cutscene=cutscene, other=other,
                                      record_sha256=sha(data[address:address + 24])))
            elif kind == b"SCEN":
                assert total <= 4096 and pos + total * 40 <= len(data)
                for n in range(total):
                    values = struct.unpack_from(">32sbbbbbbbb", data, pos + n * 40)
                    target = values[0].split(b"\0", 1)[0].decode("ascii")
                    scenes.append(dict(stage=target, room=values[1], layer=values[2],
                                       entrance=values[3], source_layer=layer, record=n))
            elif kind == b"LYSE":
                assert pos + total * 4 <= len(data)
                for n in range(total):
                    flag, night, selected = struct.unpack_from(">hbb", data, pos + n * 4)
                    selections.append(dict(flag=flag, night=night, layer=selected))
    walk(start, count, -1, 0)
    return dict(plies=plies, scenes=scenes, layer_selections=selections,
                layer_sections=sorted(set(layer_sections)))


def literal_assignment(path, name):
    for node in ast.parse(path.read_text()).body:
        if isinstance(node, ast.Assign) and any(isinstance(t, ast.Name) and t.id == name for t in node.targets):
            return ast.literal_eval(node.value)
    raise ValueError(name)


def public_names(reference):
    import yaml
    entrance_path = reference / "data/entrance_shuffle_data.yaml"
    patches = reference / "data/patches/stagepatches.yaml"
    labels = {}
    for line in patches.read_text().splitlines():
        m = re.fullmatch(r"(?:# )?([A-Za-z][A-Za-z0-9_]{0,7}):\s*#\s*(.+)", line)
        if m:
            labels[m[1]] = m[2].strip()
    labels.update(S000="Goddess's Silent Realm", B400="Demise Boss Room")
    room_names, exact_names, area_regions = defaultdict(set), defaultdict(set), {}
    sources = [entrance_path, patches]
    for path in sorted((reference / "data/world").glob("*.yaml")):
        sources.append(path)
        for area in yaml.safe_load(path.read_text()):
            area_regions[area["name"]] = path.stem
    for item in yaml.safe_load(entrance_path.read_text()):
        for direction in ("forward", "return"):
            entry = item.get(direction)
            if not entry:
                continue
            destination = entry["connection"].split(" -> ")[-1]
            for key in ("spawn_info", "secondary_spawn_info"):
                entries = entry.get(key, [])
                if entries == 0: # public region-drop directives are not spawn tuples
                    continue
                assert isinstance(entries, list)
                for spawn in entries:
                    identity = (spawn["stage"], spawn["room"], spawn["layer"], spawn["entrance"])
                    exact_names[identity].add(destination)
                    room_names[(spawn["stage"], spawn["room"])].add(destination)
    return labels, room_names, exact_names, area_regions, sources


def default_region(stage):
    if stage == "D000" or stage.startswith("F00") or stage.startswith("F01") and stage not in ("F010r", "F011r"):
        return "Skyloft"
    if stage.startswith("D003") or stage in ("F010r", "F011r", "F020", "F021", "F023", "S000"):
        return "Sky"
    if stage.startswith(("F1", "F4", "D1", "B1", "B4", "S1")):
        return "Faron"
    if stage.startswith(("F2", "D2", "B2", "S2")):
        return "Eldin"
    if stage.startswith(("F3", "D3", "B3", "S3")):
        return "Lanayru"
    return "Special / Unclassified"


def scan(args):
    reference = args.reference_root.resolve()
    labels, room_names, exact_names, area_regions, sources = public_names(reference)
    expected = literal_assignment(reference / "constants/verificationconstants.py", "BZS_FILE_HASHES")
    sources += [reference / "constants/verificationconstants.py", reference / "sslib/bzs.py"]
    rom = Rom(args.romfs)
    archives = sorted(p for p in rom.files if re.fullmatch(r"Stage/[^/]+/NX/[^/]+_stg_l\d+\.arc\.LZ", p))
    physical = defaultdict(set)
    for path in archives:
        stage = path.split("/")[1]
        assert STAGE.fullmatch(stage)
        physical[stage].add(int(re.search(r"_l(\d+)\.arc", path)[1]))
    raw_rooms, stage_data, proofs, scenes, mismatches = {}, {}, [], [], []
    for number, stage in enumerate(sorted(physical), 1):
        assert 0 in physical[stage] and all(0 <= n < 29 for n in physical[stage])
        path = f"Stage/{stage}/NX/{stage}_stg_l0.arc.LZ"
        compressed = rom.read(path)
        decoded = unlz(compressed)
        index = u8_index(decoded)
        extracted = []
        for name, (offset, size) in index.items():
            if name == "dat/stage.bzs":
                extracted.append((None, bytes(decoded[offset:offset + size]), "stage.bzs"))
            else:
                m = re.fullmatch(r"rarc/" + re.escape(stage) + r"_r(\d+)\.arc", name)
                if not m:
                    continue
                room = int(m[1]);assert room <= 255
                inner = decoded[offset:offset + size]
                room_index = u8_index(inner)
                assert "dat/room.bzs" in room_index
                at, length = room_index["dat/room.bzs"]
                extracted.append((room, bytes(inner[at:at + length]), f"room_{room}.bzs"))
        for room, data, name in extracted:
            digest = sha(data)
            match = expected.get(stage, {}).get(name) == digest
            if not match:
                mismatches.append(dict(stage=stage, file=name, observed=digest, expected=expected.get(stage, {}).get(name)))
            metadata = bzs_metadata(data)
            metadata.update(bzs_sha256=digest, bzs_file=name)
            if room is None:
                stage_data[stage] = metadata
            else:
                raw_rooms[(stage, room)] = metadata
            for scene in metadata["scenes"]:
                scene.update(source_stage=stage, source_room=room)
                scenes.append(scene)
        proofs.append(dict(stage=stage, archive=path, compressed_sha256=sha(compressed),
                           native_layers=sorted(physical[stage]), bzs_files=len(extracted)))
        print(f"audited {number}/{len(physical)}: {stage} ({len(extracted)} BZS)", flush=True)
        del decoded, compressed
    # Game logical layers may share layer0 resources: e.g. native F009r LYSE
    # selects1/2 although no separate l1/l2 archive exists. Preserve provenance.
    logical = {s:set(v) for s,v in physical.items()}
    layer_provenance = defaultdict(lambda: defaultdict(set))
    for stage, layers in physical.items():
        for layer in layers:
            layer_provenance[stage][layer].add("resource_archive")
    for (stage, _), data in list(raw_rooms.items()) + [((s, None), d) for s,d in stage_data.items()]:
        for layer in data["layer_sections"]:
            logical[stage].add(layer)
            layer_provenance[stage][layer].add("native_LAY_section")
        for selection in data["layer_selections"]:
            layer = selection["layer"]
            if 0 <= layer < 29:
                logical[stage].add(layer)
                layer_provenance[stage][layer].add("native_LYSE")
    for scene in scenes:
        if scene["stage"] in logical and 0 <= scene["layer"] < 29:
            logical[scene["stage"]].add(scene["layer"])
            layer_provenance[scene["stage"]][scene["layer"]].add("native_SCEN_reference")
    for stage, _, layer, _ in exact_names:
        if stage in logical and 0 <= layer < 29:
            logical[stage].add(layer)
            layer_provenance[stage][layer].add("public_named_reference")
    for proof in proofs:
        stage = proof["stage"]
        proof["resource_layers"] = proof["native_layers"]
        proof["native_layers"] = sorted(logical[stage])
        proof["layer_provenance"] = {str(n):sorted(v) for n,v in sorted(layer_provenance[stage].items())}
        proof["layer_selections"] = stage_data.get(stage, {}).get("layer_selections", [])
    destinations, seen, skipped, global_unbound = [], set(), [], []
    explicit_refs = {(s["stage"], s["room"], s["layer"], s["entrance"]) for s in scenes
                     if all(0 <= s[k] <= 255 for k in ("room", "layer", "entrance"))}
    for (stage, room), metadata in sorted(raw_rooms.items()):
        zone = labels.get(stage)
        if not zone:
            zone = sorted(room_names.get((stage, room), []))[0] if room_names.get((stage, room)) else "Native " + stage
        named_room = " / ".join(sorted(room_names.get((stage, room), [])))
        # A base-section first PLY with unspecified u16FFFF is a real native
        # first-record arrival, reached by an unmatched byte request255. Do not
        # add this alternative to rooms that already have numbered arrivals or
        # layer-local PLY definitions. F301_2/r9 has two unspecified records:
        # ONLY record0 is selected by the proven native fallback.
        fallback_room = (metadata["plies"] and
                         all(p["layer"] == -1 and p["entrance"] == -1 for p in metadata["plies"]) and
                         sum(p["record"] == 0 for p in metadata["plies"]) == 1)
        for spawn in metadata["plies"]:
            fallback = fallback_room and spawn["record"] == 0
            entrance = 255 if fallback else spawn["entrance"]
            layers = logical[stage] if spawn["layer"] == -1 else [spawn["layer"]]
            for layer in sorted(layers):
                key = stage, room, layer, entrance
                if not 0 <= entrance <= 255 or layer not in logical[stage]:
                    skipped.append(dict(stage=stage, room=room, spawn=spawn, reason="nonrepresentable entrance or absent declared logical layer"))
                    continue
                if key in seen:
                    skipped.append(dict(stage=stage, room=room, layer=layer, entrance=spawn["entrance"], reason="duplicate tuple declaration"))
                    continue
                seen.add(key)
                names = sorted(exact_names.get(key, []))
                label = ((named_room or zone) + f" / Room {room} Native first-record fallback (request 255)"
                         if fallback else " / ".join(names) if names else (named_room or zone) + f" / Room {room} Entry {entrance}")
                region = default_region(stage)
                flags = VERIFIED | UNTESTED | STORY
                if fallback:
                    flags |= FALLBACK_FIRST
                if layer:
                    flags |= ALTERNATE
                if layer not in physical[stage]:
                    flags |= SHARED_RESOURCES
                if "Boss Room" in zone:
                    flags |= ARENA
                if stage == "Demo" or re.search(r"cutscene|\bCS\b|movie|title", zone, re.I):
                    flags |= EVENT
                if stage == "F000" and room == 0 and layer in (26, 28):
                    flags |= EVENT
                if zone.startswith("Native "):
                    flags |= UNNAMED
                if key in explicit_refs or names:
                    flags |= REFERENCED
                destinations.append(dict(stage=stage, room=room, layer=layer, entrance=entrance,
                                         region=region, zone=zone, label=label[:191], flags=flags,
                                         proof=dict(bzs_file=metadata["bzs_file"], bzs_sha256=metadata["bzs_sha256"],
                                                    section_layer=spawn["layer"], record=spawn["record"], record_sha256=spawn["record_sha256"],
                                                    **(dict(native_record_id=65535, requested_entrance=255,
                                                            arrival_kind="native_first_record_fallback") if fallback else {}))))
    for stage, metadata in stage_data.items():
        for spawn in metadata["plies"]:
            global_unbound.append(dict(stage=stage, entrance=spawn["entrance"], section_layer=spawn["layer"], reason="stage-global PLY requires explicit room provenance; not fabricated"))
    assert len(destinations) <= MAX_ROWS
    count = Counter((d["stage"], d["room"], d["entrance"], d["proof"]["record_sha256"]) for d in destinations)
    for d in destinations:
        if count[d["stage"], d["room"], d["entrance"], d["proof"]["record_sha256"]] > 1:
            d["flags"] |= DUPLICATE
    destinations.sort(key=lambda d: (d["stage"], d["room"], d["layer"], d["entrance"]))
    zones = []
    cursor = 0
    for (stage, room), metadata in sorted(raw_rooms.items()):
        first = cursor
        while cursor < len(destinations) and (destinations[cursor]["stage"], destinations[cursor]["room"]) == (stage, room):
            cursor += 1
        zone = labels.get(stage) or (sorted(room_names[stage, room])[0] if room_names.get((stage, room)) else "Native " + stage)
        zones.append(dict(stage=stage, room=room, region=default_region(stage), zone=zone,
                          first=first, count=cursor-first, flags=UNTESTED | STORY | (UNNAMED if zone.startswith("Native ") else 0)))
    assert cursor == len(destinations)
    missing_refs = sorted(key for key in explicit_refs if key not in seen)
    return dict(kind="SSHD1.0.0 native PLY/logical-layer catalogue, not runtime safety proof",
                stages=len(physical), layer_archives=len(archives), rooms=len(raw_rooms), destinations=destinations,
                zones=zones, stage_proofs=proofs, reference_hashes={str(p.relative_to(reference)):sha(p.read_bytes()) for p in sources},
                bzs_reference_mismatches=mismatches, skipped_native_declarations=skipped,
                unbound_stage_global_spawns=global_unbound,
                native_first_record_proof=FIRST_PLY_PROOF,
                missing_explicit_scene_destinations=[dict(zip(("stage", "room", "layer", "entrance"), k)) for k in missing_refs],
                counts=dict(destinations=len(destinations), zones=len(zones), rooms_without_spawn=sum(not z["count"] for z in zones),
                            referenced=sum(bool(d["flags"] & REFERENCED) for d in destinations),
                            boss_arena=sum(bool(d["flags"] & ARENA) for d in destinations), event_title=sum(bool(d["flags"] & EVENT) for d in destinations),
                            alternate=sum(bool(d["flags"] & ALTERNATE) for d in destinations), duplicate=sum(bool(d["flags"] & DUPLICATE) for d in destinations),
                            unnamed=sum(bool(d["flags"] & UNNAMED) for d in destinations)))


def refine(metadata, args):
    # F001r is the building used by the public Link's Spawn -> Knight Academy
    # native tuple. Cave names come from location Paths, not guessed stage IDs.
    labels = {"F001r": "Knight Academy", "Demo": "Demo / Cinematic Stage"}
    if args.reference_root:
        import yaml
        path = args.reference_root / "data/locations.yaml"
        for item in yaml.safe_load(path.read_text()):
            for source in item.get("Paths", []):
                match = re.match(r"stage/(F202_[1234])/", source)
                if match:
                    labels[match[1]] = item["name"].split(" - ")[0].strip()
        metadata["reference_hashes"]["data/locations.yaml"] = sha(path.read_bytes())
    else:
        labels.update(metadata.get("additional_labels", {}))
    # A public SS map definition explicitly names F406's lowered-statue save
    # object. This is a factual name association, not an HD memory-layout import.
    labels["F406"] = "Lowered Goddess Statue / Sealed Grounds"
    metadata["additional_labels"] = labels
    if args.romfs:
        path = args.romfs.parent / "exefs/main"
        with path.open("rb") as stream:
            header = stream.read(96)
        assert header[:4] == b"NSO0"
        build_id = header[64:96].rstrip(b"\0").hex()
        assert build_id == "d824d4b1bbd45d03668380d6d041ba4ca00a64ff"
        metadata["native_build_id"] = build_id
    assert metadata.get("native_build_id") == "d824d4b1bbd45d03668380d6d041ba4ca00a64ff"
    for d in metadata["destinations"]:
        # Quarantine the whole two known title-associated layer variants, not
        # just their ordinary native entrypoints48/55. Other arrivals are untested.
        if d["stage"] == "F000" and d["room"] == 0 and d["layer"] in (26, 28):
            d["flags"] |= EVENT
        if d["stage"] in labels:
            old = d["zone"]
            d["zone"] = labels[d["stage"]]
            if d["label"].startswith(old + " / Room"):
                d["label"] = d["zone"] + d["label"][len(old):]
            d["flags"] &= ~UNNAMED
    for z in metadata["zones"]:
        if z["stage"] in labels:
            z["zone"] = labels[z["stage"]]
            z["flags"] &= ~UNNAMED
        if not z["count"]:
            z["flags"] |= NO_SPAWN
        else:
            z["flags"] &= ~NO_SPAWN
        if any(d["flags"] & FALLBACK_FIRST for d in metadata["destinations"][z["first"]:z["first"]+z["count"]]):
            z["flags"] |= FALLBACK_FIRST
        if z["stage"] == "Demo" or re.search(r"cutscene|\bCS\b|movie|title", z["zone"], re.I):
            z["flags"] |= EVENT
        if "Boss Room" in z["zone"]:
            z["flags"] |= ARENA
    metadata["counts"]["unnamed"] = sum(bool(d["flags"] & UNNAMED) for d in metadata["destinations"])
    metadata["counts"]["event_title"] = sum(bool(d["flags"] & EVENT) for d in metadata["destinations"])
    metadata["counts"]["shared_resources"] = sum(bool(d["flags"] & SHARED_RESOURCES) for d in metadata["destinations"])
    metadata["counts"]["fallback_first"] = sum(bool(d["flags"] & FALLBACK_FIRST) for d in metadata["destinations"])
    metadata["counts"]["fallback_first_rooms"] = sum(bool(z["flags"] & FALLBACK_FIRST) for z in metadata["zones"])
    metadata["counts"]["distinct_destination_stages"] = len({d["stage"] for d in metadata["destinations"]})
    return metadata


C_API = r'''
#define SST_DEST_COUNT (sizeof destinations / sizeof destinations[0])
#define SST_ZONE_COUNT (sizeof destination_zones / sizeof destination_zones[0])
_Static_assert(SST_DEST_COUNT <= SST_DESTINATION_LIMIT, "destination catalogue bound");
_Static_assert(SST_ZONE_COUNT <= SST_DESTINATION_LIMIT, "room-zone catalogue bound");

size_t sst_destination_count(void) { return SST_DEST_COUNT; }
const SstDestination *sst_destination_at(size_t index) {
    return index < SST_DEST_COUNT ? &destinations[index] : NULL;
}
size_t sst_destination_room_zone_count(void) { return SST_ZONE_COUNT; }
const SstDestinationZone *sst_destination_room_zone_at(size_t index) {
    return index < SST_ZONE_COUNT ? &destination_zones[index] : NULL;
}
static bool stage_valid(const char *stage) {
    if(!stage || !((*stage>='A' && *stage<='Z') || (*stage>='a' && *stage<='z')))return false;
    for(unsigned i=0;i<9;i++) {
        unsigned char c=(unsigned char)stage[i];
        if(!c)return i>0 && i<=8;
        if(!((c>='A' && c<='Z') || (c>='a' && c<='z') || (c>='0' && c<='9') || c=='_'))return false;
    }
    return false;
}
static int compare(const char *stage,unsigned room,unsigned layer,unsigned entrance,const SstDestination *d) {
    int c=strcmp(stage,d->stage);if(c)return c;
    if(room!=d->room)return room<d->room?-1:1;
    if(layer!=d->layer)return layer<d->layer?-1:1;
    return entrance==d->entrance?0:(entrance<d->entrance?-1:1);
}
const SstDestination *sst_destination_find(const char *stage,unsigned room,unsigned layer,unsigned entrance) {
    if(!stage_valid(stage) || room>255 || layer>255 || entrance>255)return NULL;
    size_t lo=0,hi=SST_DEST_COUNT;
    while(lo<hi) {
        size_t mid=lo+(hi-lo)/2;const SstDestination *d=&destinations[mid];
        int c=compare(stage,room,layer,entrance,d);
        if(!c)return d;
        if(c<0)hi=mid;else lo=mid+1;
    }
    return NULL;
}
bool sst_destination_valid(const char *stage,unsigned room,unsigned layer,unsigned entrance) {
    return sst_destination_find(stage,room,layer,entrance)!=NULL;
}
static unsigned char lower(unsigned char c) { return c>='A' && c<='Z'?(unsigned char)(c+'a'-'A'):c; }
static bool space(unsigned char c) { return c==' ' || c=='\t' || c=='\n' || c=='\r' || c=='\v' || c=='\f'; }
static bool contains(const char *value,const char *term,size_t length) {
    for(size_t at=0;value[at];at++) {
        size_t i=0;while(i<length && value[at+i] && lower((unsigned char)value[at+i])==lower((unsigned char)term[i]))i++;
        if(i==length)return true;
    }
    return false;
}
static bool matches(const SstDestination *d,const char *query) {
    char tuple[80];
    snprintf(tuple,sizeof tuple,"room%u layer%u entrance%u r%u l%u e%u",d->room,d->layer,d->entrance,d->room,d->layer,d->entrance);
    size_t at=0;
    while(query[at]) {
        while(space((unsigned char)query[at]))at++;
        size_t first=at;while(query[at] && !space((unsigned char)query[at]))at++;
        size_t length=at-first;if(!length)break;
        if(!contains(d->region,query+first,length) && !contains(d->zone,query+first,length) &&
           !contains(d->label,query+first,length) && !contains(d->stage,query+first,length) &&
           !contains(tuple,query+first,length))return false;
    }
    return true;
}
size_t sst_destination_search(const char *query,uint16_t required,uint16_t excluded,size_t *indices,size_t capacity) {
    if(capacity && !indices)return 0;
    if(!query)query="";
    size_t length=0;while(length<=SST_DESTINATION_QUERY_MAX && query[length])length++;
    if(length>SST_DESTINATION_QUERY_MAX)return 0;
    size_t total=0;
    for(size_t i=0;i<SST_DEST_COUNT;i++) {
        const SstDestination *d=&destinations[i];
        if((d->flags&required)!=required || (d->flags&excluded) || !matches(d,query))continue;
        if(total<capacity)indices[total]=i;
        total++;
    }
    return total;
}
bool sst_destination_catalog_valid(void) {
    if(!SST_DEST_COUNT || SST_DEST_COUNT>SST_DESTINATION_LIMIT || SST_ZONE_COUNT>SST_DESTINATION_LIMIT)return false;
    for(size_t i=0;i<SST_DEST_COUNT;i++) {
        const SstDestination *d=&destinations[i];
        if(!stage_valid(d->stage) || !(d->flags&SST_DEST_VERIFIED) || (d->flags&SST_DEST_NO_SPAWN) ||
           ((d->flags&SST_DEST_FALLBACK_FIRST) && d->entrance!=255) ||
           !d->region || !*d->region || !d->zone || !*d->zone || !d->label || !*d->label)return false;
        if(i && compare(d->stage,d->room,d->layer,d->entrance,&destinations[i-1])<=0)return false;
    }
    size_t cursor=0;
    for(size_t i=0;i<SST_ZONE_COUNT;i++) {
        const SstDestinationZone *z=&destination_zones[i];
        if(!stage_valid(z->stage) || !z->region || !*z->region || !z->zone || !*z->zone ||
           z->first!=cursor || z->first>SST_DEST_COUNT || z->count>SST_DEST_COUNT-z->first ||
           (!z->count)!=((z->flags&SST_DEST_NO_SPAWN)!=0))return false;
        for(size_t j=0;j<z->count;j++) {
            const SstDestination *d=&destinations[cursor+j];
            if(strcmp(z->stage,d->stage) || z->room!=d->room)return false;
        }
        cursor+=z->count;
    }
    return cursor==SST_DEST_COUNT;
}
'''


def emit(metadata, root, check):
    rows, zones = metadata["destinations"], metadata["zones"]
    assert len(rows) <= MAX_ROWS and not metadata["bzs_reference_mismatches"]
    quote = lambda value: json.dumps(value, ensure_ascii=False)
    digest = sha(json.dumps(metadata, sort_keys=True, separators=(",", ":")).encode())
    output = ["// Generated factual SSHD native spawn metadata; no game assets.\n",
              "// Metadata SHA256: " + digest + "\n",
              "// All entries are runtime-untested and may depend on current progress.\n",
              '#include "sst_destinations.h"\n#include <stdio.h>\n#include <string.h>\n\n',
              "static const SstDestination destinations[] = {\n"]
    for d in rows:
        output.append("    {%s,%u,%u,%u,%u,%s,%s,%s},\n" %
                      (quote(d["stage"]), d["room"], d["layer"], d["entrance"], d["flags"],
                       quote(d["region"]), quote(d["zone"]), quote(d["label"])))
    output.append("};\n\nstatic const SstDestinationZone destination_zones[] = {\n")
    for z in zones:
        output.append("    {%s,%u,%u,%s,%s,%u,%u},\n" %
                      (quote(z["stage"]), z["room"], z["flags"], quote(z["region"]), quote(z["zone"]), z["first"], z["count"]))
    output.append("};\n" + C_API)
    path = root / "client/src/sst_destinations.c"
    encoded = "".join(output)
    if check:
        assert path.read_text() == encoded, "Generated C differs from audited metadata"
    else:
        path.write_text(encoded)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--romfs", type=Path)
    parser.add_argument("--reference-root", type=Path)
    parser.add_argument("--audit", type=Path, required=True)
    parser.add_argument("--from-audit", action="store_true")
    parser.add_argument("--scan-only", action="store_true")
    parser.add_argument("--output-root", type=Path, default=Path(__file__).resolve().parents[1])
    parser.add_argument("--check", action="store_true")
    parser.add_argument("--emit-audit", type=Path, help="write refined scalar audit to a separate path")
    args = parser.parse_args()
    if args.from_audit:
        metadata = json.loads(args.audit.read_text())
    else:
        assert args.romfs and args.reference_root
        metadata = scan(args)
        metadata = refine(metadata, args)
        encoded = json.dumps(metadata, indent=2, sort_keys=True) + "\n"
        if args.check:
            assert args.audit.read_text() == encoded
        else:
            args.audit.parent.mkdir(parents=True, exist_ok=True)
            args.audit.write_text(encoded)
    if args.from_audit:
        metadata = refine(metadata, args)
    if args.emit_audit:
        assert args.emit_audit != args.audit
        args.emit_audit.write_text(json.dumps(metadata, indent=2, sort_keys=True) + "\n")
    print(json.dumps({"stages": metadata["stages"], "layer_archives": metadata["layer_archives"],
                      "rooms": metadata["rooms"], **metadata["counts"],
                      "bzs_reference_mismatches": len(metadata["bzs_reference_mismatches"]),
                      "unbound_stage_global_spawns": len(metadata["unbound_stage_global_spawns"]),
                      "missing_explicit_scene_destinations": len(metadata["missing_explicit_scene_destinations"])}, sort_keys=True), flush=True)
    if not args.scan_only:
        emit(metadata, args.output_root, args.check)


if __name__ == "__main__":
    main()
