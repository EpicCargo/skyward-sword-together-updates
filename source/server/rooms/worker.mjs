// SST room transport only. Native C host remains the gameplay/protocol authority.
import { DurableObject } from "cloudflare:workers";

const CODE_ALPHABET = "ABCDEFGHJKLMNPQRSTUVWXYZ23456789";
const CODE_RE = /^[ABCDEFGHJKLMNPQRSTUVWXYZ23456789]{6}$/;
const TTL_MS = 6 * 60 * 60 * 1000;
const MAX_DATA = 32768;
const MAX_PENDING = 256 * 1024;
const MAX_CREDITS = 512;
const encoder = new TextEncoder();
const decoder = new TextDecoder("utf-8", { fatal: true });
const OPEN = 1, DATA = 2, CLOSE = 3;

function json(status, error, extra = {}) {
  return new Response(JSON.stringify(error ? { error, ...extra } : extra), {
    status, headers: { "content-type": "application/json", "cache-control": "no-store", "x-content-type-options": "nosniff" },
  });
}
function random(n) { return crypto.getRandomValues(new Uint8Array(n)); }
function hex(bytes) { return Array.from(bytes, b => b.toString(16).padStart(2, "0")).join(""); }
async function digest(text) { return hex(new Uint8Array(await crypto.subtle.digest("SHA-256", encoder.encode(text)))); }
function equal(a, b) {
  if (typeof a !== "string" || typeof b !== "string" || a.length !== b.length) return false;
  let different = 0; for (let i = 0; i < a.length; i++) different |= a.charCodeAt(i) ^ b.charCodeAt(i);
  return different === 0;
}
function binary(value) {
  if (value instanceof ArrayBuffer) return new Uint8Array(value);
  if (ArrayBuffer.isView(value)) return new Uint8Array(value.buffer, value.byteOffset, value.byteLength);
  return null;
}
function mux(op, channel, payload = new Uint8Array()) {
  const result = new Uint8Array(5 + payload.length); result[0] = op;
  new DataView(result.buffer).setUint32(1, channel, true); result.set(payload, 5); return result;
}
function closePayload(code, reason) {
  const text = encoder.encode(reason).slice(0, 96), body = new Uint8Array(2 + text.length);
  new DataView(body.buffer).setUint16(0, code, true); body.set(text, 2); return body;
}
function validClose(code) { return code === 1000 || code === 1001 || code === 1002 || code === 1003 || (code >= 1007 && code <= 1014) || (code >= 3000 && code <= 4999); }
function pack(bytes) { return btoa(String.fromCharCode(...bytes)); }
function unpack(text) { return Uint8Array.from(atob(text), c => c.charCodeAt(0)); }
function credits(state) {
  const list = unpack(state.credits || "");
  if (list.length % 10 || list.length > MAX_CREDITS * 10) throw new Error("invalid credits");
  return list;
}
function setState(ws, state) { ws.serializeAttachment(state); }
function readState(ws) { try { return ws.deserializeAttachment(); } catch { return null; } }
function sockets(ctx, role) { return ctx.getWebSockets(role).filter(ws => ws.readyState === 1 && !readState(ws)?.closed); }
function stop(ws, code, reason) {
  try { const state = ws.deserializeAttachment(); if (state) { state.closed = true; setState(ws, state); } } catch { /* metadata may be unavailable after close */ }
  try { ws.close(code, reason); } catch { /* no payload or credentials logged */ }
}
async function smallBody(request, max = 256) {
  if (!request.body) return "";
  const reader = request.body.getReader(); let size = 0, pieces = [];
  for (;;) {
    const { done, value } = await reader.read(); if (done) break;
    size += value.length; if (size > max) { await reader.cancel(); throw new Error("body too large"); }
    pieces.push(value);
  }
  const bytes = new Uint8Array(size); let at = 0; for (const piece of pieces) { bytes.set(piece, at); at += piece.length; }
  return decoder.decode(bytes);
}
async function limited(env, binding, request, prefix) {
  if (!env[binding]?.limit) return false; // service remains closed without configured abuse controls
  const actor = request.headers.get("CF-Connecting-IP") || "local";
  const { success } = await env[binding].limit({ key: prefix + await digest(actor) }); return success;
}

