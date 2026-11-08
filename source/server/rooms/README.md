# SST opaque room relay — deployed testing service

This component transports the current native C host's TCP streams through a Cloudflare Worker and one SQLite Durable Object per room. The host PC runs the gameplay authority. The Worker does not implement SST gameplay, interpret protocol messages, or replace the native host's pickup, prop, progress or enemy rules.

The approved testing service is `https://sst-opaque-rooms.sst-epiccargo.workers.dev`. The user confirmed the Free plan and approved deployment on2026-10-30. No subscription, billing plan, existing website or custom DNS was changed. Launcher5 uses this address by default; friends need no Cloudflare accounts. Daily Free quotas and the six-hour room lifetime apply.

The checked-in `wrangler.toml` remains a disabled deployment template (`ROOMS_ENABLED = "false"`, `workers_dev = false`). The deployed configuration, account identifier and OAuth state stay outside source/public packages. Existing services must never be overwritten by a fresh setup. Deployment requires an account-specific configuration and explicit authorization.

## HTTP and connection contract

All public requests use HTTPS. Browser `Origin` headers and query parameters are rejected: v1 is a launcher/bridge transport, with no browser cross-origin API and no public room listing.

- `POST /api/rooms`: empty body or literal `{}`. Returns HTTP201 JSON: `version:1`, six-character `code`, `host_token` (32 random bytes as64 lowercase hex characters), `expires_at_ms`, absolute `host_url`/`join_url`, `max_guests:3`, `max_data_bytes:32768`, `credit_ack:true`, `envelope_bytes:8`.
- Codes use `ABCDEFGHJKLMNPQRSTUVWXYZ23456789`, exactly32 symbols;0/1/I/O are excluded. Codes are case-sensitive uppercase. A code is an invite, not a strong account identity.
- Creator connects `WSS /r/<CODE>/host` with `Authorization: Bearer <host_token>`. The host token never belongs in a URL. Only its SHA-256 digest is stored.
- Up to three guests connect `WSS /r/<CODE>/join` using the code. The fourth native game is the host PC's local loopback client. There is one host socket, no host replacement/resumption and no host-token recovery endpoint.
- The host must connect before guests. Host disconnect, host protocol error, host backpressure or room expiry closes every guest and revokes the room. Create a new room after host loss.
- Standard WebSocket protocol pings/pongs are handled by Cloudflare. They are not SST packets or application ACKs.

Nonupgrade requests return426. Errors are noncached JSON `{ "error": "stable_code" }`; examples include `rooms_disabled`503, `method_not_allowed`405, `host_auth_required`401, `room_not_found`404, `host_not_connected`409, `room_full`409, `host_already_connected`409 and `room_gone`410. Public rate checks can return429; unknown paths return404. Bodies over256 bytes are rejected before parsing. Room creation retries at most eight code collisions.

## Binary and credit framing

The host's logical payload is `op:u8 | channel:u32LE | body`. Channels start at1, increase monotonically for the life of the host connection and never recycle.

| Op | Direction | Body |
| --- | --- | --- |
|1 OPEN|Worker → host|Empty; exactly5 logical bytes. Create one local native-host TCP connection.|
|2 DATA|Both host directions|0–32768 opaque TCP bytes for exactly that channel.|
|3 CLOSE|Both host directions|`code:u16LE` plus0–96 UTF-8 bytes of reason. Close exactly that local connection.|

Host → Worker supports only DATA and CLOSE. Guest → Worker binary messages contain only raw TCP bytes, with a32768-byte cap. Guests cannot create channels or send multiplex headers as control messages. No TCP framing assumptions are made: split/coalesce chunks normally at the bridge.

**Every Worker → client binary message has an additional8-byte cryptographically random credit nonce before its logical payload.** The bridge strips this prefix before interpreting the host mux or forwarding guest bytes. After the corresponding local TCP operation succeeds, send the text control message `{"ack":"<16 lowercase hex nonce>"}`. ACK OPEN/CLOSE after processing them and DATA after local forwarding (or discarding a response for an already closed local channel). Client → Worker binary data has no nonce prefix. The maximum physical Worker → host binary message is32781 bytes; Worker → guest is32776 bytes.

The Worker tracks exact outstanding nonces, not caller-supplied byte totals. Unpredictable credits prevent a peer from freeing a guessed future queue entry without draining its incoming TCP stream. No payload is retained for retransmission. Unknown, repeated, malformed or numeric ACKs close that connection with1008. Other application text is also rejected. Allow at most256KiB and512 outstanding frames per socket; exceeding credit closes a guest with1013 or revokes the room if the host is congested. Credits are packed as nonce+length metadata in socket attachments, remaining below Cloudflare's16KiB attachment limit. No undocumented `bufferedAmount` property is assumed.

Each socket also allows at most512 application messages and1MiB received per one-second window. These limits include ACKs. Payload-size violations close with1009, invalid host operations with1002, transport faults with1011 and peer/room shutdown with1000/1001. Unknown old channel IDs are ignored rather than routed to a replacement guest.

## Lifecycle, storage and abuse boundaries

