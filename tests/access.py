#!/usr/bin/env python3
"""Exercise production lwIP, browser console, protocol-independent relays and settings HTTP."""
# manager.py builds the harness and exercises manager login/SD lifecycle first.
from manager import *
from net_packets import Peer, pop

base_drain = drain


def drain(peer):
    for _ in range(20):
        queued = [
            p
            for p in other
            if p.get("dport") == peer.sport
            and p.get("sport") == peer.dport
            and p.get("iface") == peer.iface
            and not p.get("flags", 0) & 4
        ]
        for p in queued:
            other.remove(p)
            peer.receive(p)
        base_drain(peer)
        if not queued:
            break


def connect_hu(remote):
    syns = [p for p in other if p.get("flags") == 2 and p.get("dport") == remote]
    assert syns, other
    p = syns[-1]
    other.clear()
    h = Peer(1, p["dst"], p["src"], p["dport"], p["sport"], 9000)
    h.ack = p["seq"] + 1
    h.send(flags=0x12)
    drain(h)
    return h


def request_console(a, session="", data=""):
    return http(
        "/api/console",
        {"X-MST-Action": a, "X-MST-Session": str(session), "X-MST-Data": data},
        "POST",
    )


def output(cursor=0):
    code, body = http("/api/console", {"X-MST-Cursor": str(cursor)})
    assert code == 200, body
    return json.loads(body)


# Stock HTTP/1.0 downloaders may omit Host on USB. Content-addressed runner
# delivery remains compatible, while USB management endpoints stay forbidden.
asset_hash = hashlib.sha256((ROOT / "assets/sd-runner.so").read_bytes()).hexdigest()
for idx, endpoint in enumerate(["/payload/sd-runner-" + asset_hash[:16] + ".so", "/api/settings"]):
    c = Peer(1, "172.16.250.248", "172.16.250.1", 47900 + idx, 80)
    c.connect()
    c.send(("GET " + endpoint + " HTTP/1.0\r\n\r\n").encode())
    c.send(flags=0x11)
    drain(c)
    assert c.fin
    header, body = c.data.split(b"\r\n\r\n", 1)
    if idx == 0:
        assert b"200 OK" in header and hashlib.sha256(body).hexdigest() == asset_hash
    else:
        assert b"403 Forbidden" in header
# Host and Origin checks also guard against an unrelated website rebinding DNS.
assert http("/api/settings", {"Host": "attacker.example"})[0] == 403
assert http("/api/settings", {"Origin": "https://attacker.example"})[0] == 403
code, body = http("/api/settings")
s = json.loads(body)
assert code == 200 and s["ssid"] == "MST-Link"
assert s["forwards"] == [[2323, 23], [2222, 22], [8080, 80]] and s["listeners"] == 3
assert "password" not in s
assert request_console("open")[0] == 200
other.extend(pop())
h = connect_hu(23)
# Every byte of IAC negotiation may arrive in a separate packet.
for value in b"\xff\xfb\x01\xff\xfd\x18login: ":
    h.send(bytes([value]))
    drain(h)
o = output()
sid = o["session"]
assert o["ready"] and bytes.fromhex(o["data"]) == b"login: ", o
assert b"\xff\xfd\x01" in h.data and b"\xff\xfc\x18" in h.data
assert (
    action("connect", **{"X-HU-User": "testroot", "X-HU-Password": "test-only-password"})[0] == 409
)
assert request_console("open")[0] == 409
assert request_console("send", sid + 1, "41")[0] == 409
assert request_console("send", sid, "gg")[0] == 409
assert request_console("send", sid, "ff410d0a")[0] == 200
drain(h)
assert h.data.endswith(b"\xff\xffA\r\n"), h.data
# Large output is bounded; stale readers get an explicit loss indicator.
for _ in range(12):
    h.send(b"Z" * 1000)
    drain(h)
o = output()
assert o["lost"] and len(bytes.fromhex(o["data"])) == 1024
cursor = o["next"]
while cursor < 12007:
    o = output(cursor)
    cursor = o["next"]
assert request_console("close", sid)[0] == 200
other.clear()
assert not output()["connected"] and output()["data"] == ""

