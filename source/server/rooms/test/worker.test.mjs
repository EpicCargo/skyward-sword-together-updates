import test from "node:test";
import assert from "node:assert/strict";
import { webcrypto } from "node:crypto";
import { serialize } from "node:v8";
import * as entrypoint from "../worker.mjs";
import worker, { RoomRelay } from "../worker.mjs";

// Independent documented v1 wire/resource fixtures; entrypoint exports stay valid workerd handlers.
const CODE_ALPHABET = "ABCDEFGHJKLMNPQRSTUVWXYZ23456789";
const TTL_MS = 6 * 60 * 60 * 1000, MAX_DATA = 32768, MAX_PENDING = 256 * 1024, MAX_CREDITS = 512;
function mux(op, channel, payload = new Uint8Array()) {
  const bytes = new Uint8Array(5 + payload.length); bytes[0] = op;
  new DataView(bytes.buffer).setUint32(1, channel, true); bytes.set(payload, 5); return bytes;
}

test("production entrypoint exposes only Worker and Durable Object handlers", () => {
  assert.deepEqual(Object.keys(entrypoint).sort(), ["RoomRelay", "default"]);
  assert.equal(typeof entrypoint.RoomRelay, "function");
  assert.equal(typeof entrypoint.default.fetch, "function");
});

if (!globalThis.crypto) globalThis.crypto = webcrypto;
const NativeResponse = globalThis.Response;
globalThis.Response = class extends NativeResponse {
  constructor(body, options = {}) {
    super(body, options.status === 101 ? { ...options, status: 200 } : options);
    if (options.status === 101) Object.defineProperty(this, "status", { value: 101 });
    this.webSocket = options.webSocket;
  }
};
class Socket {
  readyState = 1; sent = []; closes = []; attachment = null;
  send(value) { if (this.readyState !== 1) throw new Error("closed"); this.sent.push(typeof value === "string" ? value : new Uint8Array(value)); }
  close(code, reason) { if (this.readyState === 3) return; this.readyState = 3; this.closes.push({ code, reason }); }
  serializeAttachment(value) { assert.ok(serialize(value).length <= 16384); this.attachment = structuredClone(value); }
  deserializeAttachment() { return structuredClone(this.attachment); }
}
globalThis.WebSocketPair = class { constructor() { this[0] = new Socket(); this[1] = new Socket(); } };
class Context {
  queue = Promise.resolve(); sockets = []; tags = new Map(); values = new Map(); writes = []; alarm = null;
  storage = {
    get: async key => structuredClone(this.values.get(key)),
    put: async (key, value) => { this.writes.push({ key, value: structuredClone(value) }); this.values.set(key, structuredClone(value)); },
    setAlarm: async at => { this.alarm = at; },
    deleteAll: async () => { this.values.clear(); },
  };
  blockConcurrencyWhile(fn) { const result = this.queue.then(fn); this.queue = result.catch(() => {}); return result; }
  acceptWebSocket(ws, tags) { this.sockets.push(ws); this.tags.set(ws, tags); }
  getWebSockets(tag) { return this.sockets.filter(ws => !tag || this.tags.get(ws).includes(tag)); }
  getTags(ws) { return this.tags.get(ws) || []; }
}
function environment() {
  const rooms = new Map();
  const env = { ROOMS_ENABLED: "true", CREATE_LIMITER: { limit: async () => ({ success: true }) },
    CREATE_GLOBAL_LIMITER: { limit: async () => ({ success: true }) }, CONNECT_LIMITER: { limit: async () => ({ success: true }) } };
  env.ROOMS = { idFromName: name => name, get: name => {
    if (!rooms.has(name)) { const ctx = new Context(); rooms.set(name, { ctx, room: new RoomRelay(ctx, env) }); }
    return { fetch: request => rooms.get(name).room.fetch(request) };
  } };
  return { env, rooms };
}
function request(path, options = {}) { return new Request("https://relay.example" + path, options); }
async function setup() {
  const fixture = environment(), response = await worker.fetch(request("/api/rooms", { method: "POST" }), fixture.env);
  assert.equal(response.status, 201); const metadata = await response.json();
  return { ...fixture, metadata, ...fixture.rooms.get(metadata.code) };
}
async function connect(f, role, headers = {}) {
  const authorization = role === "host" ? { Authorization: "Bearer " + f.metadata.host_token } : {};
  const response = await worker.fetch(request(`/r/${f.metadata.code}/${role}`, { headers: { Upgrade: "websocket", ...authorization, ...headers } }), f.env);
  return { response, socket: f.ctx.sockets.at(-1) };
}
function unwrap(frame) { return { nonce: Buffer.from(frame.subarray(0, 8)).toString("hex"), body: frame.subarray(8) }; }
async function ack(f, ws, frame) { await f.room.webSocketMessage(ws, JSON.stringify({ ack: unwrap(frame).nonce })); }
async function hosted() { const f = await setup(); f.host = (await connect(f, "host")).socket; return f; }
async function guest(f) {
  const result = await connect(f, "join"); assert.equal(result.response.status, 101);
  const opened = f.host.sent.at(-1); assert.equal(unwrap(opened).body[0], 1); await ack(f, f.host, opened);
  return result.socket;
}

