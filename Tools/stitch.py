#!/usr/bin/env python3
"""stitch.py out.png in1.png in2.png ...  -> the inputs side by side (same height), with a small gap. Stdlib only."""
import zlib, struct, sys
def read_png(p):
    d=open(p,'rb').read(); assert d[:8]==b'\x89PNG\r\n\x1a\n', p
    i=8; idat=b''
    while i<len(d):
        l=struct.unpack('>I',d[i:i+4])[0]; t=d[i+4:i+8]; c=d[i+8:i+8+l]; i+=12+l
        if t==b'IHDR': w,h,bd,ct,_,_,il=struct.unpack('>IIBBBBB',c)
        elif t==b'IDAT': idat+=c
    assert bd==8 and il==0, (p,bd,il)
    ch={2:3,6:4}[ct]; raw=zlib.decompress(idat); st=w*ch; rows=[]; prev=bytearray(st); pos=0
    for y in range(h):
        f=raw[pos]; line=bytearray(raw[pos+1:pos+1+st]); pos+=1+st
        if f==1:
            for x in range(ch,st): line[x]=(line[x]+line[x-ch])&255
        elif f==2:
            for x in range(st): line[x]=(line[x]+prev[x])&255
        elif f==3:
            for x in range(st): line[x]=(line[x]+(((line[x-ch] if x>=ch else 0)+prev[x])>>1))&255
        elif f==4:
            for x in range(st):
                a=line[x-ch] if x>=ch else 0; b=prev[x]; c2=prev[x-ch] if x>=ch else 0
                pa=abs(b-c2); pb=abs(a-c2); pc=abs(a+b-2*c2)
                line[x]=(line[x]+(a if (pa<=pb and pa<=pc) else (b if pb<=pc else c2)))&255
        rows.append(line); prev=line
    if ch==4:
        # composite over mid grey so alpha is visible
        out=[]
        for line in rows:
            o=bytearray(w*3)
            for x in range(w):
                a=line[x*4+3]/255.0
                for k in range(3): o[x*3+k]=int(line[x*4+k]*a+ BG[k]*(1-a))
            out.append(bytes(o))
        return w,h,out
    return w,h,[bytes(r) for r in rows]
def write_png(p,w,h,rows):
    raw=b''.join(b'\x00'+r for r in rows)
    def chunk(t,c): return struct.pack('>I',len(c))+t+c+struct.pack('>I',zlib.crc32(t+c)&0xffffffff)
    open(p,'wb').write(b'\x89PNG\r\n\x1a\n'+chunk(b'IHDR',struct.pack('>IIBBBBB',w,h,8,2,0,0,0))+chunk(b'IDAT',zlib.compress(raw,6))+chunk(b'IEND',b''))
BG=(40,60,80)
if __name__=='__main__':
    out=sys.argv[1]; imgs=[read_png(p) for p in sys.argv[2:]]
    h=max(i[1] for i in imgs); gap=8; pad=bytes(BG)*gap
    rows=[]
    for y in range(h):
        parts=[]
        for n,(w,ih,r) in enumerate(imgs):
            parts.append(r[y] if y<ih else bytes(BG)*w)
            if n<len(imgs)-1: parts.append(pad)
        rows.append(b''.join(parts))
    W=sum(i[0] for i in imgs)+gap*(len(imgs)-1)
    write_png(out,W,h,rows); print(out,W,h)
