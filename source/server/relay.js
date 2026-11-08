// Skyward Sword Together relay. Protocol: docs/protocol.md. No dependencies (Node >= 18).
'use strict';
const net = require('node:net');

const PROTO = 45;
const trialRewards = new Map([[206,68],[207,138],[208,20],[209,180]]);
const walletGifts = new Map([[360,111]]);
const batreauxRewards = new Map([[902,108],[903,94],[904,109],[905,33],[906,110],[907,33]]);
const pouchItems = new Set([70,100,101,102,103,104,114,116,117,118,119,120,121,122,123,124,125,126,128,129,130,131,132,133,134,135,136,153]);
const shieldRewards = new Map(Array.from({length:10},(_,i)=>[1209+i,116+i]));
const ammoRewards = new Map(Array.from({length:9},(_,i)=>[1219+i,128+i]));
const pouchGifts = new Map([[35,153],[814,114]]);
const shopRewards = new Map([[813,94],[814,114],[1202,113],[1203,113],[1204,113],[1205,199],[1206,199],[1207,199],[1208,70]]);
const questFlags = new Set([326,373,383,384,387,394,398,472,477,482,553,561,575,636,639]);
const questReserved = new Set([35,326,360,361,373,382,383,384,386,387,394,395,398,472,473,477,478,481,482,547,548,549,550,551,552,553,554,555,556,557,558,559,560,561,575,635,636,639,663,664,771,707,708,813,814]);
const crystalReceipts = new Set([934,935,936,938,940,941,942,943,944,945,946,947,3620,3636,3639]);
const containerScenes = new Set(require('./reward-catalogue.json').filter(([i]) => i >= 832).map(([i]) => i));
const MAX_FRAME = 65536;
const T = {
  HELLO: 0x01, WELCOME: 0x02, PLAYER_JOIN: 0x03, PLAYER_LEAVE: 0x04, ERROR: 0x05, PING: 0x06, PONG: 0x07,
  SCENE: 0x10, POSE: 0x11, FLAG: 0x20, WORLD_REQUEST: 0x21, RESYNC: 0x22, CHEST: 0x23, WORLD_DONE: 0x24, ENEMY: 0x30, MOTION: 0x31,
};
const regularEnemies = new Set([
  0x14f, 0x156, 0x158, 0x15a, 0x15c, 0x15f, 0x162, 0x163, 0x164, 0x166,
  0x169, 0x16a, 0x16d, 0x16e, 0x170, 0x173, 0x174, 0x175, 0x176, 0x177,
  0x178, 0x179, 0x17a, 0x17b, 0x17c, 0x17e, 0x17f, 0x180, 0x181, 0x182, 0x184,
  0x185, 0x186, 0x187, 0x188, 0x18a, 0x18b, 0x18c, 0x18e, 0x190,
]);
function sameArea(a, b) { return a.stage === b.stage && a.room === b.room && a.layer === b.layer; }
function motionEligible(p) {
  return p.pose?.length === 36 && Date.now() - p.poseTime <= 1500 &&
    (p.pose[34] & 0xe0) === 0x20 && p.pose.readUInt16LE(30) > 0;
}

// ---- encoding helpers
class Reader {
  constructor(buf) { this.b = buf; this.o = 0; }
  need(n) { if (this.o + n > this.b.length) throw new Error('short frame'); }
  u8() { this.need(1); return this.b[this.o++]; }
  u16() { this.need(2); const v = this.b.readUInt16LE(this.o); this.o += 2; return v; }
  u32() { this.need(4); const v = this.b.readUInt32LE(this.o); this.o += 4; return v; }
  bytes(n) { this.need(n); const v = this.b.subarray(this.o, this.o + n); this.o += n; return v; }
  str() { const n = this.u8(); return this.bytes(n).toString('utf8'); }
}
function strBuf(s) {
  let b = Buffer.from(String(s), 'utf8');
  if (b.length > 255) b = b.subarray(0, 255);
  return Buffer.concat([Buffer.from([b.length]), b]);
}
function u16(v) { const b = Buffer.alloc(2); b.writeUInt16LE(v); return b; }
function frame(type, ...parts) {
  const body = Buffer.concat(parts);
  const head = Buffer.alloc(5);
  head.writeUInt32LE(body.length + 1, 0);
  head[4] = type;
  return Buffer.concat([head, body]);
}