test("public surface rejects disabled, malformed, cross-origin and unbounded requests", async () => {
  const { env, rooms } = environment(); env.ROOMS_ENABLED = "false";
  assert.equal((await worker.fetch(request("/api/rooms", { method: "POST" }), env)).status, 503); assert.equal(rooms.size, 0);
  env.ROOMS_ENABLED = "true";
  assert.equal(CODE_ALPHABET.length, 32); assert.equal(new Set(CODE_ALPHABET).size, 32);
  for (const [path, options, status] of [["/api/rooms", {}, 405], ["/rooms", {}, 404],
    ["/api/rooms?token=x", { method: "POST" }, 403], ["/api/rooms", { method: "POST", headers: { Origin: "https://other.example" } }, 403],
    ["/api/rooms", { method: "POST", body: "x".repeat(257) }, 413], ["/api/rooms", { method: "POST", body: '{"token":"x"}' }, 400]]) {
    assert.equal((await worker.fetch(request(path, options), env)).status, status);
  }
  env.CREATE_LIMITER.limit = async () => ({ success: false });
  assert.equal((await worker.fetch(request("/api/rooms", { method: "POST" }), env)).status, 429);
});

test("room creation keeps only hashed auth/metadata; role/auth/capacity errors are explicit", async () => {
  const f = await setup(), m = f.metadata;
  assert.match(m.code, /^[ABCDEFGHJKLMNPQRSTUVWXYZ23456789]{6}$/); assert.match(m.host_token, /^[0-9a-f]{64}$/);
  assert.equal(m.version, 1); assert.equal(m.credit_ack, true); assert.equal(m.envelope_bytes, 8);
  assert.ok(m.expires_at_ms > Date.now() && m.expires_at_ms <= Date.now() + TTL_MS);
  assert.equal(m.host_url, `wss://relay.example/r/${m.code}/host`);
  assert.equal(f.ctx.alarm, m.expires_at_ms);
  assert.ok(!JSON.stringify(f.ctx.writes).includes(m.host_token));
  assert.equal((await worker.fetch(request(`/r/${m.code}/host`), f.env)).status, 426);
  assert.equal((await connect(f, "host", { Authorization: "Bearer " + "0".repeat(64) })).response.status, 401);
  assert.equal((await connect(f, "join")).response.status, 409);
  const result = await connect(f, "host"); assert.equal(result.response.status, 101); f.host = result.socket;
  assert.equal((await connect(f, "host")).response.status, 409);
  const guests = []; for (let i = 0; i < 3; i++) guests.push(await guest(f));
  assert.equal((await connect(f, "join")).response.status, 409);
  assert.equal(new Set(guests.map(ws => ws.attachment.channel)).size, 3);
  assert.equal((await worker.fetch(request(`/r/${m.code}/invalid`, { headers: { Upgrade: "websocket" } }), f.env)).status, 404);
});

