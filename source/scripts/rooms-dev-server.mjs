// Loopback-only Node fixture running the ACTUAL Worker with an in-memory CF shim.
// This is not Miniflare/Cloudflare validation and is never a deployment server.
import http from "node:http";
import https from "node:https";
import fs from "node:fs";
import { createHash } from "node:crypto";
import { registerHooks } from "node:module";
import { serialize } from "node:v8";

registerHooks({ resolve(specifier, context, next) {
  if (specifier === "cloudflare:workers") return { shortCircuit: true,
    url: "data:text/javascript,export class DurableObject { constructor(ctx, env) { this.ctx=ctx; this.env=env; } }" };
  return next(specifier, context);
} });

const NativeResponse = globalThis.Response;
globalThis.Response = class extends NativeResponse {
  constructor(body, options = {}) {
    super(body, options.status === 101 ? { ...options, status: 200 } : options);
    if (options.status === 101) Object.defineProperty(this, "status", { value: 101 });
    this.webSocket = options.webSocket;
  }
};

const connections = new Set();
let workerEventErrors = 0, pendingWorkerEvents = 0, maxRateMessages = 0, maxRateBytes = 0;
const closeCodes = {}, closeReasons = {}, networkErrors = {};
const decoder = new TextDecoder("utf-8", { fatal: true });
function encodeFrame(opcode, data) {
  data = Buffer.from(data); let header;
  if (data.length < 126) header = Buffer.from([128 | opcode, data.length]);
  else if (data.length < 65536) { header = Buffer.alloc(4); header[0] = 128 | opcode; header[1] = 126; header.writeUInt16BE(data.length, 2); }
  else { header = Buffer.alloc(10); header[0] = 128 | opcode; header[1] = 127; header.writeBigUInt64BE(BigInt(data.length), 2); }
  return Buffer.concat([header, data]);
}
class Endpoint {
  readyState = 1; attachment = null; binaryType = "arraybuffer";
  network = null; pending = []; pendingBytes = 0; notified = false; context = null;
  send(value) {
    if (this.readyState !== 1) throw new Error("closed");
    const frame = encodeFrame(typeof value === "string" ? 1 : 2,
      typeof value === "string" ? Buffer.from(value) : Buffer.from(value.buffer ?? value, value.byteOffset ?? 0, value.byteLength));
    if (!this.network) {
      if (this.pendingBytes + frame.length > 262144) throw new Error("fixture buffer limit");
      this.pending.push(frame); this.pendingBytes += frame.length;
    } else this.write(frame);
  }
  write(frame) {
    if (!this.network || this.network.destroyed || this.network.writableLength + frame.length > 262144)
      throw new Error("fixture backpressure");
    this.network.write(frame);
  }
  close(code = 1000, reason = "") {
    if (this.readyState !== 1) return;
    this.readyState = 3;
    closeCodes[code] = (closeCodes[code] || 0) + 1;
    const label = ["Rate limit", "Host backpressure", "Guest backpressure", "Host disconnected", "Peer disconnected"].includes(reason) ? reason : "other";
    closeReasons[label] = (closeReasons[label] || 0) + 1;
    const text = Buffer.from(reason).subarray(0, 96), payload = Buffer.alloc(2 + text.length);
    payload.writeUInt16BE(code); text.copy(payload, 2);
    if (this.network && !this.network.destroyed) this.network.end(encodeFrame(8, payload));
    this.notify(false);
  }
  serializeAttachment(value) {
    if (serialize(value).length > 16384) throw new Error("fixture attachment limit");
    maxRateMessages = Math.max(maxRateMessages, value.rate_messages || 0);
    maxRateBytes = Math.max(maxRateBytes, value.rate_bytes || 0);
    this.attachment = structuredClone(value);
  }
  deserializeAttachment() { return structuredClone(this.attachment); }
  notify(error) {
    if (this.notified || !this.context) return;
    this.notified = true;
    this.context.event(async () => {
      try {
        if (error) await this.context.room.webSocketError(this);
        else await this.context.room.webSocketClose(this);
      } finally { this.context.retire(this); }
    });
  }
  attach(socket, initial) {
    this.network = socket; connections.add(socket); socket.setNoDelay(true);
    let buffer = Buffer.alloc(0), fragments = [], fragmentSize = 0, fragmentOp = null;
    const accept = chunk => {
      try {
        buffer = Buffer.concat([buffer, chunk]);
        if (buffer.length > 262144) throw new Error("fixture buffer limit");
        while (buffer.length >= 2 && this.readyState === 1) {
          const a = buffer[0], b = buffer[1], final = !!(a & 128), opcode = a & 15;
          if ((a & 112) || !(b & 128) || ![0, 1, 2, 8, 9, 10].includes(opcode)) throw new Error("invalid client frame");
          let size = b & 127, at = 2;
          if (size === 126) { if (buffer.length < 4) return; size = buffer.readUInt16BE(2); at = 4; if (size < 126) throw new Error("bad length"); }
          else if (size === 127) { if (buffer.length < 10) return; const wide = buffer.readBigUInt64BE(2); if (wide > 65536n || wide < 65536n) throw new Error("bad length"); size = Number(wide); at = 10; }
          if (size > 65536 || (opcode >= 8 && (!final || size > 125))) throw new Error("invalid frame size");
          if (buffer.length < at + 4 + size) return;
          const mask = buffer.subarray(at, at + 4), payload = Buffer.from(buffer.subarray(at + 4, at + 4 + size));
          buffer = buffer.subarray(at + 4 + size);
          for (let i = 0; i < payload.length; i++) payload[i] ^= mask[i & 3];
          if (opcode === 8) { this.close(1000, "Peer disconnected"); return; }
          if (opcode === 9) { this.write(encodeFrame(10, payload)); continue; }
          if (opcode === 10) continue;
          if ((opcode === 0 && fragmentOp === null) || (opcode !== 0 && fragmentOp !== null)) throw new Error("invalid fragmentation");
          if (opcode !== 0) fragmentOp = opcode;
          fragments.push(payload); fragmentSize += payload.length;
          if (fragmentSize > 65536) throw new Error("message too large");
          if (!final) continue;
          const message = Buffer.concat(fragments), type = fragmentOp;
          fragments = []; fragmentSize = 0; fragmentOp = null;
          const value = type === 1 ? decoder.decode(message) : message.buffer.slice(message.byteOffset, message.byteOffset + message.byteLength);
          this.context.event(() => this.context.room.webSocketMessage(this, value));
        }
      } catch { this.close(1002, "Fixture protocol error"); }
    };
    socket.on("data", accept);
    socket.on("close", () => { connections.delete(socket); this.readyState = 3; this.notify(false); });
    socket.on("error", error => {
      const code = typeof error.code === "string" && /^[A-Z0-9_]{1,80}$/.test(error.code) ? error.code : "OTHER";
      networkErrors[code] = (networkErrors[code] || 0) + 1;
      this.readyState = 3; this.notify(true);
    });
    for (const frame of this.pending) this.write(frame);
    this.pending = []; this.pendingBytes = 0;
    if (initial.length) accept(initial);
  }
}
globalThis.WebSocketPair = class {
  constructor() { const server = new Endpoint(); this[0] = { server }; this[1] = server; }
};

