"""
Recompute every glyph's ink box from the font bytes alone and compare with the goldens.

    python3 tools/oracle/check_font_bounds.py

This is tools/oracle/FORMAT-font.md's "How the ink box is computed" as running
code: a glyf parser, a Type 2 charstring interpreter and the bounds arithmetic,
written from that description with no fontTools, in the order of operations the
C++ reader has to use. It exists to show the description is sufficient: every
GLYPH record's box in tests/golden/font/*.raw.golden must come out bit for bit.
It is terse because it is a check, not a library; the C++ reader is the
implementation. Standard library only, except that the CFF glyph-name check
borrows fontTools' table of the 391 standard strings when fontTools is there.

Exit status 0 when every box matches.
"""
# UPSTREAM NOTICE. The bounds arithmetic in this file (Bounds.quad, Bounds.cubic
# and the quadratic solver inside it), the hflex1 and flex1 operators and the cmap
# format 4 decode follow fontTools 4.66.0 expression for expression, because
# bit-equal results need the same operations in the same order: solveQuadratic,
# calcCubicParameters, calcCubicBounds and calcQuadraticBounds in
# misc/bezierTools.py; op_hflex1 and op_flex1 in misc/psCharStrings.py;
# cmap_format_4.decompile in ttLib/tables/_c_m_a_p.py. src/opentype.cpp carries
# the same notice. fontTools is distributed under the following licence
# (THIRD-PARTY.md):
#
# MIT License
#
# Copyright (c) 2017 Just van Rossum
#
# Permission is hereby granted, free of charge, to any person obtaining a copy
# of this software and associated documentation files (the "Software"), to deal
# in the Software without restriction, including without limitation the rights
# to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
# copies of the Software, and to permit persons to whom the Software is
# furnished to do so, subject to the following conditions:
#
# The above copyright notice and this permission notice shall be included in all
# copies or substantial portions of the Software.
#
# THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
# IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
# FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
# AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
# LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
# OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
# SOFTWARE.
import struct, sys, math
from pathlib import Path
ROOT = Path(__file__).resolve().parents[2]
FONTS = ROOT / "tests" / "data" / "fonts"
GOLD = ROOT / "tests" / "golden" / "font"

def tables(blob):
    n = struct.unpack_from(">H", blob, 4)[0]; out = {}
    for i in range(n):
        tag, _, off, ln = struct.unpack_from(">4sIII", blob, 12+16*i); out[tag.decode("latin-1")] = blob[off:off+ln]
    return out

class Bounds:
    def __init__(s): s.b=None; s.cur=None
    def upd(s,p):
        x,y=p
        s.b=(x,y,x,y) if s.b is None else (min(s.b[0],x),min(s.b[1],y),max(s.b[2],x),max(s.b[3],y))
    def inside(s,p): return s.b[0]<=p[0]<=s.b[2] and s.b[1]<=p[1]<=s.b[3]
    def union(s,pts):
        for p in pts: s.upd(p)
    def move(s,p): s.upd(p); s.cur=p
    def line(s,p): s.upd(p); s.cur=p
    def quad(s,c,p):
        s.upd(p)
        if not s.inside(c):
            (x1,y1),(x2,y2),(x3,y3)=s.cur,c,p
            cx,cy=x1,y1; bx=(x2-cx)*2.0; by=(y2-cy)*2.0; ax=x3-cx-bx; ay=y3-cy-by
            ax2=ax*2.0; ay2=ay*2.0; roots=[]
            if ax2!=0: roots.append(-bx/ax2)
            if ay2!=0: roots.append(-by/ay2)
            s.union([(ax*t*t+bx*t+cx, ay*t*t+by*t+cy) for t in roots if 0<=t<1])
        s.cur=p
    def qcurve(s,offs,on):
        for i in range(len(offs)-1):
            (x,y),(nx,ny)=offs[i],offs[i+1]
            s.quad(offs[i],(0.5*(x+nx),0.5*(y+ny)))
        s.quad(offs[-1],on)
    def cubic(s,c1,c2,p):
        s.upd(p)
        if not s.inside(c1) or not s.inside(c2):
            (dx,dy),(x2,y2),(x3,y3),(x4,y4)=s.cur,c1,c2,p
            cx=(x2-dx)*3.0; cy=(y2-dy)*3.0; bx=(x3-x2)*3.0-cx; by=(y3-y2)*3.0-cy; ax=x4-dx-cx-bx; ay=y4-dy-cy-by
            def solve(a,b,c):
                if abs(a)<1e-10:
                    return [] if abs(b)<1e-10 else [-c/b]
                DD=b*b-4.0*a*c
                if DD>=0.0:
                    r=math.sqrt(DD); return [(-b+r)/2.0/a, (-b-r)/2.0/a]
                return []
            roots=[t for t in solve(ax*3.0,bx*2.0,cx) if 0<=t<1]+[t for t in solve(ay*3.0,by*2.0,cy) if 0<=t<1]
            s.union([(ax*t*t*t+bx*t*t+cx*t+dx, ay*t*t*t+by*t*t+cy*t+dy) for t in roots])
        s.cur=p

