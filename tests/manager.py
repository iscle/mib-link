#!/usr/bin/env python3
"""Keyless Wi-Fi HTTP, fragmented stock login and SD management state tests."""
import ctypes as C
import hashlib
import json
from pathlib import Path
import re
import subprocess
import sys
import os

embedded = False
ROOT = Path(__file__).resolve().parents[1]
LWIP = Path(os.environ.get("PICO_SDK_PATH", ROOT / "external/pico-sdk")) / "lib/lwip/src"
subprocess.run(
    [
        "cc",
        "-shared",
        "-fPIC",
        "-g",
        "-O1",
        "-DMIB_HOST_TEST=1",
        "-Itests/host",
        "-Igenerated",
        "-Isrc",
        "-Ivendor",
        "-I" + str(LWIP / "include"),
        "tests/host/network.c",
        "src/services.c",
        "src/settings.c",
        "src/forward.c",
        "src/console.c",
        "src/manager.c",
        "generated/payload.c",
        "vendor/dhcpserver.c",
        str(LWIP / "netif/ethernet.c"),
        *map(str, (LWIP / "core").glob("*.c")),
        *map(str, (LWIP / "core/ipv4").glob("*.c")),
        "-o",
        "build/tests/manager-network.dylib",
    ],
    cwd=ROOT,
    check=True,
)
lib = C.CDLL(str(ROOT / "build/tests/manager-network.dylib"))
lib.test_inject.argtypes = [C.c_uint, C.c_void_p, C.c_uint]
lib.test_pop.argtypes = [C.c_void_p, C.POINTER(C.c_uint)]
lib.manager_request.argtypes = [C.c_char_p] * 5
lib.manager_request.restype = C.c_bool
lib.manager_status.restype = C.c_char_p
import net_packets as net

net.lib = lib
lib.test_init()
port = 45000
other = []


def drain(peer):
    for _ in range(150):
        ps = net.pop()
        if not ps:
            return
        for p in ps:
            if p.get("dport") == peer.sport and p.get("sport") == peer.dport and not p["flags"] & 4:
                peer.receive(p)
            else:
                other.append(p)
    raise AssertionError("Packet loop")


def http(path="/manage/status", headers=None, method="GET", key=None):
    global port
    port += 1
    c = net.Peer(0, "192.168.4.16", "192.168.4.1", port, 80)
    c.connect()
    h = {
        "Host": "192.168.4.1",
        **({"X-MHI2-Key": key} if key is not None else {}),
        **(headers or {}),
    }
    request = (
        method
        + " "
        + path
        + " HTTP/1.1\r\n"
        + "".join(k + ": " + v + "\r\n" for k, v in h.items())
        + "\r\n"
    ).encode()
    for offset in range(0, len(request), 13):
        c.send(request[offset : offset + 13])
        drain(c)
    assert c.fin, c.data[:100]
    head, body = c.data.split(b"\r\n\r\n", 1)
    assert int(re.search(rb"Content-Length: (\d+)", head)[1]) == len(body)
    c.send(flags=0x11)
    drain(c)
    return int(head.split()[1]), body


def state():
    code, body = http()
    assert code == 200
    return json.loads(body)


def action(a, **h):
    return http("/manage/action", {"X-MHI2-Action": a, **h}, "POST")


assert http(key=None)[0] == 200
assert http(key="obsolete-key")[0] == 200
assert http("/manage/action", {"X-MHI2-Action": "scan\r\nX-MHI2-Action: stop"}, "POST")[0] == 409
assert http("/manage/action", {}, "POST")[0] == 409
assert b"MIB-Link" in http("/", key=None)[1]
assert b"test-management-key" not in http("/", key=None)[1]
assert b"test-only-password" not in http("/", key=None)[1]
assert b"id='key'" not in http("/", key=None)[1]
assert state()["credentials_ready"] == embedded
assert action("run")[0] == 409
lib.test_up(1)
other.extend(net.pop())
if embedded:
    assert action("connect")[0] == 200
else:
    assert action("connect")[0] == 409
    assert (
        action("connect", **{"X-HU-User": "testroot", "X-HU-Password": "test-only-password"})[0]
        == 200
    )
lib.test_tick(1)
other.extend(net.pop())
last_peer = None