test("opaque gameplay bytes route only through their channel without persistence", async () => {
  const f = await hosted(), a = await guest(f), b = await guest(f);
  const payload = Uint8Array.from([45, 0, 0x33, 0x34, 0xff, 0, 1, 2]);
  await f.room.webSocketMessage(a, payload.buffer);
  const incoming = f.host.sent.at(-1), body = unwrap(incoming).body;
  assert.equal(body[0], 2); assert.equal(new DataView(body.buffer, body.byteOffset).getUint32(1, true), a.attachment.channel);
  assert.deepEqual(body.subarray(5), payload); await ack(f, f.host, incoming);
  await f.room.webSocketMessage(f.host, mux(2, a.attachment.channel, payload));
  assert.deepEqual(unwrap(a.sent.at(-1)).body, payload); assert.equal(b.sent.length, 0);
  await ack(f, a, a.sent.at(-1)); assert.equal(a.attachment.pending, 0);
  const writes = JSON.stringify(f.ctx.writes); assert.ok(!writes.includes("payload"));
  assert.deepEqual(new Set(f.ctx.writes.map(entry => entry.key)), new Set(["meta"]));
  await f.room.webSocketMessage(f.host, mux(2, 999, payload)); assert.equal(b.sent.length, 0);
});

test("host close routes one guest; disconnect signals host and channel IDs never repeat", async () => {
  const f = await hosted(), a = await guest(f), b = await guest(f), old = a.attachment.channel;
  await f.room.webSocketClose(a); const closed = unwrap(f.host.sent.at(-1)).body;
  assert.equal(closed[0], 3); assert.equal(new DataView(closed.buffer, closed.byteOffset).getUint32(1, true), old);
  assert.equal(new DataView(closed.buffer, closed.byteOffset).getUint16(5, true), 1000);
  const next = await guest(f); assert.ok(next.attachment.channel > old);
  const reason = new Uint8Array([0xe8, 3, ...new TextEncoder().encode("Local game closed")]);
  await f.room.webSocketMessage(f.host, mux(3, b.attachment.channel, reason));
  assert.equal(b.closes.at(-1).code, 1000); assert.equal(next.readyState, 1);
});

test("host loss revokes auth and every guest, including close with unavailable attachments", async () => {
  const f = await hosted(), a = await guest(f); f.host.attachment = null;
  await f.room.webSocketClose(f.host); assert.equal(a.readyState, 3); assert.equal(f.room.meta.revoked, true);
  assert.equal(f.room.meta.host_hash, ""); assert.equal((await connect(f, "host")).response.status, 410);
  assert.equal((await connect(f, "join")).response.status, 410);
});

test("missing current host state during a message closes the unusable room", async () => {
  const f = await hosted(), a = await guest(f), b = await guest(f);
  f.host.attachment = null;
  await f.room.webSocketMessage(f.host, mux(2, a.attachment.channel, Uint8Array.from([1])));
  assert.equal(f.room.meta.revoked, true);
  assert.ok([f.host, a, b].every(ws => ws.readyState === 3));
  assert.equal((await connect(f, "join")).response.status, 410);
});

test("guest admission detects missing host state and closes the unusable room", async () => {
  const f = await hosted(), a = await guest(f);
  f.host.attachment = null;
  assert.equal((await connect(f, "join")).response.status, 410);
  assert.equal(f.room.meta.revoked, true);
  assert.ok(f.ctx.sockets.every(ws => ws.readyState === 3)); assert.equal(a.readyState, 3);
});