export default {
  async fetch(request, env) {
    try {
      if (env.ROOMS_ENABLED !== "true") return json(503, "rooms_disabled");
      const url = new URL(request.url);
      if (url.protocol !== "https:" || request.headers.has("Origin") || url.search) return json(403, "request_not_allowed");
      if (url.pathname === "/api/rooms") {
        if (request.method !== "POST") return json(405, "method_not_allowed");
        let body; try { body = (await smallBody(request)).trim(); } catch { return json(413, "body_too_large"); }
        if (body && body !== "{}") return json(400, "empty_body_required");
        if (!env.CREATE_GLOBAL_LIMITER?.limit || !(await env.CREATE_GLOBAL_LIMITER.limit({ key: "rooms" })).success ||
            !await limited(env, "CREATE_LIMITER", request, "create:")) return json(429, "create_rate_limited");
        for (let attempt = 0; attempt < 8; attempt++) {
          const code = Array.from(random(6), b => CODE_ALPHABET[b & 31]).join("");
          const token = hex(random(32)), expires = Date.now() + TTL_MS;
          const stub = env.ROOMS.get(env.ROOMS.idFromName(code));
          const result = await stub.fetch(new Request("https://room.internal/_create", {
            method: "POST", body: JSON.stringify({ code, host_hash: await digest(token), expires_at_ms: expires }),
          }));
          if (result.status === 409) continue;
          if (result.status !== 201) return json(503, "room_create_failed");
          const base = `wss://${url.host}/r/${code}`;
          return json(201, null, { version: 1, code, host_token: token, expires_at_ms: expires,
            host_url: base + "/host", join_url: base + "/join", max_guests: 3, max_data_bytes: MAX_DATA,
            credit_ack: true, envelope_bytes: 8 });
        }
        return json(503, "room_codes_busy");
      }
      const match = /^\/r\/([ABCDEFGHJKLMNPQRSTUVWXYZ23456789]{6})\/(host|join)$/.exec(url.pathname);
      if (!match) return json(404, "not_found");
      if (request.method !== "GET") return json(405, "method_not_allowed");
      if (request.headers.get("Upgrade")?.toLowerCase() !== "websocket") return json(426, "websocket_required");
      if (match[2] === "host" && !/^Bearer [0-9a-f]{64}$/.test(request.headers.get("Authorization") || "")) return json(401, "host_auth_required");
      if (!await limited(env, "CONNECT_LIMITER", request, "connect:")) return json(429, "connect_rate_limited");
      return await env.ROOMS.get(env.ROOMS.idFromName(match[1])).fetch(request);
    } catch { return json(503, "relay_unavailable"); }
  },
};

