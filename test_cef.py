#!/usr/bin/env python3
import socket
import json
import time

sock = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
sock.connect(("127.0.0.1", 4777))
sock.settimeout(15)
TOKEN = "helper-smoke"

def send(msg):
    sock.send((json.dumps(msg) + "\n").encode())
    print(f"sent: {msg}")

def recv():
    buf = b""
    while True:
        c = sock.recv(1)
        if not c or c == b"\n":
            break
        buf += c
    if buf:
        msg = json.loads(buf)
        data_field = msg.get("data", "")
        if len(data_field) > 100:
            display = dict(msg)
            display["data"] = data_field[:100] + "...(truncated)"
            print(f"recv: {display}")
        else:
            print(f"recv: {msg}")
        return msg
    return None

send({"type": "handshake", "token": TOKEN})
recv()
send({"type": "initBrowser", "token": TOKEN, "id": "test1", "url": "data:text/html,<h1>Hello CEF</h1>", "width": 800, "height": 600})
resp = recv()
print(f"initBrowser response: {resp}")
print("Waiting for frames...")
start = time.time()
frames = 0
while time.time() - start < 10 and frames < 3:
    try:
        msg = recv()
        if msg and msg.get("type") == "frameReady":
            frames += 1
            w = msg.get("width")
            h = msg.get("height")
            dlen = len(msg.get("data", ""))
            print(f"Frame {frames}: {w}x{h}, data len: {dlen}")
    except socket.timeout:
        print("Timeout")
        break
print(f"Total frames: {frames}")
send({"type": "disposeBrowser", "token": TOKEN, "id": "test1"})
sock.close()
print("Done")