class Context {
  queue = Promise.resolve(); values = new Map(); sockets = []; tags = new Map(); alarm = null; room = null;
  storage = {
    get: async key => structuredClone(this.values.get(key)),
    put: async (key, value) => { this.values.set(key, structuredClone(value)); },
    deleteAll: async () => { this.values.clear(); },
    setAlarm: async at => {
      clearTimeout(this.alarm);
      this.alarm = setTimeout(() => this.event(() => this.room.alarm()), Math.max(1, at - Date.now()));
      this.alarm.unref();
    },
  };
  blockConcurrencyWhile(fn) { const result = this.queue.then(fn); this.queue = result.catch(() => {}); return result; }
  event(fn) {
    pendingWorkerEvents++;
    const result = this.queue.then(fn);
    this.queue = result.catch(() => { workerEventErrors++; }).finally(() => { pendingWorkerEvents--; });
    return this.queue;
  }
  acceptWebSocket(ws, tags) { ws.context = this; this.sockets.push(ws); this.tags.set(ws, tags); }
  retire(ws) { this.sockets = this.sockets.filter(candidate => candidate !== ws); this.tags.delete(ws); }
  getWebSockets(tag) { return this.sockets.filter(ws => !tag || this.tags.get(ws)?.includes(tag)); }
  getTags(ws) { return this.tags.get(ws) || []; }
}

const { default: worker, RoomRelay } = await import("../server/rooms/worker.mjs");
const rooms = new Map();
const env = { ROOMS_ENABLED: "true", CREATE_LIMITER: { limit: async () => ({ success: true }) },
  CREATE_GLOBAL_LIMITER: { limit: async () => ({ success: true }) }, CONNECT_LIMITER: { limit: async () => ({ success: true }) } };