def login(rejected=False):
    global last_peer
    syns = [p for p in other if p.get("flags") == 2 and p.get("dport") == 23]
    if not syns:
        assert last_peer is not None, other
        peer = last_peer
        peer.data = b""
        for p in other:
            if p.get("dport") == peer.sport and p.get("sport") == peer.dport and not p["flags"] & 4:
                peer.receive(p)
        other.clear()
        drain(peer)

        def send(data):
            for i in range(0, len(data), 7):
                peer.send(data[i : i + 7])
                drain(peer)

        assert b"PICOSD_ACTION=" in peer.data, peer.data
        assert b"test-only-password" not in peer.data
        return peer, send
    syn = syns[-1]
    other.clear()
    peer = net.Peer(1, "172.16.250.248", "172.16.250.1", 23, syn["sport"], seq=9000)
    last_peer = peer
    peer.ack = syn["seq"] + 1
    peer.send(flags=0x12)
    drain(peer)

    def send(data):
        for i in range(0, len(data), 7):
            peer.send(data[i : i + 7])
            drain(peer)

    send(b"\xff\xfb\x01\xff\xfd\x03\xff\xfd\x18\xff\xfa\x18\x01\xff\xf0login: ")
    assert peer.data.count(b"testroot\r\n") == 1
    send(b"\r\nPassword:")
    assert peer.data.count(b"test-only-password\r\n") == 1
    if rejected:
        send(b"\r\nLogin incorrect\r\nlogin: ")
        return peer, send
    send(b"\r\n# ")
    assert b"PICOSD_ACTION=" in peer.data and b"http://172.16.250.1/payload/sd-runner-" in peer.data
    assert b"PICO_MOD_RESULT" not in peer.data
    return peer, send


peer, send = login()
digest = "a" * 64


def finish(peer, send, records):
    send(records)
    mark = re.search(rb"pico_sd_mark=(PICOSD_DONE_\d+_);", peer.data)[1]
    send(mark + b"0\r\n")


finish(
    peer,
    send,
    (
        "PICOSD_FW "
        + "b" * 64
        + "\r\nPICOSD_CARD sda0 "
        + digest
        + " Demo compatible\r\nPICOSD_COUNT 1\r\nPICOSD_STATE inactive\r\n"
    ).encode(),
)
s = state()
assert s["authenticated"] and not s["busy"] and s["cards"][0]["digest"] == digest, s
assert b"test-only-password" not in json.dumps(s).encode()
assert action("run", **{"X-MHI2-Slot": "sda0;reboot", "X-MHI2-Digest": digest})[0] == 409
assert action("run", **{"X-MHI2-Slot": "sda0", "X-MHI2-Digest": "c" * 64})[0] == 409
assert action("run", **{"X-MHI2-Slot": "sda0", "X-MHI2-Digest": digest})[0] == 200
lib.test_tick(1)
other.extend(net.pop())
peer, send = login()
assert b"PICOSD_ACTION=run PICOSD_SLOT=sda0" in peer.data
finish(
    peer,
    send,
    (
        "PICOSD_STATE active\r\nPICOSD_ACTIVE "
        + digest
        + "\r\nPICOSD_LOG "
        + b'hello\n"quoted"'.hex()
        + "\r\n"
    ).encode(),
)
s = state()
assert s["hu_state"] == "active" and s["log"] == 'hello\n"quoted"', s
assert s["live_connected"] and not s["busy"]
# Live status uses the existing authenticated session, not a second login.
lib.test_tick(2000)
other.extend(net.pop())
peer, send = login()
assert b"PICOSD_ACTION=status" in peer.data
telemetry = b"PICOSD_PROGRESS v1 copying mmx-emmc 4096 1024 1024 8192 1 3072 7168 2000 1\r\n"
finish(peer, send, b"PICOSD_STATE active\r\n" + telemetry)
s = state()
assert s["progress"]["copied"] == 4096 and s["progress"]["verified"] == 1024, s
lib.test_tick(2000)
other.extend(net.pop())
peer, send = login()
# Stop is accepted while a background status poll is in flight, then serialized.
assert action("stop")[0] == 200 and state()["stop_pending"]
finish(peer, send, b"PICOSD_STATE active\r\n" + telemetry)
assert state()["progress"]["heartbeat_age_ms"] >= 2000
lib.test_tick(1)
other.extend(net.pop())
peer, send = login()
assert b"PICOSD_ACTION=stop" in peer.data
finish(
    peer,
    send,
    b"PICOSD_STATE inactive\r\nPICOSD_PROGRESS v1 cancelled mmx-emmc 4096 1024 1024 8192 1 3072 7168 4000 2\r\n",
)
assert not state()["live_connected"] and state()["progress"]["phase"] == "cancelled"
# A forged/invalid completion record cannot turn partial bytes into success.
assert action("status")[0] == 200
lib.test_tick(1)
other.extend(net.pop())
peer, send = login()
finish(
    peer,
    send,
    b"PICOSD_STATE inactive\r\nPICOSD_PROGRESS v1 complete mmx-emmc 4096 1024 1024 8192 1 3072 7168 5000 3\r\n",
)
assert state()["progress"] is None