test("unreadable guest state closes only that guest and preserves the other players", async () => {
  const f = await hosted(), a = await guest(f), b = await guest(f);
  const channel = a.attachment.channel, before = f.host.sent.length;
  a.deserializeAttachment = () => { throw new Error("injected unavailable attachment"); };
  await f.room.webSocketMessage(a, Uint8Array.from([1]));
  assert.equal(a.readyState, 3); assert.equal(f.host.readyState, 1); assert.equal(b.readyState, 1);
  const closed = unwrap(f.host.sent.at(-1)).body;
  assert.equal(f.host.sent.length, before + 1); assert.equal(closed[0], 3);
  assert.equal(new DataView(closed.buffer, closed.byteOffset).getUint32(1, true), channel);
  assert.equal(new DataView(closed.buffer, closed.byteOffset).getUint16(5, true), 1011);
  await ack(f, f.host, f.host.sent.at(-1));
  const payload = Uint8Array.from([45, 0, 255]);
  await f.room.webSocketMessage(f.host, mux(2, b.attachment.channel, payload));
  assert.deepEqual(unwrap(b.sent.at(-1)).body, payload);
  assert.equal((await guest(f)).readyState, 1);
});

test("expiry/alarm closes sockets and deletes metadata; an early alarm reschedules safely", async () => {
  const f = await hosted(), a = await guest(f); await f.room.alarm(); assert.equal(a.readyState, 1);
  f.room.meta.expires_at_ms = Date.now() - 1; await f.room.alarm();
  assert.equal(a.readyState, 3); assert.equal(f.host.readyState, 3); assert.equal(f.ctx.values.size, 0);
});

test("binary size/direction/close guards and rate limits close only the proper scope", async () => {
  const f = await hosted(), a = await guest(f), b = await guest(f);
  await f.room.webSocketMessage(a, new Uint8Array(MAX_DATA + 1)); assert.equal(a.closes.at(-1).code, 1009);
  assert.equal(f.host.readyState, 1); assert.equal(b.readyState, 1);
  await f.room.webSocketMessage(f.host, mux(1, b.attachment.channel));
  assert.equal(f.host.closes.at(-1).code, 1002); assert.equal(f.room.meta.revoked, true);
  const second = await hosted(), peer = await guest(second);
  await second.room.webSocketMessage(peer, '{"ack":999}'); assert.equal(peer.closes.at(-1).code, 1008);
});

test("nonce credit limits reject blind/duplicate ACKs and cap a slow reader's bytes", async () => {
  const f = await hosted(), a = await guest(f), channel = a.attachment.channel;
  for (let i = 0; i < 8; i++) await f.room.webSocketMessage(f.host, mux(2, channel, new Uint8Array(MAX_DATA)));
  assert.equal(a.closes.at(-1).code, 1013); assert.ok(a.attachment.pending <= MAX_PENDING);
  const other = await guest(f); await f.room.webSocketMessage(f.host, mux(2, other.attachment.channel, new Uint8Array([1])));
  const frame = other.sent.at(-1); await ack(f, other, frame); assert.equal(other.attachment.pending, 0);
  await ack(f, other, frame); assert.equal(other.closes.at(-1).code, 1008);
});

test("a backward clock adjustment resets the rate window without ejecting healthy players", async t => {
  const f = await hosted(), a = await guest(f), payload = Uint8Array.from([45]);
  let wall = Date.now();
  t.mock.method(Date, "now", () => wall);
  for (let i = 0; i < 500; i++) {
    await f.room.webSocketMessage(a, payload);
    await ack(f, f.host, f.host.sent.at(-1));
  }
  assert.equal(f.host.readyState, 1); assert.equal(a.readyState, 1);
  wall -= 2000;
  for (let i = 0; i < 20; i++) {
    await f.room.webSocketMessage(a, payload);
    await ack(f, f.host, f.host.sent.at(-1));
    assert.equal(f.host.readyState, 1); assert.equal(a.readyState, 1);
  }
  assert.equal(f.host.attachment.pending, 0); assert.equal(f.room.meta.revoked, false);
  assert.equal(a.attachment.rate_messages, 20); assert.equal(f.host.attachment.rate_messages, 20);
});

