# Co-op Anything chaos tools

Unmodified MIT-licensed UDP relay, impairment model and profiles from the pinned upstream revision in UPSTREAM.json. Used only by developer network tests; not loaded by the game.

Our adapter is client/tests/udp_e2e.py: it forwards the reliable TCP connection alongside the impaired UDP channel on the same numeric port. No extra game process, runtime recording or global MCP setup is required.