env.ROOMS = { idFromName: code => code, get: code => {
  if (!rooms.has(code)) {
    if (rooms.size >= 16) throw new Error("fixture room limit");
    const ctx = new Context(); const room = new RoomRelay(ctx, env); ctx.room = room; rooms.set(code, { ctx, room });
  }
  return { fetch: request => rooms.get(code).room.fetch(request) };
} };
let cert, key, statsFile;
for (let i = 2; i < process.argv.length; i++) {
  if (process.argv[i] === "--tls-cert") cert = process.argv[++i];
  else if (process.argv[i] === "--tls-key") key = process.argv[++i];
  else if (process.argv[i] === "--stats-file") statsFile = process.argv[++i];
  else throw new Error("Unknown fixture option");
}
if (!!cert !== !!key) throw new Error("Both fixture TLS files are required");
const server = cert ? https.createServer({ cert: fs.readFileSync(cert), key: fs.readFileSync(key) }) : http.createServer();
function reportStats() {
  if (!statsFile) return;
  let retainedEndpoints = 0, activeEndpoints = 0, retainedTags = 0;
  for (const { ctx } of rooms.values()) {
    retainedEndpoints += ctx.sockets.length; retainedTags += ctx.tags.size;
    activeEndpoints += ctx.sockets.filter(ws => ws.readyState === 1).length;
  }
  const memory = process.memoryUsage();
  fs.writeFileSync(statsFile, JSON.stringify({ rooms: rooms.size, retained_endpoints: retainedEndpoints,
    active_endpoints: activeEndpoints, retained_tags: retainedTags,
    open_network_sockets: connections.size, worker_event_errors: workerEventErrors,
    pending_worker_events: pendingWorkerEvents, rss_bytes: memory.rss, heap_used_bytes: memory.heapUsed,
    max_rate_messages: maxRateMessages, max_rate_bytes: maxRateBytes, close_codes: closeCodes, close_reasons: closeReasons,
    network_errors: networkErrors }) + "\n");
}
const statsTimer = statsFile ? setInterval(reportStats, 250) : null;
statsTimer?.unref();
async function requestFor(req, body) {
  const headers = new Headers();
  for (const [name, value] of Object.entries(req.headers)) if (value !== undefined)
    headers.set(name, Array.isArray(value) ? value.join(", ") : value);
  // The outer service is loopback HTTP or test TLS; the ACTUAL Worker still
  // executes its production HTTPS-only and Origin/query checks unchanged.
  return new Request(`https://127.0.0.1:${server.address().port}${req.url}`, {
    method: req.method, headers, ...(body?.length ? { body } : {}),
  });
}
server.on("request", async (req, res) => {
  try {
    const pieces = []; let size = 0;
    for await (const chunk of req) { size += chunk.length; if (size > 1024) { res.writeHead(413).end(); return; } pieces.push(chunk); }
    const response = await worker.fetch(await requestFor(req, Buffer.concat(pieces)), env);
    res.writeHead(response.status, Object.fromEntries(response.headers)); res.end(Buffer.from(await response.arrayBuffer()));
  } catch { res.writeHead(503).end(); }
});
server.on("upgrade", async (req, socket, head) => {
  let accepted;
  const cancelled = () => accepted?.close(1001, "Upgrade cancelled");
  socket.once("close", cancelled); socket.once("error", cancelled);
  try {
    const nonce = req.headers["sec-websocket-key"];
    if (req.headers["sec-websocket-version"] !== "13" || typeof nonce !== "string" || !/^[A-Za-z0-9+/]{22}==$/.test(nonce)) { socket.destroy(); return; }
    const response = await worker.fetch(await requestFor(req), env);
    if (response.status !== 101) {
      const body = Buffer.from(await response.arrayBuffer());
      socket.end(`HTTP/1.1 ${response.status} Refused\r\nContent-Length: ${body.length}\r\nConnection: close\r\n\r\n` + body.toString()); return;
    }
    accepted = response.webSocket.server;
    if (socket.destroyed) { cancelled(); return; }
    const accept = createHash("sha1").update(nonce + "258EAFA5-E914-47DA-95CA-C5AB0DC85B11").digest("base64");
    socket.write("HTTP/1.1 101 Switching Protocols\r\nUpgrade: websocket\r\nConnection: Upgrade\r\nSec-WebSocket-Accept: " + accept + "\r\n\r\n");
    accepted.attach(socket, head);
    socket.removeListener("close", cancelled); socket.removeListener("error", cancelled);
  } catch { cancelled(); socket.destroy(); }
});
server.listen(0, "127.0.0.1", () => {
  console.log(JSON.stringify({ ready: true, url: `${cert ? "https" : "http"}://127.0.0.1:${server.address().port}`, fixture: "actual-worker-node-cloudflare-shim" }));
});
function stop() {
  if (statsTimer) clearInterval(statsTimer);
  for (const { ctx } of rooms.values()) clearTimeout(ctx.alarm);
  for (const connection of connections) connection.destroy();
  server.closeAllConnections(); server.close(() => { reportStats(); process.exit(0); });
  setTimeout(() => process.exit(0), 1000).unref();
}
process.on("SIGTERM", stop); process.on("SIGINT", stop);