export class RoomRelay extends DurableObject {
  constructor(ctx, env) {
    super(ctx, env);
    this.revocationPending = false;
    this.ready = ctx.blockConcurrencyWhile(async () => { this.meta = await ctx.storage.get("meta"); });
  }
  async fetch(request) {
    await this.ready;
    return this.ctx.blockConcurrencyWhile(() => this.admit(request));
  }
  belongs(ws) {
    const instance = this.meta?.room_instance;
    if (!instance) return true; // Existing pre-instance rooms retain their v1 lifecycle.
    let state;
    try { state = ws.deserializeAttachment(); } catch { state = null; }
    if (state?.room_instance !== undefined) return state.room_instance === instance;
    // Closing sockets can lose their attachment; accepted tags retain the room
    // identity. A callback from an expired incarnation cannot affect a new room.
    try { return this.ctx.getTags(ws).includes(`room:${instance}`); } catch { return false; }
  }
  activeSockets(role) { return sockets(this.ctx, role).filter(ws => this.belongs(ws)); }
  async admit(request) {
    const url = new URL(request.url);
    if (url.pathname === "/_create" && request.method === "POST") {
      if (this.meta) return json(409, "code_exists");
      let meta; try { meta = JSON.parse(await smallBody(request)); } catch { return json(400, "invalid_metadata"); }
      if (!CODE_RE.test(meta.code) || !/^[0-9a-f]{64}$/.test(meta.host_hash) || !Number.isSafeInteger(meta.expires_at_ms) ||
          meta.expires_at_ms <= Date.now() || meta.expires_at_ms > Date.now() + TTL_MS) return json(400, "invalid_metadata");
      const initialized = { code: meta.code, host_hash: meta.host_hash, created_at_ms: Date.now(), expires_at_ms: meta.expires_at_ms,
        revoked: false, connected: false, room_instance: hex(random(16)) };
      // Secure cleanup before storing any room/auth metadata. If the metadata write
      // fails (even after an ambiguous commit), its expiry alarm is already durable.
      // Publish active in-memory state only after both storage operations succeed.
      await this.ctx.storage.setAlarm(initialized.expires_at_ms);
      await this.ctx.storage.put("meta", initialized);
      this.meta = initialized;
      return json(201, null, { created: true });
    }
    const route = /^\/r\/([ABCDEFGHJKLMNPQRSTUVWXYZ23456789]{6})\/(host|join)$/.exec(url.pathname);
    if (!route || request.method !== "GET") return json(404, "not_found");
    if (!this.meta || this.meta.code !== route[1]) return json(404, "room_not_found");
    if (this.meta.revoked || Date.now() >= this.meta.expires_at_ms) { await this.revoke("Room expired"); return json(410, "room_gone"); }
    if (request.headers.get("Upgrade")?.toLowerCase() !== "websocket") return json(426, "websocket_required");
    const role = route[2], hosts = this.activeSockets("host");
    if (role === "host") {
      const authorization = request.headers.get("Authorization") || "";
      if (!/^Bearer [0-9a-f]{64}$/.test(authorization) || !equal(await digest(authorization.slice(7)), this.meta.host_hash)) return json(401, "host_auth_required");
      if (hosts.length) return json(409, "host_already_connected");
      if (this.meta.connected) { await this.revoke("Host disconnected"); return json(410, "room_gone"); }
    } else {
      if (!hosts.length) { if (this.meta.connected) await this.revoke("Host disconnected"); return json(this.meta.revoked ? 410 : 409, this.meta.revoked ? "room_gone" : "host_not_connected"); }
      if (this.activeSockets("guest").length >= 3) return json(409, "room_full");
    }
    const pair = new WebSocketPair(), [client, server] = Object.values(pair);
    server.binaryType = "arraybuffer";
    let channel = 0;
    let opened = false;
    try {
      if (role === "join") {
        const host = hosts[0], state = readState(host);
        if (!state) {
          stop(server, 1011, "Host state lost");
          await this.revoke("Host state lost"); return json(410, "room_gone");
        }
        channel = state.next_channel;
        if (!Number.isInteger(channel) || channel < 1 || channel >= 0xffffffff) {
          stop(server, 1001, "Channel limit reached");
          await this.revoke("Channel limit reached"); return json(410, "room_gone");
        }
        // Assignment is part of admission too. A failed attachment write cannot
        // escape the concurrency callback and reset the other players' sockets.
        state.next_channel++; setState(host, state);
      }
      const tags = [role === "host" ? "host" : "guest", `channel:${channel}`];
      const attachment = { role: role === "host" ? "host" : "guest", channel, next_channel: 1,
        credits: "", pending: 0, rate_at: Date.now(), rate_bytes: 0, rate_messages: 0, closed: false };
      if (this.meta.room_instance) {
        tags.push(`room:${this.meta.room_instance}`); attachment.room_instance = this.meta.room_instance;
      }
      this.ctx.acceptWebSocket(server, tags);
      setState(server, attachment);
      if (role === "host") { this.meta.connected = true; await this.ctx.storage.put("meta", this.meta); }
      else {
        opened = this.send(hosts[0], mux(OPEN, channel));
        if (!opened) { await this.revoke("Host backpressure"); return json(503, "host_backpressure"); }
      }
      return new Response(null, { status: 101, webSocket: client });
    } catch {
      // An accepted socket still belongs to this admission until its upgrade
      // response succeeds. Failed setup must not consume a slot or backend.
      stop(server, 1011, "Connection setup failed");
      try {
        if (role === "host") await this.revoke("Host setup failed");
        else if (opened && !this.send(hosts[0], mux(CLOSE, channel, closePayload(1011, "Guest setup failed"))))
          await this.revoke("Host backpressure");
      } catch {
        // revoke() already closes sockets in finally and retains its pending
        // durable write. Keep the handled failure inside this request callback.
      }
      // Throwing out of blockConcurrencyWhile resets the entire object. A
      // recovered guest admission must not disconnect the other players.
      return json(503, "relay_unavailable");
    }
  }
  send(ws, payload) {
    try {
      if (ws.readyState !== 1) return false;
      const state = ws.deserializeAttachment(), previous = credits(state), size = payload.length + 8;
      if (state.closed || state.pending + size > MAX_PENDING || previous.length / 10 >= MAX_CREDITS) return false;
      let nonce;
      const existing = new Set(Array.from({ length: previous.length / 10 }, (_, i) => hex(previous.subarray(i * 10, i * 10 + 8))));
      for (let tries = 0; tries < 8; tries++) { const candidate = random(8); if (!existing.has(hex(candidate))) { nonce = candidate; break; } }
      if (!nonce) return false;
      const list = new Uint8Array(previous.length + 10); list.set(previous); list.set(nonce, previous.length);
      new DataView(list.buffer).setUint16(previous.length + 8, size, true);
      const frame = new Uint8Array(size); frame.set(nonce); frame.set(payload, 8);
      state.credits = pack(list); state.pending += size; setState(ws, state); ws.send(frame); return true;
    } catch { return false; }
  }
  acknowledge(ws, text) {
    if (text.length > 64 || encoder.encode(text).length > 64) return false;
    let data; try { data = JSON.parse(text); } catch { return false; }
    if (!data || Object.keys(data).length !== 1 || typeof data.ack !== "string" || !/^[0-9a-f]{16}$/.test(data.ack)) return false;
    const state = ws.deserializeAttachment(), list = credits(state);
    for (let at = 0; at < list.length; at += 10) if (hex(list.subarray(at, at + 8)) === data.ack) {
      state.pending -= new DataView(list.buffer).getUint16(at + 8, true);
      const rest = new Uint8Array(list.length - 10); rest.set(list.subarray(0, at)); rest.set(list.subarray(at + 10), at);
      state.credits = pack(rest); setState(ws, state); return true;
    }
    return false;
  }
  async webSocketMessage(ws, message) {
    await this.ready;
    if (!this.belongs(ws)) { stop(ws, 1001, "Room replaced"); return; }
    if (!this.meta || this.meta.revoked || Date.now() >= this.meta.expires_at_ms) { await this.revoke("Room expired"); return; }
    const state = readState(ws);
    if (!state) { await this.disconnect(ws, 1011, "Connection state lost"); return; }
    if (state.closed) return;
    const bytes = binary(message), size = bytes?.length ?? encoder.encode(String(message)).length;
    const now = Date.now();
    // Wall time also survives hibernation, but can move backwards. Do not
    // accumulate several seconds of healthy traffic in a future-dated window.
    if (now < state.rate_at || now - state.rate_at >= 1000) {
      state.rate_at = now; state.rate_bytes = state.rate_messages = 0;
    }
    state.rate_bytes += size; state.rate_messages++; setState(ws, state);
    if (state.rate_messages > 512 || state.rate_bytes > 1024 * 1024) { await this.disconnect(ws, 1013, "Rate limit"); return; }
    if (typeof message === "string") { if (!this.acknowledge(ws, message)) await this.disconnect(ws, 1008, "Invalid credit acknowledgement"); return; }
    if (!bytes) { await this.disconnect(ws, 1003, "Binary transport required"); return; }
    if (state.role === "guest") {
      if (bytes.length > MAX_DATA) { await this.disconnect(ws, 1009, "Data message too large"); return; }
      const host = this.activeSockets("host")[0];
      if (!host || !this.send(host, mux(DATA, state.channel, bytes))) await this.revoke("Host backpressure");
      return;
    }
    if (bytes.length < 5 || bytes.length > MAX_DATA + 5) { await this.disconnect(ws, 1009, "Invalid host message size"); return; }
    const op = bytes[0], channel = new DataView(bytes.buffer, bytes.byteOffset, bytes.byteLength).getUint32(1, true);
    if (!channel || (op !== DATA && op !== CLOSE)) { await this.disconnect(ws, 1002, "Invalid host operation"); return; }
    const guest = this.activeSockets("guest").find(peer => readState(peer)?.channel === channel);
    if (!guest) return; // A late response cannot route into a reused channel (channels never repeat).
    if (op === DATA) { if (!this.send(guest, bytes.subarray(5))) await this.disconnect(guest, 1013, "Guest backpressure"); }
    else {
      if (bytes.length < 7 || bytes.length > 103) { await this.disconnect(ws, 1002, "Invalid close message"); return; }
      const code = new DataView(bytes.buffer, bytes.byteOffset).getUint16(5, true); let reason;
      try { reason = decoder.decode(bytes.subarray(7)); } catch { await this.disconnect(ws, 1002, "Invalid close reason"); return; }
      if (!validClose(code)) { await this.disconnect(ws, 1002, "Invalid close code"); return; }
      stop(guest, code, reason);
    }
  }
  async disconnect(ws, code, reason) {
    if (!this.belongs(ws)) { stop(ws, code, reason); return; }
    let state; try { state = ws.deserializeAttachment(); } catch { state = null; }
    if (!state) {
      let tags; try { tags = this.ctx.getTags(ws); } catch { tags = []; }
      if (!tags.includes("host") && !tags.includes("guest")) { await this.revoke("Connection state lost"); return; }
      const channel = Number(tags.find(tag => tag.startsWith("channel:"))?.slice(8));
      state = { role: tags.includes("host") ? "host" : "guest", channel };
    }
    if (state.closed) return;
    stop(ws, code, reason);
    if (state.role === "host") { await this.revoke("Host disconnected"); return; }
    const host = this.activeSockets("host")[0];
    if (host && !this.send(host, mux(CLOSE, state.channel, closePayload(code, reason)))) await this.revoke("Host backpressure");
  }
  async webSocketClose(ws) { await this.ready; await this.disconnect(ws, 1000, "Peer disconnected"); }
  async webSocketError(ws) { await this.ready; await this.disconnect(ws, 1011, "Peer transport error"); }
  async revoke(reason) {
    try {
      if (this.meta && (!this.meta.revoked || this.revocationPending)) {
        this.meta.revoked = true; this.meta.host_hash = ""; this.revocationPending = true;
        await this.ctx.storage.put("meta", this.meta);
        this.revocationPending = false;
      }
    } finally {
      // Storage failure cannot keep a disconnected room's transport alive.
      // Retain the pending write so a later cleanup retries its durable state.
      for (const ws of this.ctx.getWebSockets()) stop(ws, 1001, reason);
    }
  }
  async alarm() {
    await this.ready;
    if (this.meta && Date.now() < this.meta.expires_at_ms) { await this.ctx.storage.setAlarm(this.meta.expires_at_ms); return; }
    await this.revoke("Room expired"); await this.ctx.storage.deleteAll(); this.meta = null;
  }
}
