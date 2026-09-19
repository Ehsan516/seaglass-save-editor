"""Make copyright-free synthetic ROM/save fixtures and export renderer PNGs.

The shapes in these fixtures are deliberately NOT Pokemon artwork. Hardware
uses the user's ROM sprites. No private game data is required by these tests.
"""
from pathlib import Path
import struct
import sys
import zlib

ROOT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT))
from seaglass_save import SeaglassSave, Mon, _enc, DATA_SIZE, FOOTER, SIGNATURE, STAT_KEYS

OUT = ROOT / 'nds-editor/build/test'

def literal_lz(raw):
    return bytes([0x10]) + len(raw).to_bytes(3, 'little') + b''.join(
        b'\0' + raw[i:i+8] for i in range(0, len(raw), 8))

def make():
    OUT.mkdir(parents=True, exist_ok=True)
    rom = bytearray(b'\xff' * 0xA00000)
    def name(off, text):
        data = _enc(text) + b'\xff'
        rom[off:off+len(data)] = data
    # String and compressed data intentionally straddle 4 KiB cache pages.
    strings = 0x1ffffc
    move_names = {1:'Pound',2:'Karate Chop',3:'Sludge',4:'Mud Bomb',5:'Protect',6:'Rock Slide'}
    for i, text in move_names.items():
        off = strings + (i-1)*32
        name(off,text)
        struct.pack_into('<I',rom,0x6d2a18+(i-1)*0x38,0x08000000+off)
    # An earlier non-table reference must not be mistaken for the move table.
    struct.pack_into('<I',rom,0x100,0x08000000+strings+32)
    for i,text in {1:'Poké Ball',2:'Ultra Ball',3:'Great Ball',4:'Master Ball',5:'Muscle Feather'}.items():
        name(0x67e77c+i*0x54,text)
    name(0x6e15b0+65*0x1c,'Overgrow')
    name(0x6e15b0+67*0x1c,'Torrent')
    species = {1:'Bulbasaur',2:'Ivysaur',3:'Venusaur',25:'Pikachu',63:'Abra',129:'Magikarp',259:'Marshtomp',280:'Ralts'}
    for k,(sp,text) in enumerate(species.items()):
        base = 0x8f087c+(sp-1)*0xd0-0x2c
        rom[base:base+0xd0]=b'\xff'*0xd0
        rom[base:base+6]=bytes([70,85,70,50,60,70])
        rom[base+0x12]=127
        struct.pack_into('<HHH',rom,base+0x18,67,65,0)
        name(base+0x2c,text)
        # Asymmetric test badge: circular body, two pointed ears, white face.
        px = [[0]*64 for _ in range(64)]
        for y in range(6,60):
            for x in range(8,56):
                body=(x-32)**2+(y-36)**2<21**2
                ears=(12<x<24 and 7<y<29 and x-12>abs(y-16)//2) or (40<x<50 and 11<y<29)
                if body or ears:px[y][x]=1 if (x+y)%9 else 2
                if 24<x<43 and 27<y<43 and body:px[y][x]=3
                if y in (30,31,32) and x in (28,29,37,38):px[y][x]=4
        tiles=bytearray()
        for ty in range(8):
            for tx in range(8):
                for y in range(8):
                    for x in range(0,8,2):tiles.append(px[ty*8+y][tx*8+x]|(px[ty*8+y][tx*8+x+1]<<4))
        pic=0x300ffe+k*0x2000
        compressed=literal_lz(tiles)
        rom[pic:pic+len(compressed)]=compressed
        palette=[0, (8+k*2)|((17+k%4)<<5)|(24<<10), 5|(12<<5)|(20<<10),0x7fff,0x1084]+[0]*11
        pal=pic+0x1000;data=literal_lz(struct.pack('<16H',*palette))
        rom[pal:pal+len(data)]=data
        struct.pack_into('<I',rom,base+0x58,0x08000000+pic)
        struct.pack_into('<I',rom,base+0x68,0x08000000+pal)
        struct.pack_into('<I',rom,base+0x70,0x08000000+pal)
    (OUT/'fixture.gba').write_bytes(rom)
    save=bytearray(0x20000)
    mons=[]
    for i,sp in enumerate([259,25,63,1,280,129]):
        m=Mon(bytes(100),True);m.pv=100013+i*25;m.otid=0x12345678
        m.species=sp;m.nickname='Guts' if i==0 else species[sp]
        m.raw_head[0x14:0x1b]=_enc('Tester')+b'\xff'
        m.party_tail[4]=35;m.experience=35**3;m.held_item=5;m.moves=[3,4,5,6];m.pp=[20,15,10,10]
        m.ivs=dict(zip(STAT_KEYS,[14,31,31,16,25,16]));m.evs=dict(zip(STAT_KEYS,[68,36,66,125,61,35]));m.friendship=160
        mons.append(m)
    for slot in range(2):
        blocks=[bytearray(DATA_SIZE) for _ in range(14)]
        blocks[0][:7]=_enc('Tester')+b'\xff'
        sb=bytearray(4*DATA_SIZE);struct.pack_into('<I',sb,0x234,6)
        for i,m in enumerate(mons):sb[0x238+i*100:0x238+(i+1)*100]=m.encode()
        storage=bytearray(9*DATA_SIZE)
        for i in range(30):
            if i%7!=6:
                m=mons[i%6];m.is_party=False
                storage[4+i*80:4+(i+1)*80]=m.encode();m.is_party=True
        for i in range(4):blocks[1+i][:]=sb[i*DATA_SIZE:(i+1)*DATA_SIZE]
        for i in range(9):blocks[5+i][:]=storage[i*DATA_SIZE:(i+1)*DATA_SIZE]
        for sid,data in enumerate(blocks):
            off=slot*0xe000+sid*0x1000
            save[off:off+DATA_SIZE]=data
            struct.pack_into('<HHII',save,off+FOOTER,sid,SeaglassSave._checksum(data),SIGNATURE,slot+1)
    (OUT/'fixture.sav').write_bytes(save)
    s=SeaglassSave(OUT/'fixture.sav',OUT/'fixture.gba')
    assert s.rom_ok and s.verify_checksums()==[]
    print('Created synthetic ROM/save with page-boundary sprite data.')

def png_from_raw(path):
    raw=path.read_bytes();width,height=256,384
    def chunk(kind,data):return struct.pack('>I',len(data))+kind+data+struct.pack('>I',zlib.crc32(kind+data))
    scan=bytearray()
    for y in range(height):
        scan.append(0)
        for x in range(width):
            c=struct.unpack_from('<H',raw,(y*width+x)*2)[0]
            scan.extend(((c&31)*255//31,((c>>5)&31)*255//31,((c>>10)&31)*255//31))
    out=b'\x89PNG\r\n\x1a\n'+chunk(b'IHDR',struct.pack('>IIBBBBB',width,height,8,2,0,0,0))+chunk(b'IDAT',zlib.compress(scan))+chunk(b'IEND',b'')
    path.with_suffix('.png').write_bytes(out)

if __name__=='__main__':
    if len(sys.argv)>1 and sys.argv[1]=='png':
        for p in OUT.glob('*.rgb555'):png_from_raw(p)
        print('Exported exact 256x192 dual-screen renderer images.')
    else:make()
