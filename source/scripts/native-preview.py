#!/usr/bin/env python3
"""Local browser preview of this project's active headless game, without another game instance."""
import argparse
from http.server import BaseHTTPRequestHandler, HTTPServer
import json
import os
from pathlib import Path
import re
import threading
import time

ROOT = Path(__file__).resolve().parents[1]
FRAME = re.compile(rb"\[sst\] frame=(\d+)")
LAST_IMAGE = ROOT / "build/native-preview-last.png"
HTML = b'''<!doctype html><html lang="en"><meta charset="utf-8">
<meta name="viewport" content="width=device-width,initial-scale=1"><title>Skyward Sword test preview</title>
<style>body{margin:0;background:#101713;color:#e9f1e9;font:16px system-ui}
main{max-width:1280px;margin:24px auto;padding:0 16px}h1{font-size:22px}p{color:#bccbbf}
canvas{width:100%;display:block;background:#000;border-radius:8px;aspect-ratio:16/9}
span{color:#adce75}</style><main><h1>Skyward Sword test preview</h1>
<p id="status">Waiting for a test capture...</p><canvas id="view" role="img" aria-label="Current game test view"></canvas>
<p>View only. Refreshes about every two seconds while a headless test runs. The last image stays
visible while the game is stopped for builds. This uses the existing test game.</p></main>
<script>let stamp=0;const canvas=document.querySelector('#view'),ctx=canvas.getContext('2d',{alpha:false});
async function refresh(){try{let s=await(await fetch('/status.json',
{cache:'no-store'})).json();document.querySelector('#status').textContent=s.active?
'Live test running'+(s.updated?' - image '+Math.round((Date.now()/1000-s.updated))+' seconds old':' - preparing first capture'):
'Test stopped - showing the last capture';if(s.updated&&s.updated!==stamp){
let bitmap=await createImageBitmap(await(await fetch('/frame.png',{cache:'no-store'})).blob());
if(canvas.width!==bitmap.width||canvas.height!==bitmap.height){canvas.width=bitmap.width;canvas.height=bitmap.height}
ctx.drawImage(bitmap,0,0);bitmap.close();stamp=s.updated}}catch(e){document.querySelector('#status').textContent=
'Preview disconnected'}setTimeout(refresh,1000)}refresh()</script></html>'''


def active_test():
    expected = str(ROOT / "engine/build/skyrecomp")
    for proc in Path("/proc").iterdir():
        if not proc.name.isdigit():
            continue
        try:
            if (proc / "comm").read_text().strip() != "skyrecomp":
                continue
            if os.readlink(proc / "exe") != expected:
                continue
            env = dict(entry.split(b"=", 1) for entry in (proc / "environ").read_bytes().split(b"\0") if b"=" in entry)
            if env.get(b"LARECOMP_NO_USER_INPUT") != b"1":
                continue
            data = Path(os.fsdecode(env[b"LARECOMP_DATA_ROOT"]))
            inputs = Path(os.fsdecode(env[b"LARECOMP_INPUT_FILE"])) if env.get(b"LARECOMP_INPUT_FILE") else None
            request = Path(os.fsdecode(env[b"SST_PREVIEW_FILE"])) if env.get(b"SST_PREVIEW_FILE") else None
            if inputs or request:
                return int(proc.name), data, inputs, request
        except (OSError, KeyError, ValueError):
            continue
    return None


class Preview:
    def __init__(self):
        self.lock = threading.Lock()
        self.stop = threading.Event()
        self.png = b""
        self.active = False
        self.updated = 0
        self.viewed = 0
        if LAST_IMAGE.exists():
            self.png, self.updated = LAST_IMAGE.read_bytes(), LAST_IMAGE.stat().st_mtime
        self.thread = threading.Thread(target=self.capture, daemon=True)

    def capture(self):
        previous = None
        previous_mtime = 0
        requested = 0
        while not self.stop.wait(.25):
            with self.lock:
                watching = time.monotonic() - self.viewed < 5
            if not watching:
                continue  # Avoid screenshot readbacks/PNG work when no browser is watching.
            test = active_test()
            with self.lock:
                self.active = test is not None
            if not test:
                continue
            pid, data, inputs, request = test
            if pid != previous:
                previous, previous_mtime, requested = pid, 0, 0
            shot = data / "build/sst-live-preview.png"
            try:
                mtime = shot.stat().st_mtime_ns
                if mtime != previous_mtime:
                    png = shot.read_bytes()
                    if png.startswith(b"\x89PNG\r\n\x1a\n") and png.endswith(b"IEND\xaeB`\x82"):
                        with self.lock:
                            self.png, self.updated = png, shot.stat().st_mtime
                        previous_mtime = mtime
            except OSError:
                pass
            if time.monotonic() - requested < 2:
                continue
            try:
                if request:
                    request.touch()
                    requested = time.monotonic()
                    continue
                # Compatibility with a test started before the preview request hook was built.
                # Do not grow the engine's bounded 16 KiB input script indefinitely.
                if inputs.stat().st_size > 12000:
                    continue
                with (data / "skyrecomp.log").open("rb") as log:
                    log.seek(0, 2)
                    log.seek(max(0, log.tell() - 131072))
                    frames = FRAME.findall(log.read())
                if frames:
                    # Trace can be a second old. Schedule just ahead, keeping all existing inputs.
                    frame = int(frames[-1]) + 75
                    with inputs.open("a") as stream:
                        stream.write(f",{frame}:shot:sst-live-preview")
                    requested = time.monotonic()
            except OSError:
                continue


def handler(preview):
    class Handler(BaseHTTPRequestHandler):
        def do_GET(self):
            path = self.path.split("?", 1)[0]
            with preview.lock:
                if path in ["/", "/status.json"]:
                    preview.viewed = time.monotonic()
                status = dict(active=preview.active, updated=preview.updated)
                png = preview.png
            if path == "/":
                body, content = HTML, "text/html; charset=utf-8"
            elif path == "/status.json":
                body, content = json.dumps(status).encode(), "application/json"
            elif path == "/frame.png" and png:
                body, content = png, "image/png"
            else:
                self.send_error(404)
                return
            self.send_response(200)
            self.send_header("Content-Type", content)
            self.send_header("Content-Length", str(len(body)))
            self.send_header("Cache-Control", "no-store")
            self.end_headers()
            try:
                self.wfile.write(body)
            except (BrokenPipeError, ConnectionResetError):
                pass

        def log_message(self, *_args):
            pass
    return Handler


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--port", type=int, default=8765)
    args = parser.parse_args()
    if not 1 <= args.port <= 65535:
        parser.error("port must be 1-65535")
    preview = Preview()
    with HTTPServer(("127.0.0.1", args.port), handler(preview)) as server:
        preview.thread.start()
        print(f"Game preview: http://127.0.0.1:{args.port}", flush=True)
        try:
            server.serve_forever()
        except KeyboardInterrupt:
            pass
        finally:
            preview.stop.set()
            preview.thread.join(timeout=2)
            if preview.png:
                LAST_IMAGE.parent.mkdir(parents=True, exist_ok=True)
                LAST_IMAGE.write_bytes(preview.png)
                os.utime(LAST_IMAGE,(preview.updated,preview.updated))


if __name__ == "__main__":
    main()