Room TTL is six hours from creation, not six hours of idle time. An alarm closes all sockets and deletes metadata. Before expiry, a revoked room retains only metadata with its auth digest erased, preventing resurrection by an old host token. The alarm is safe to retry. A request/message also checks expiry. Future random creation may reuse a fully expired code; old host credentials still fail.

Durable storage contains only room code, token digest, creation/expiry times, a random room-instance identifier and connection/revocation flags. WebSocket attachments contain role/channel, room instance, credit nonces/lengths and bounded transport-rate counters. Hibernation reconstructs connections from those attachments. Accepted socket tags also retain the room instance, so a delayed disconnect with a missing attachment from an expired room cannot revoke a later room reusing that code. Existing rooms without instance metadata retain their original lifecycle until expiry. **No gameplay bytes, saves, keys, assets, usernames, IP addresses or packet logs are written to application storage or console.** Cloudflare terminates TLS; this is not end-to-end encryption. Provider request metadata/telemetry is outside the payload policy; Worker Observability is disabled in the supplied configuration.

Guest admission owns its socket and channel setup until the upgrade response succeeds. A handled failure releases that socket and closes an already-announced backend channel. It returns503 inside the concurrency callback: allowing the exception to escape `blockConcurrencyWhile` would reset the entire object and disconnect the other players. Host setup failures still revoke the room. Initial durable metadata failures propagate so Cloudflare can reload potentially committed state; cleanup is secured by an alarm before publishing any metadata.

Creation and connection require rate-limiter bindings. Creation uses a hashed source-network key plus a shared creation key; connection attempts use a hashed source-network key. This anonymous v1 can unintentionally limit users sharing a NAT. Cloudflare limits are approximate and apply per location: the `CREATE_GLOBAL_LIMITER` binding is a shared key **per location**, not a strict worldwide budget. The illustrative rate-limit namespace IDs must be checked for account uniqueness before deployment. Distributed abuse and provider CPU/storage billing are not eliminated by six-character codes, TTL or these limits. Keep the service disabled until account-level controls, quotas and public exposure have been reviewed.

## Local mock verification

`npm test` uses only Node builtins, a tiny loader stub for `cloudflare:workers`, mocked WebSocketPair/storage/context and real local WebCrypto. There are no dependencies to install, no remote sockets and no account calls. Tests cover auth/roles/capacity, concurrency, opaque exact-channel routing, host loss, alarm expiry, hibernation, bounds and nonce-credit backpressure. The Node loader exists only under `test/`; production imports Cloudflare's actual DurableObject base class.

Verification also passed four real local workerd HTTPS/WSS scenarios with12 checks, and a deployed Cloudflare internet smoke with the native C host and lightweight peers. Internet checks covered HELLO/WELCOME/pose, byte-exact0x34, collector ACK0x33, late-join consumed snapshots and room/helper/lease cleanup. No full game or account credential is involved in the public smoke. Sustained/lossy connections, measured long-session usage and separate-PC play with the new relay still need testing. Launcher v1 establishes HOST/JOIN before game launch; live in-game connection IPC is future work.

Cloudflare-managed workers.dev rejects default Python urllib client identification with error1010. Both HTTP room creation and WebSocket upgrades identify this application as `SkywardSwordTogether/1`; TLS certificate verification remains enabled. See [Cloudflare's documented client-header guidance](https://developers.cloudflare.com/support/troubleshooting/http-status-codes/cloudflare-1xxx-errors/error-1010/).

## Cost review basis — not an account quote

Cloudflare's current Durable Objects documentation lists Free SQLite-backed availability,100000 requests/day and13000GB-s/day; Paid includes1million requests/month and400000GB-s/month, with excess billing and rounding. Incoming WebSocket messages are billed at20:1 for compute requests; application credit ACKs are incoming messages too. Outgoing WebSocket messages and protocol pings do not count as incoming application messages. Hibernation avoids duration charges only while eligible; continuous gameplay may keep the object active.

Illustration only:100 incoming data messages/second plus100 ACKs/second for six hours is4.32million application messages, approximately216000 metered DO requests before HTTP/alarm events. A worst-case continuously active128MB allocation for six hours is about2764.8GB-s. This does not predict actual SST traffic or the user's bill. Worker requests/CPU, plan minimums, storage rows, other account use, provider rounding and abuse also matter. Limits are not a guaranteed monetary cap. No plan or spending decision has been made.

Official sources consulted before implementing APIs:

- [Durable Objects WebSockets/hibernation and attachments](https://developers.cloudflare.com/durable-objects/best-practices/websockets/)
- [Durable Object base class and WebSocket event methods](https://developers.cloudflare.com/durable-objects/api/base/)
- [State acceptWebSocket/getWebSockets/getTags APIs](https://developers.cloudflare.com/durable-objects/api/state/)
- [Workers WebSocket methods and close behavior](https://developers.cloudflare.com/workers/runtime-apis/websockets/)
- [Alarm scheduling and retry behavior](https://developers.cloudflare.com/durable-objects/api/alarms/)
- [Rate-limiter binding syntax and locality limits](https://developers.cloudflare.com/workers/runtime-apis/bindings/rate-limit/)
- [Durable Objects pricing](https://developers.cloudflare.com/durable-objects/platform/pricing/)