# Three arbitrary simultaneous TCP streams; byte transparency and half-close.
clients = []
heads = []
for index, (local, remote) in enumerate([(2323, 23), (2222, 22), (8080, 80)]):
    c = Peer(0, "192.168.4.16", "192.168.4.1", 48000 + index, local)
    c.connect()
    other.extend(pop())
    h = connect_hu(remote)
    clients.append(c)
    heads.append(h)
    data = bytes(range(256)) * 3
    h.send(data)
    drain(c)
    c.send(data)
    drain(h)
    assert c.data == data and h.data == data
assert request_console("open")[0] == 409
# Hold receiver ACKs to fill the relay's outbound window, then release it.
c, h = clients[2], heads[2]
baseline = len(c.data)
payload = bytes(range(250)) * 24
for offset in range(0, len(payload), 1000):
    h.send(payload[offset : offset + 1000])
    drain(h)
assert any(p.get("dport") == c.sport and p.get("data") for p in other)
for _ in range(20):
    drain(c)
    drain(h)
    lib.test_tick(250)
    other.extend(pop())
    if len(c.data) - baseline == len(payload):
        break
assert c.data[baseline:] == payload, (
    "Relay data under send-window backpressure",
    len(c.data) - baseline,
    len(payload),
    c.data[baseline:] == payload[: len(c.data) - baseline],
    [(p.get("iface"), p.get("dport"), p.get("flags"), len(p.get("data", b""))) for p in other],
)
# Fourth client is refused without disturbing existing streams.
c = Peer(0, "192.168.4.16", "192.168.4.1", 48010, 8080)
c.send(flags=2)
ps = pop()
c.receive(ps[0])
assert any(p.get("flags", 0) & 4 for p in pop())
for c, h in zip(clients, heads):
    c.send(flags=0x11)
    drain(h)
    assert h.fin and not c.fin
    h.send(b"last bytes")
    drain(c)
    assert c.data.endswith(b"last bytes")
    h.send(flags=0x11)
    drain(c)
    assert c.fin
assert lib.forward_active() == 0
# An unknown service refusal drops both legs and recovers its slot.
c = Peer(0, "192.168.4.16", "192.168.4.1", 48020, 2222)
c.connect()
ps = pop()
p = ps[0]
h = Peer(1, p["dst"], p["src"], 22, p["sport"], 1000)
h.ack = p["seq"] + 1
h.send(flags=0x14)
assert any(p.get("flags", 0) & 4 and p.get("dport") == c.sport for p in pop())
assert lib.forward_active() == 0
# USB loss closes both browser and native sessions.
assert request_console("open")[0] == 200
other.extend(pop())
h = connect_hu(23)
lib.test_up(0)
pop()
assert not output()["connected"]
settings_headers = {
    "X-MST-SSID": 'My "MST" link',
    "X-MST-Password": "easynew1",
    "X-MST-Forward1": "1234:4321",
    "X-MST-Forward2": "0:0",
    "X-MST-Forward3": "8443:443",
}
for key, value in [
    ("X-MST-Password", "short"),
    ("X-MST-Forward2", "80:23"),
    ("X-MST-Forward2", "1234:23"),
    ("X-MST-Forward1", "65536:23"),
    ("X-MST-Forward1", "1:0"),
    ("X-MST-SSID", ""),
    ("X-MST-SSID", "x" * 33),
    ("X-MST-SSID", "ok\r\nX-MST-SSID: duplicate"),
]:
    assert http("/api/settings", {**settings_headers, key: value}, "POST")[0] == 409, (key, value)
assert http("/api/settings", settings_headers, "POST")[0] == 200
code, body = http("/api/settings")
s = json.loads(body)
assert s["ssid"] == 'My "MST" link' and s["forwards"][0] == [1234, 4321]
assert b"easynew1" not in body
assert http("/api/settings", {**settings_headers, "X-MST-Password": ""}, "POST")[0] == 200
print(
    "PASS: console negotiation/overflow/session isolation, three TCP services, half-close/refusal/USB loss, settings validation and HTTP origin checks"
)