assert action("stop")[0] == 200
lib.test_tick(1)
other.extend(net.pop())
peer, send = login()
assert b"PICOSD_ACTION=stop" in peer.data
finish(peer, send, b"PICOSD_STATE inactive\r\n")
assert state()["hu_state"] == "inactive"
# Verify a maximum-length log streams correctly with TCP window backpressure.
assert action("status")[0] == 200
lib.test_tick(1)
other.extend(net.pop())
peer, send = login()
finish(
    peer,
    send,
    b"PICOSD_STATE inactive\r\n" + (b"PICOSD_LOG " + (b'"' * 64).hex().encode() + b"\r\n") * 32,
)
assert state()["log"] == '"' * 2048
# Manual access also disarms auto-run when the previous scan closed its shell.
assert action("auto-on", **{"X-MHI2-Slot": "sda0", "X-MHI2-Digest": digest})[0] == 200
lib.manager_poll(1, 1)
assert not state()["autorun"]
lib.manager_poll(1, 0)
# Auto-run is opt-in for the observed digest, never an arbitrary new card.
assert action("auto-on", **{"X-MHI2-Slot": "sda0", "X-MHI2-Digest": digest})[0] == 200
lib.test_tick(1)
other.extend(net.pop())
peer, send = login()
assert b"PICOSD_ACTION=run" in peer.data
finish(peer, send, b"PICOSD_STATE active\r\n")
lib.test_tick(10000)
other.extend(net.pop())
peer, send = login()
assert b"PICOSD_ACTION=scan" in peer.data
finish(peer, send, b"PICOSD_COUNT 0\r\nPICOSD_STATE inactive\r\n")
lib.test_tick(10000)
other.extend(net.pop())
peer, send = login()
finish(
    peer,
    send,
    ("PICOSD_CARD sda0 " + "c" * 64 + " Changed compatible\r\nPICOSD_STATE inactive\r\n").encode(),
)
lib.test_tick(10000)
other.extend(net.pop())
peer, send = login()
assert b"PICOSD_ACTION=scan" in peer.data, "Auto-ran an untrusted version"
finish(
    peer,
    send,
    ("PICOSD_CARD sda0 " + digest + " Demo compatible\r\nPICOSD_STATE inactive\r\n").encode(),
)
lib.test_tick(10000)
other.extend(net.pop())
peer, send = login()
assert b"PICOSD_ACTION=run" in peer.data
mark = re.search(rb"pico_sd_mark=(PICOSD_DONE_\d+_);", peer.data)[1]
send(b"PICOSD_ERROR start-failed-cleanup-complete\r\nPICOSD_STATE inactive\r\n" + mark + b"1\r\n")
assert not state()["autorun"]
other.clear()
lib.test_tick(30000)
net.pop()
assert not lib.manager_busy(), "Failed auto-run retried"
assert action("disconnect")[0] == 200
assert not state()["authenticated"] and action("scan")[0] == 409
assert not state()["credentials_ready"] and action("connect")[0] == 409
assert (
    action("connect", **{"X-HU-User": "testroot", "X-HU-Password": "test-only-password"})[0] == 200
)
lib.test_tick(1)
other.extend(net.pop())
peer, send = login(rejected=True)
assert not state()["authenticated"] and action("scan")[0] == 409
other.clear()
lib.test_tick(30000)
net.pop()
assert not lib.manager_busy(), "Rejected authentication retried"
print(
    "PASS: management HTTP, fragmented login, state, SD actions, progress, autorun and rejected authentication"
)