test("the message limit still applies inside a new backward-clock rate window", async t => {
  const f = await hosted(), a = await guest(f);
  const wall = Date.now() - 2000;
  t.mock.method(Date, "now", () => wall);
  for (let i = 0; i < 512; i++) await f.room.webSocketMessage(f.host, mux(2, 999));
  assert.equal(f.host.readyState, 1); assert.equal(a.readyState, 1);
  await f.room.webSocketMessage(f.host, mux(2, 999));
  assert.equal(f.host.readyState, 3); assert.equal(a.readyState, 3);
  assert.equal(f.room.meta.revoked, true);
});

test("credit frame count and serialized attachment size stay bounded across hibernation", async () => {
  const f = await hosted(), a = await guest(f), channel = a.attachment.channel;
  let hostState = f.host.deserializeAttachment(); hostState.rate_messages = 0; f.host.serializeAttachment(hostState);
  for (let i = 0; i < MAX_CREDITS; i++) await f.room.webSocketMessage(f.host, mux(2, channel));
  assert.equal(a.readyState, 1); assert.ok(serialize(a.attachment).length <= 16384);
  f.room = new RoomRelay(f.ctx, f.env); await f.room.ready;
  await ack(f, a, a.sent[0]); assert.equal(a.attachment.pending, 8 * (MAX_CREDITS - 1));
  hostState = f.host.deserializeAttachment(); hostState.rate_at = Date.now() - 1000; hostState.rate_messages = 0; f.host.serializeAttachment(hostState);
  await f.room.webSocketMessage(f.host, mux(2, channel)); assert.equal(a.readyState, 1);
  // Reset the inbound rate window so this specifically exercises outstanding-frame credit.
  hostState = f.host.deserializeAttachment(); hostState.rate_at = Date.now() - 1000; hostState.rate_messages = 0; f.host.serializeAttachment(hostState);
  await f.room.webSocketMessage(f.host, mux(2, channel)); assert.equal(a.closes.at(-1).code, 1013);
});

test("concurrent guest admission cannot exceed three slots", async () => {
  const f = await hosted(); const results = await Promise.all(Array.from({ length: 6 }, () => connect(f, "join")));
  assert.equal(results.filter(r => r.response.status === 101).length, 3);
  assert.equal(results.filter(r => r.response.status === 409).length, 3);
});

test("revocation closes every socket even when its durable write fails", async () => {
  const f = await hosted(), a = await guest(f), b = await guest(f);
  const put = f.ctx.storage.put;
  f.ctx.storage.put = async () => { throw new Error("injected revoke failure"); };
  await assert.rejects(f.room.revoke("Host disconnected"), /injected revoke failure/);
  assert.equal(f.room.meta.revoked, true); assert.equal(f.room.meta.host_hash, "");
  assert.ok([f.host, a, b].every(ws => ws.readyState === 3));
  // A retry on the same instance must complete the failed durable revocation.
  f.ctx.storage.put = put;
  await f.room.revoke("Retry cleanup");
  assert.equal(f.ctx.values.get("meta").revoked, true);
  assert.equal(f.ctx.values.get("meta").host_hash, "");
});

test("failed host admission closes the unreturned socket and cannot reopen the room", async () => {
  const f = await setup(), put = f.ctx.storage.put;
  f.ctx.storage.put = async (key, value) => {
    if (value.connected && !value.revoked) throw new Error("injected host admission failure");
    return put(key, value);
  };
  const result = await connect(f, "host");
  assert.equal(result.response.status, 503);
  assert.ok(f.ctx.sockets.every(ws => ws.readyState === 3));
  f.ctx.storage.put = put;
  assert.equal((await connect(f, "host")).response.status, 410);
  assert.equal((await connect(f, "join")).response.status, 410);
  assert.equal(f.ctx.values.get("meta").host_hash, "");
});

test("failed guest attachment releases its socket and keeps the other peers usable", async () => {
  const f = await hosted(), first = await guest(f);
  const accept = f.ctx.acceptWebSocket;
  f.ctx.acceptWebSocket = function(ws, tags) {
    accept.call(this, ws, tags);
    ws.serializeAttachment = () => { throw new Error("injected attachment failure"); };
  };
  const failed = await connect(f, "join");
  assert.equal(failed.response.status, 503);
  assert.equal(failed.socket.readyState, 3);
  assert.equal(first.readyState, 1); assert.equal(f.host.readyState, 1);
  f.ctx.acceptWebSocket = accept;
  const next = await guest(f);
  assert.equal(next.readyState, 1);
  const payload = new Uint8Array([0, 0x34, 255]);
  await f.room.webSocketMessage(next, payload);
  assert.deepEqual(unwrap(f.host.sent.at(-1)).body.subarray(5), payload);
});