def glyf_boxes(blob):
    T=tables(blob); glyf=T["glyf"]; n=struct.unpack_from(">H",T["maxp"],4)[0]
    fmt=struct.unpack_from(">h",T["head"],50)[0]
    loca=[2*v for v in struct.unpack_from(f">{n+1}H",T["loca"],0)] if fmt==0 else list(struct.unpack_from(f">{n+1}I",T["loca"],0))
    nh=struct.unpack_from(">H",T["hhea"],34)[0]
    def lsb(g):
        return struct.unpack_from(">h",T["hmtx"],4*g+2)[0] if g<nh else struct.unpack_from(">h",T["hmtx"],4*nh+2*(g-nh))[0]
    def draw(g, pen, xf, top):
        a,e=loca[g],loca[g+1]
        if a==e: return
        nc,xmin=struct.unpack_from(">hh",glyf,a)
        if nc==0: return
        if nc<0:
            at=a+10
            while True:
                fl,child=struct.unpack_from(">HH",glyf,at); at+=4
                if fl&1: dx,dy=struct.unpack_from(">hh",glyf,at); at+=4
                else: dx,dy=struct.unpack_from(">bb",glyf,at); at+=2
                assert fl&2
                xx,xy,yx,yy=1,0,0,1
                if fl&8: xx=yy=struct.unpack_from(">h",glyf,at)[0]/16384.0; at+=2
                elif fl&0x40:
                    xx,yy=(v/16384.0 for v in struct.unpack_from(">hh",glyf,at)); at+=4
                elif fl&0x80:
                    xx,xy,yx,yy=(v/16384.0 for v in struct.unpack_from(">hhhh",glyf,at)); at+=8
                t=(xx,xy,yx,yy,dx,dy)
                draw(child,pen,xf+[t] if t!=(1,0,0,1,0,0) else xf,False)
                if not fl&0x20: break
            return
        off = lsb(g)-xmin if top else 0
        ends=struct.unpack_from(f">{nc}H",glyf,a+10); npts=ends[-1]+1
        at=a+10+2*nc; at+=2+struct.unpack_from(">H",glyf,at)[0]
        flags=[]
        while len(flags)<npts:
            f=glyf[at]; at+=1; flags.append(f)
            if f&8: flags += [f]*glyf[at]; at+=1
        xs=[];v=0
        for f in flags:
            if f&2: d=glyf[at]; at+=1; v += d if f&0x10 else -d
            elif not f&0x10: v+=struct.unpack_from(">h",glyf,at)[0]; at+=2
            xs.append(v)
        ys=[];v=0
        for f in flags:
            if f&4: d=glyf[at]; at+=1; v += d if f&0x20 else -d
            elif not f&0x20: v+=struct.unpack_from(">h",glyf,at)[0]; at+=2
            ys.append(v)
        def tp(p):
            x,y=p
            for (xx,xy,yx,yy,dx,dy) in reversed(xf): x,y = xx*x+yx*y+dx, xy*x+yy*y+dy
            return (x,y)
        s=0
        for e in ends:
            pts=[tp((xs[i]+off,ys[i])) for i in range(s,e+1)]; on=[flags[i]&1 for i in range(s,e+1)]; s=e+1
            if 1 not in on:
                (x,y),(nx,ny)=pts[-1],pts[0]; st=(0.5*(x+nx),0.5*(y+ny)); pen.move(st); pen.qcurve(pts,st); continue
            k=on.index(1)+1; pts=pts[k:]+pts[:k]; on=on[k:]+on[:k]
            pen.move(pts[-1]); offs=[]
            for p,o in zip(pts,on):
                if o:
                    if offs: pen.qcurve(offs,p); offs=[]
                    else: pen.line(p)
                else: offs.append(p)
    out=[]
    for g in range(n):
        pen=Bounds(); draw(g,pen,[],True); out.append(pen.b)
    return out

