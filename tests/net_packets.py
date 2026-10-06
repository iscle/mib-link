"""Packet helpers shared by Pico service and automatic-deployment tests."""
import ctypes as C
import socket
import struct
lib = None
def checksum(b):
    if len(b)%2:b+=b'\0'
    n=sum(struct.unpack('!%dH'%(len(b)//2),b));n=(n&65535)+(n>>16);n=(n&65535)+(n>>16)
    return (~n)&65535
def ip_packet(src,dst,proto,data):
    h=struct.pack('!BBHHHBBH4s4s',0x45,0,20+len(data),1,0,64,proto,0,socket.inet_aton(src),socket.inet_aton(dst))
    return h[:10]+struct.pack('!H',checksum(h))+h[12:]+data
def tcp(src,dst,sport,dport,seq,ack,flags,data=b''):
    h=struct.pack('!HHIIBBHHH',sport,dport,seq,ack,0x50,flags,8192,0,0)
    pseudo=socket.inet_aton(src)+socket.inet_aton(dst)+struct.pack('!BBH',0,6,len(h)+len(data))
    h=h[:16]+struct.pack('!H',checksum(pseudo+h+data))+h[18:]
    return ip_packet(src,dst,6,h+data)
def inject(iface,p):lib.test_inject(iface,p,len(p))
def pop():
    result=[];b=C.create_string_buffer(1600);iface=C.c_uint()
    while True:
        n=lib.test_pop(b,C.byref(iface))
        if not n:break
        p=b.raw[:n];off=(p[0]&15)*4
        if p[9]==6:
            sport,dport,seq,ack,offset,flags,window,cs,urg=struct.unpack('!HHIIBBHHH',p[off:off+20])
            result.append(dict(iface=iface.value,src=socket.inet_ntoa(p[12:16]),dst=socket.inet_ntoa(p[16:20]),sport=sport,dport=dport,seq=seq,ack=ack,flags=flags,data=p[off+(offset>>4)*4:]))
        else:result.append(dict(iface=iface.value,raw=p))
    return result
class Peer:
    def __init__(self,iface,src,dst,sport,dport,seq=100):
        self.iface,self.src,self.dst,self.sport,self.dport,self.seq=iface,src,dst,sport,dport,seq
        self.ack=0;self.data=b'';self.fin=False
    def send(self,data=b'',flags=0x18):
        inject(self.iface,tcp(self.src,self.dst,self.sport,self.dport,self.seq,self.ack,flags,data))
        self.seq+=len(data)+bool(flags&2)+bool(flags&1)
    def receive(self,p):
        assert p['dport']==self.sport and p['sport']==self.dport
        assert not p['flags']&4, 'Unexpected TCP reset'
        if p['flags']&2:self.ack=p['seq']+1
        if p['data']:
            assert p['seq']==self.ack,(p,self.ack)
            self.data+=p['data'];self.ack+=len(p['data'])
        if p['flags']&1:self.ack+=1;self.fin=True
        if p['flags']&3 or p['data']:self.send(flags=0x10)
    def connect(self):
        self.send(flags=2);ps=pop();assert len(ps)==1 and ps[0]['flags']&0x12==0x12,(self.iface,self.sport,ps);self.receive(ps[0])
def drain(*peers):
    for _ in range(60):
        ps=pop()
        if not ps:return
        for p in ps:
            peer=next((c for c in peers if c.sport==p.get('dport') and c.dport==p.get('sport') and c.iface==p['iface']),None)
            assert peer,p
            peer.receive(p)
    raise AssertionError('Packet loop')