test("failed upgrade response closes an already-opened guest backend channel", async () => {
  const f = await hosted(), first = await guest(f), before = f.host.sent.length;
  const Response = globalThis.Response;
  globalThis.Response = class extends Response {
    constructor(body, options = {}) {
      if (options.status === 101) throw new Error("injected upgrade response failure");
      super(body, options);
    }
  };
  let failed;
  try { failed = await connect(f, "join"); }
  finally { globalThis.Response = Response; }
  assert.equal(failed.response.status, 503); assert.equal(failed.socket.readyState, 3);
  assert.equal(f.host.readyState, 1); assert.equal(first.readyState, 1);
  const frames = f.host.sent.slice(before), opened = unwrap(frames[0]).body, closed = unwrap(frames[1]).body;
  assert.equal(frames.length, 2); assert.equal(opened[0], 1); assert.equal(closed[0], 3);
  assert.deepEqual(closed.subarray(1, 5), opened.subarray(1, 5));
  assert.equal(new DataView(closed.buffer, closed.byteOffset).getUint16(5, true), 1011);
  for (const frame of frames) await ack(f, f.host, frame);
  assert.equal(f.host.attachment.pending, 0);
  assert.equal((await guest(f)).readyState, 1);
});

test("failed host setup with failed revocation still closes sockets and retries its pending write", async () => {
  const f = await setup(), put = f.ctx.storage.put;
  f.ctx.storage.put = async () => { throw new Error("injected storage outage"); };
  assert.equal((await connect(f, "host")).response.status, 503);
  assert.ok(f.ctx.sockets.every(ws => ws.readyState === 3));
  assert.equal(f.room.meta.revoked, true); assert.equal(f.room.revocationPending, true);
  f.ctx.storage.put = put;
  assert.equal((await connect(f, "host")).response.status, 410);
  assert.equal(f.ctx.values.get("meta").revoked, true); assert.equal(f.room.revocationPending, false);
});

test("a recoverable guest setup error never escapes the concurrency callback and resets its room", async () => {
  const f = await hosted(), first = await guest(f);
  const blocked = f.ctx.blockConcurrencyWhile.bind(f.ctx), accept = f.ctx.acceptWebSocket;
  let resets = 0;
  // Model Cloudflare's documented reset-on-throw contract, beyond the ordinary
  // queue-only shim. A guest admission failure must stay inside its callback.
  f.ctx.blockConcurrencyWhile = fn => blocked(async () => {
    try { return await fn(); }
    catch (error) {
      resets++;
      for (const ws of f.ctx.sockets) ws.close(1011, "Runtime reset");
      throw error;
    }
  });
  f.ctx.acceptWebSocket = function(ws, tags) {
    accept.call(this, ws, tags);
    ws.serializeAttachment = () => { throw new Error("injected guest setup failure"); };
  };
  const failed = await connect(f, "join");
  assert.equal(failed.response.status, 503); assert.equal(failed.socket.readyState, 3);
  assert.equal(resets, 0);
  assert.equal(f.host.readyState, 1); assert.equal(first.readyState, 1);
  f.ctx.acceptWebSocket = accept;
  assert.equal((await guest(f)).readyState, 1);
});

async function replaceExpiredRoom(f) {
  f.room.meta.expires_at_ms = Date.now() - 1;
  await f.room.alarm();
  const hash = Buffer.from(await crypto.subtle.digest("SHA-256",new TextEncoder().encode(f.metadata.host_token))).toString("hex");
  const created = await f.room.fetch(new Request("https://room.internal/_create",{method:"POST",body:JSON.stringify({
    code:f.metadata.code,host_hash:hash,expires_at_ms:Date.now()+TTL_MS/2,
  })}));
  assert.equal(created.status,201);
  const host = await connect(f,"host");assert.equal(host.response.status,101);f.host=host.socket;
  return guest(f);
}