def index(data, at):
    count=struct.unpack_from(">H",data,at)[0]
    if count==0: return [], at+2
    osz=data[at+2]; offs=[int.from_bytes(data[at+3+i*osz:at+3+(i+1)*osz],"big") for i in range(count+1)]
    base=at+3+(count+1)*osz-1
    return [data[base+offs[i]:base+offs[i+1]] for i in range(count)], base+offs[-1]

def dict_parse(d):
    out={}; st=[]; i=0
    while i<len(d):
        b=d[i]
        if b<=21:
            if b==12: op=(12,d[i+1]); i+=2
            else: op=b; i+=1
            out[op]=st; st=[]
        elif b==28: st.append(struct.unpack_from(">h",d,i+1)[0]); i+=3
        elif b==29: st.append(struct.unpack_from(">i",d,i+1)[0]); i+=5
        elif b==30:
            i+=1; s=""
            while True:
                for nib in (d[i]>>4, d[i]&15):
                    if nib<10: s+=str(nib)
                    elif nib==10: s+="."
                    elif nib==11: s+="E"
                    elif nib==12: s+="E-"
                    elif nib==14: s+="-"
                if d[i]&15==15: i+=1; break
                i+=1
            st.append(float(s))
        elif b<=246: st.append(b-139); i+=1
        elif b<=250: st.append((b-247)*256+d[i+1]+108); i+=2
        else: st.append(-(b-251)*256-d[i+1]-108); i+=2
    return out

def cff_parse(blob):
    cff=tables(blob)["CFF "]
    at=cff[2]
    names,at=index(cff,at); tops,at=index(cff,at); strings,at=index(cff,at); gsubrs,at=index(cff,at)
    top=dict_parse(tops[0])
    chars,_=index(cff,top[17][0])
    psize,poff=top[18]; priv=dict_parse(cff[poff:poff+psize])
    lsubrs=index(cff,poff+priv[19][0])[0] if 19 in priv else []
    # charset
    co=top.get(15,[0])[0]; n=len(chars); sids=[0]
    fmt=cff[co]; p=co+1
    if fmt==0:
        sids += list(struct.unpack_from(f">{n-1}H",cff,p))
    else:
        while len(sids)<n:
            if fmt==1: first,left=struct.unpack_from(">HB",cff,p); p+=3
            else: first,left=struct.unpack_from(">HH",cff,p); p+=4
            sids += list(range(first, first+left+1))
    return chars,gsubrs,lsubrs,sids[:n],strings,fmt