function createRelay({ log = () => {} } = {}) {
  const rooms = new Map();   // room name -> { players: Map<id, Player>, teams: Map<team, Map<key, Buffer>> }
  const sessions = new Map(); // session hex -> Player
  let nextId = 1;

  function room(name) {
    let r = rooms.get(name);
    if (!r) rooms.set(name, (r = { players: new Map(), teams: new Map(), chests: new Map() }));
    return r;
  }
  function teamCache(p) {
    const r = room(p.room);
    let t = r.teams.get(p.team);
    if (!t) r.teams.set(p.team, (t = new Map()));
    return t;
  }
  function chestCache(p) {
    const r = room(p.room);
    if (!r.chests.has(p.team)) r.chests.set(p.team, new Map());
    return r.chests.get(p.team);
  }
  function send(p, buf) { if (p.sock && !p.sock.destroyed) p.sock.write(buf); }
  function others(p) { return [...room(p.room).players.values()].filter(o => o !== p && o.sock); }
  function forwarded(type, p, body) { return frame(type, u16(p.id), body); }

  function onHello(sock, r) {
    const proto = r.u16();
    const name = r.str(), roomName = r.str(), team = r.str();
    const session = r.bytes(16).toString('hex');
    if (proto !== PROTO) {
      sock.end(frame(T.ERROR, strBuf(`protocol ${proto} != relay ${PROTO}; update the mod`)));
      return null;
    }
    let p = sessions.get(session);
    if (p && p.room === roomName && p.team === team) {
      if (p.sock && p.sock !== sock) p.sock.destroy();  // reconnect replaces the old socket, keeps the id
      p.sock = sock;
      p.name = name;
      log(`reconnect #${p.id} ${name}`);
    } else {
      if (p) leave(p);
      p = { id: nextId++, name, room: roomName, team, session, sock, scene: { stage: '', room: 0, layer: 0, entrance: 0 } };
      nextId = nextId > 0xfff0 ? 1 : nextId;
      sessions.set(session, p);
      room(roomName).players.set(p.id, p);
      log(`join #${p.id} ${name} room="${roomName}" team="${team}"`);
    }
    const list = others(p);
    send(p, frame(T.WELCOME, u16(p.id), Buffer.from([list.length]),
      ...list.flatMap(o => [u16(o.id), strBuf(o.name), strBuf(o.team), strBuf(o.scene.stage), Buffer.from([o.scene.room])])));
    for (const o of list) send(p, forwarded(T.SCENE, o,
      Buffer.concat([strBuf(o.scene.stage), Buffer.from([o.scene.room, o.scene.layer, o.scene.entrance])])));
    for (const o of list) send(o, frame(T.PLAYER_JOIN, u16(p.id), strBuf(p.name), strBuf(p.team)));
    return p;
  }

  function leave(p) {
    const r = room(p.room);
    if (r.players.get(p.id) !== p) return;
    r.players.delete(p.id);
    sessions.delete(p.session);
    for (const o of r.players.values()) send(o, frame(T.PLAYER_LEAVE, u16(p.id)));
    log(`leave #${p.id} ${p.name}`);
  }

  function onFrame(state, type, body) {
    const r = new Reader(body);
    if (!state.p) {
      if (type !== T.HELLO) throw new Error('expected HELLO');
      state.p = onHello(state.sock, r);
      return;
    }
    const p = state.p;
    switch (type) {
      case T.PING: send(p, frame(T.PONG, body.subarray(0, 4))); break;
      case T.SCENE: {
        p.scene = { stage: r.str(), room: r.u8(), layer: r.u8(), entrance: r.u8() };
        p.pose = null;
        for (const o of others(p)) send(o, forwarded(T.SCENE, p, body));
        break;
      }
      case T.POSE:
        if (body.length !== 36) throw new Error('invalid pose');
        p.pose = body; p.poseTime = Date.now();
        for (const o of others(p)) if (o.scene.stage === p.scene.stage) send(o, forwarded(T.POSE, p, body));
        break;
      case T.FLAG: {
        const kind = r.u8(), index = r.u16(), value = r.u8();
        if (kind === 0 && (questReserved.has(index) || index >= 206 && index <= 210)) break;
        if (kind === 1 && (index === 199 || index === 490 || index === 508 || index === 180 || index === 113 || pouchItems.has(index) || index >= 108 && index <= 111)) break; // receipt/pouch transactions only
        if (kind === 2 && index < 3328 &&
            ([520, 521, 522, 524, 576, 161, 196, 614, 744, 1511].includes(index) ||
             crystalReceipts.has(832 + index) || containerScenes.has(832 + index) || chestCache(p).has(832 + index))) break;
        const key = (kind << 16) | index;
        const cache = teamCache(p);
        const prev = cache.get(key);
        if (prev !== undefined && (prev | value) === prev) break;  // nothing new: don't rebroadcast
        cache.set(key, (prev ?? 0) | value);
        for (const o of others(p)) if (o.team === p.team) send(o, forwarded(T.FLAG, p, body));
        break;
      }
      case T.WORLD_REQUEST: {
        if (body.length !== 0 && body.length !== 4 || body.length === 4 && !body.readUInt32LE(0))
          throw new Error('invalid world request');
        const sys = { id: 0 };
        for (const [index, item] of chestCache(p)) send(p, forwarded(T.CHEST, sys, Buffer.concat([u16(index), u16(item)])));
        for (const [key, value] of teamCache(p))
          send(p, forwarded(T.FLAG, sys, Buffer.from([key >>> 16, key & 0xff, (key >>> 8) & 0xff, value])));
        for (const o of others(p)) if (o.team === p.team && o.pose && Date.now() - o.poseTime < 1000)
          send(p, forwarded(T.POSE, o, o.pose));
        if (body.length === 4) send(p, frame(T.WORLD_DONE, body));
        break;
      }
      case T.CHEST: {
        if (body.length !== 4) throw new Error('invalid chest reward');
        const index = r.u16(), item = r.u16();
        const quest = index >= 4160 && (shopRewards.get(index - 4160) === item || shieldRewards.get(index - 4160) === item || ammoRewards.get(index - 4160) === item || (item === 35 && questFlags.has(index - 4160)) || trialRewards.get(index - 4160) === item || pouchGifts.get(index - 4160) === item || walletGifts.get(index - 4160) === item);
        const table = index < 832 ? Math.floor(index / 32) : Math.floor((index - 832) / 128);
        const smallKey = item === 1 && [7, 11, 12, 15, 17, 18, 20].includes(table);
        const keyPiece = item === 28 && [1352, 1353, 1354, 1356, 1408].includes(index);
        const crystal = item === 48 && crystalReceipts.has(index);
        const dungeonItem = item === 50 ? [11, 12, 14, 15, 17, 18, 20].includes(table) :
          ({25: 12, 26: 15, 27: 18, 29: 11, 30: 14, 31: 17})[item] === table;
        if (batreauxRewards.has(index) && batreauxRewards.get(index) !== item) throw new Error('unsupported NPC reward');
        if (index >= 5388 || !(quest || index < 4160 && (smallKey || keyPiece || crystal || dungeonItem || item === 113 || pouchItems.has(item) || [2, 3, 4, 9, 10, 11, 12, 13, 14, 19, 20, 32, 33, 49, 52, 53, 56, 68, 71, 75, 76, 77, 90, 91, 92, 94, 99, 105, 108, 109, 110, 111, 126, 137, 138, 140, 153].includes(item) || item >= 161 && item <= 176)))
          throw new Error('unsupported chest reward');
        const cache = chestCache(p);
        if (!cache.has(index)) {
          cache.set(index, item);
          if (quest) teamCache(p).delete(index - 4160);
          else if (index >= 832) teamCache(p).delete((2 << 16) | (index - 832));
          for (const o of others(p)) if (o.team === p.team) send(o, forwarded(T.CHEST, p, body));
        }
        break;
      }
      case T.RESYNC:
        if (body.length) throw new Error('invalid resync request');
        if (Date.now() - (p.lastRequest || 0) >= 1000) {
          p.lastRequest = Date.now();
          for (const o of others(p)) if (o.team === p.team) send(o, forwarded(type, p, body));
        }
        break;
      case T.ENEMY: {
        const stage = r.str(), areaRoom = r.u8(), layer = r.u8();
        if (body.length - r.o !== 19 || body[body.length - 1] > 1) throw new Error('invalid enemy event');
        if (stage !== p.scene.stage || areaRoom !== p.scene.room || layer !== p.scene.layer) break;
        for (const o of others(p)) if (o.team === p.team && o.scene.stage === stage &&
          o.scene.room === areaRoom && o.scene.layer === layer) send(o, forwarded(type, p, body));
        break;
      }
      case T.MOTION: {
        const area = { stage: r.str(), room: r.u8(), layer: r.u8() };
        if (body.length - r.o !== 44) throw new Error('invalid enemy movement');
        const data = r.bytes(44), profile = data.readUInt16LE(12);
        if (!regularEnemies.has(profile) || data.readUInt16LE(2) !== profile ||
          data.readUInt16LE(0) === 0xffff || !/^[DF][A-Za-z0-9_]{1,6}$/.test(area.stage) ||
          ['F403', 'F406'].includes(area.stage) || !sameArea(p.scene, area) || !motionEligible(p)) break;
        let valid = true;
        for (const offset of [14, 18, 22, 32, 36, 40]) {
          const v = data.readFloatLE(offset);
          if (!Number.isFinite(v) || Math.abs(v) > (offset >= 32 ? 1000 : 1e6)) valid = false;
        }
        if (!valid || others(p).some(o => o.team === p.team && o.id < p.id && sameArea(o.scene, area) && motionEligible(o))) break;
        for (const o of others(p)) if (o.team === p.team && sameArea(o.scene, area) && o.sock.writableLength < 65536)
          send(o, forwarded(T.MOTION, p, body));
        break;
      }
      default: break;  // unknown types are ignored, so newer clients can add optional messages
    }
  }

  const server = net.createServer(sock => {
    sock.setNoDelay(true);
    const state = { sock, p: null, buf: Buffer.alloc(0) };
    sock.setTimeout(30000, () => sock.destroy());
    sock.on('data', chunk => {
      state.buf = Buffer.concat([state.buf, chunk]);
      try {
        while (state.buf.length >= 4) {
          const len = state.buf.readUInt32LE(0);
          if (len < 1 || len > MAX_FRAME) throw new Error(`bad frame length ${len}`);
          if (state.buf.length < 4 + len) break;
          const type = state.buf[4];
          const body = state.buf.subarray(5, 4 + len);
          state.buf = state.buf.subarray(4 + len);
          onFrame(state, type, body);
          if (sock.destroyed) return;
        }
      } catch (e) {
        log(`drop connection: ${e.message}`);
        sock.destroy();
      }
    });
    sock.on('close', () => { if (state.p && state.p.sock === sock) { state.p.sock = null; leave(state.p); } });
    sock.on('error', () => {});
  });
  return { server, rooms };
}

module.exports = { createRelay, PROTO, T, frame, strBuf };

if (require.main === module) {
  const port = Number(process.env.PORT || process.argv[2] || 7623);
  const host = process.env.HOST || '0.0.0.0';
  const { server } = createRelay({ log: m => console.log(new Date().toISOString(), m) });
  server.listen(port, host, () => console.log(`Skyward Sword Together relay on tcp://${host}:${port}`));
}