test("failed guest channel assignment stays inside admission and preserves existing players", async () => {
  for (const afterCommit of [false, true]) {
    const f = await hosted(), first = await guest(f), before = f.host.sent.length;
    const blocked = f.ctx.blockConcurrencyWhile.bind(f.ctx);
    let resets = 0;
    f.ctx.blockConcurrencyWhile = fn => blocked(async () => {
      try { return await fn(); }
      catch (error) {
        resets++;
        for (const ws of f.ctx.sockets) ws.close(1011, "Runtime reset");
        throw error;
      }
    });
    const write = f.host.serializeAttachment;
    f.host.serializeAttachment = function(state) {
      if (afterCommit) write.call(this, state);
      throw new Error("injected channel assignment failure");
    };
    const failed = await connect(f, "join");
    f.host.serializeAttachment = write;
    assert.equal(failed.response.status, 503);
    assert.equal(resets, 0);
    assert.equal(f.host.readyState, 1); assert.equal(first.readyState, 1);
    assert.equal(f.host.sent.length, before);
    const next = await guest(f);
    assert.ok(next.attachment.channel > first.attachment.channel);
    const payload = Uint8Array.from([45, 0, 255]);
    await f.room.webSocketMessage(next, payload);
    assert.deepEqual(unwrap(f.host.sent.at(-1)).body.subarray(5), payload);
  }
});

test("a delayed old host close with lost attachment cannot revoke a replacement room", async () => {
  const f=await hosted(),old=f.host;
  const current=await replaceExpiredRoom(f);
  old.attachment=null;
  await f.room.webSocketClose(old);
  assert.equal(f.host.readyState,1);assert.equal(current.readyState,1);
  assert.equal(f.room.meta.revoked,false);
});

test("a delayed old guest close cannot close a new room's identically numbered channel", async () => {
  const f=await hosted(),old=await guest(f),oldChannel=old.attachment.channel;
  const current=await replaceExpiredRoom(f);
  assert.equal(current.attachment.channel,oldChannel);
  const before=f.host.sent.length;
  old.attachment=null;
  await f.room.webSocketError(old);
  assert.equal(f.host.sent.length,before);
  assert.equal(f.host.readyState,1);assert.equal(current.readyState,1);
  const payload=Uint8Array.from([0x34,0,255]);
  await f.room.webSocketMessage(current,payload);
  assert.deepEqual(unwrap(f.host.sent.at(-1)).body.subarray(5),payload);
});

test("room identity persists across object rehydration and current lost-attachment close still revokes", async () => {
  const f=await hosted(),a=await guest(f);
  assert.match(f.room.meta.room_instance,/^[0-9a-f]{32}$/);
  assert.equal(f.ctx.getTags(f.host).length,3);
  f.room=new RoomRelay(f.ctx,f.env);await f.room.ready;
  const payload=Uint8Array.from([1,2,3]);await f.room.webSocketMessage(a,payload);
  assert.deepEqual(unwrap(f.host.sent.at(-1)).body.subarray(5),payload);
  f.host.attachment=null;
  await f.room.webSocketClose(f.host);
  assert.equal(a.readyState,3);assert.equal(f.room.meta.revoked,true);
});

test("active legacy rooms without instance metadata retain normal forwarding and host-close cleanup", async () => {
  const f=await hosted();delete f.room.meta.room_instance;
  await f.ctx.storage.put("meta",f.room.meta);
  const host=f.host.deserializeAttachment();delete host.room_instance;f.host.serializeAttachment(host);
  f.ctx.tags.set(f.host,["host","channel:0"]);
  f.room=new RoomRelay(f.ctx,f.env);await f.room.ready;
  const a=await guest(f),payload=Uint8Array.from([3,4,5]);
  assert.equal(f.ctx.getTags(a).length,2);
  await f.room.webSocketMessage(a,payload);
  assert.deepEqual(unwrap(f.host.sent.at(-1)).body.subarray(5),payload);
  f.host.attachment=null;await f.room.webSocketClose(f.host);
  assert.equal(a.readyState,3);assert.equal(f.room.meta.revoked,true);
});

