"""Summarise an Intel HEX for dsPIC (byte addr = 2*PC): ranges, word count, config words."""
import sys
def load(p):
    mem={}; base=0
    for ln in open(p):
        ln=ln.strip()
        if not ln.startswith(':'): continue
        b=bytes.fromhex(ln[1:]); n,a,t=b[0],(b[1]<<8)|b[2],b[3]; d=b[4:4+n]
        if t==4: base=((d[0]<<8)|d[1])<<16
        elif t==0:
            for i,x in enumerate(d): mem[base+a+i]=x
    return mem
for p in sys.argv[1:]:
    m=load(p); words={}
    for ba in m:
        pc=ba//2 & ~1
        words.setdefault(ba//4*2,0)
    ks=sorted(words); rngs=[]; s=ks[0]; prev=s
    for k in ks[1:]:
        if k!=prev+2: rngs.append((s,prev)); s=k
        prev=k
    rngs.append((s,prev))
    def w(pc): return m.get(pc*2,0)|m.get(pc*2+1,0)<<8|m.get(pc*2+2,0)<<16
    cfg={hex(pc):hex(w(pc)) for pc in range(0x2BF00,0x2BF30,2) if pc*2 in m}
    print(p, "words",len(ks), "ranges",[(hex(a),hex(b)) for a,b in rngs][:8])
    print("  config", cfg)
    print("  max code PC below 0x2B000:", hex(max(k for k in ks if k<0x2B000)))