def cff_boxes(blob):
    chars,gsubrs,lsubrs,_,_,_=cff_parse(blob)
    bias=lambda s: 107 if len(s)<1240 else (1131 if len(s)<33900 else 32768)
    out=[]
    for cs in chars:
        pen=Bounds(); st=[]; x=y=0; nh=0; seenw=False
        def mv(dx,dy):
            nonlocal x,y; x+=dx; y+=dy; pen.move((x,y))
        def ln(dx,dy):
            nonlocal x,y; x+=dx; y+=dy; pen.line((x,y))
        def cv(a,b,c,d,e,f):
            nonlocal x,y
            p1=(x+a,y+b); p2=(p1[0]+c,p1[1]+d); x,y=p2[0]+e,p2[1]+f; pen.cubic(p1,p2,(x,y))
        def run(code):
            nonlocal st,nh,seenw
            i=0
            def width(nargs_even=None, n=None):
                nonlocal st,seenw
                if not seenw:
                    seenw=True
                    if n is not None:
                        if len(st)>n: st=st[1:]
                    elif len(st)%2: st=st[1:]
            while i<len(code):
                b=code[i]
                if b>=32 or b==28:
                    if b==28: st.append(struct.unpack_from(">h",code,i+1)[0]); i+=3
                    elif b<=246: st.append(b-139); i+=1
                    elif b<=250: st.append((b-247)*256+code[i+1]+108); i+=2
                    elif b<=254: st.append(-(b-251)*256-code[i+1]-108); i+=2
                    else: st.append(struct.unpack_from(">i",code,i+1)[0]/65536.0); i+=5
                    continue
                i+=1
                if b in (1,3,18,23): width(); nh+=len(st)//2; st=[]
                elif b in (19,20):
                    width(); nh+=len(st)//2; st=[]; i+=(nh+7)//8
                elif b==21: width(n=2); mv(st[0],st[1]); st=[]
                elif b==22: width(n=1); mv(st[0],0); st=[]
                elif b==4: width(n=1); mv(0,st[0]); st=[]
                elif b==5:
                    for k in range(0,len(st),2): ln(st[k],st[k+1])
                    st=[]
                elif b in (6,7):
                    h=(b==6)
                    for v in st:
                        ln(v,0) if h else ln(0,v); h=not h
                    st=[]
                elif b==8:
                    for k in range(0,len(st),6): cv(*st[k:k+6])
                    st=[]
                elif b in (30,31):
                    h=(b==31); k=0
                    while len(st)-k>=4:
                        a,b2,c,d=st[k:k+4]; k+=4
                        last = (len(st)-k==1)
                        if h: cv(a,0,b2,c, st[k] if last else 0, d)
                        else: cv(0,a,b2,c, d, st[k] if last else 0)
                        if last: k+=1
                        h=not h
                    st=[]
                elif b==10:
                    n=st.pop()+bias(lsubrs)
                    if run(lsubrs[n]): return True
                elif b==29:
                    n=st.pop()+bias(gsubrs)
                    if run(gsubrs[n]): return True
                elif b==11: return False
                elif b==14: width(n=0); return True
                else: raise SystemExit(f"op {b}")
            return False
        run(cs); out.append(pen.b)
    return out

bad=0
for gold in sorted(GOLD.glob("*.raw.golden")):
    base=gold.name[:-len(".raw.golden")]; blob=(FONTS/base).read_bytes()
    boxes = cff_boxes(blob) if blob[:4]==b"OTTO" else glyf_boxes(blob)
    n=0; miss=0
    for line in gold.read_text().splitlines():
        if not line.startswith("GLYPH "): continue
        t=line.split(); gid=int(t[1]); want=tuple(float.fromhex(v) for v in t[-4:])
        got=boxes[gid] or (0.0,0.0,0.0,0.0); got=tuple(float(v) for v in got); n+=1
        if got!=want:
            miss+=1
            if miss<4: print("  MISMATCH",base,gid,t[2],want,got)
    print(base, "glyphs", n, "mismatches", miss); bad+=miss
    if blob[:4]==b"OTTO":
        chars,g,l,sids,strings,fmt=cff_parse(blob)
        print("  charset format",fmt,"sids<391:",sum(1 for s_ in sids if s_<391))
        try:
            import fontTools.cffLib as c
        except ImportError:
            continue
        names=[c.cffStandardStrings[s_] if s_<391 else strings[s_-391].decode("latin-1") for s_ in sids]
        m=0
        for line in gold.read_text().splitlines():
            if line.startswith("GLYPH "):
                t=line.split(); m+= names[int(t[1])]!=t[2]
        print("  name mismatches",m); bad+=m
sys.exit(1 if bad else 0)