function creationRequest(expires = Date.now() + TTL_MS / 2) {
  return new Request("https://room.internal/_create", { method: "POST", body: JSON.stringify({
    code: "ABC234", host_hash: "a".repeat(64), expires_at_ms: expires,
  }) });
}

test("initialization alarm failure leaves no metadata and permits a clean retry", async () => {
  const ctx = new Context(), room = new RoomRelay(ctx, {}); await room.ready;
  const original = ctx.storage.setAlarm;
  ctx.storage.setAlarm = async () => { throw new Error("injected alarm failure"); };
  await assert.rejects(room.fetch(creationRequest()), /injected alarm failure/);
  assert.equal(ctx.values.has("meta"), false); assert.equal(ctx.writes.length, 0);
  assert.equal(room.meta, undefined); assert.equal(ctx.alarm, null);
  ctx.storage.setAlarm = original;
  assert.equal((await room.fetch(creationRequest())).status, 201);
  assert.equal(ctx.alarm, room.meta.expires_at_ms); assert.equal(ctx.values.get("meta").code, "ABC234");
});

test("initialization metadata failure retains bounded cleanup without publishing a room", async () => {
  const ctx = new Context(), room = new RoomRelay(ctx, {}); await room.ready;
  const put = ctx.storage.put, expires = Date.now() + TTL_MS / 2;
  ctx.storage.put = async () => { throw new Error("injected metadata failure"); };
  await assert.rejects(room.fetch(creationRequest(expires)), /injected metadata failure/);
  assert.equal(ctx.alarm, expires); assert.equal(ctx.values.has("meta"), false); assert.equal(room.meta, undefined);
  ctx.storage.put = put;
  assert.equal((await room.fetch(creationRequest(expires))).status, 201);
  assert.equal(room.meta.expires_at_ms, expires); assert.equal(ctx.alarm, expires);
});

test("initialization ambiguous metadata commit is still removed by the secured alarm", async t => {
  const ctx = new Context(), room = new RoomRelay(ctx, {}); await room.ready;
  const put = ctx.storage.put, expires = Date.now() + TTL_MS / 2;
  ctx.storage.put = async (key, value) => { await put(key, value); throw new Error("injected post-commit failure"); };
  await assert.rejects(room.fetch(creationRequest(expires)), /injected post-commit failure/);
  assert.equal(room.meta, undefined); assert.equal(ctx.values.has("meta"), true); assert.equal(ctx.alarm, expires);
  ctx.storage.put = put;
  const rehydrated = new RoomRelay(ctx, {}); await rehydrated.ready;
  assert.equal(rehydrated.meta.code, "ABC234");
  t.mock.method(Date, "now", () => expires + 1);
  await rehydrated.alarm(); assert.equal(ctx.values.size, 0); assert.equal(rehydrated.meta, null);
});

test("initialization publishes active state only after alarm and metadata persistence", async () => {
  const ctx = new Context(), room = new RoomRelay(ctx, {}); await room.ready;
  let complete, entered;
  const blocked = new Promise(resolve => { complete = resolve; });
  const started = new Promise(resolve => { entered = resolve; });
  const put = ctx.storage.put, expires = Date.now() + TTL_MS / 2;
  ctx.storage.put = async (key, value) => { entered(); await blocked; return put(key, value); };
  const creating = room.fetch(creationRequest(expires)); await started;
  assert.equal(ctx.alarm, expires); assert.equal(room.meta, undefined); assert.equal(ctx.values.has("meta"), false);
  complete(); assert.equal((await creating).status, 201);
  assert.equal(room.meta.expires_at_ms, expires); assert.equal(ctx.values.get("meta").code, "ABC234");
});
