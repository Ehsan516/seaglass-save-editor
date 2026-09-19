"""Build/run host UI tests and byte-compare Python, C and DS streaming.

Usage: python nds-editor/test/validate.py [--save test.sav --rom game.gba]
Only writes test outputs under nds-editor/build/test, never the input save.
"""
import argparse
from pathlib import Path
import subprocess
import sys
from fixtures import ROOT, OUT, make, png_from_raw
sys.path.insert(0,str(ROOT))
from seaglass_save import SeaglassSave, STAT_KEYS

def run(*args):
    return subprocess.run([str(a) for a in args],cwd=ROOT,check=True,capture_output=True,text=True,encoding='utf-8')

def main():
    args=argparse.ArgumentParser()
    args.add_argument('--save',type=Path);args.add_argument('--rom',type=Path)
    opts=args.parse_args()
    if bool(opts.save)!=bool(opts.rom):args.error('--save and --rom must be supplied together')
    make()
    common=['gcc','-O2','-Wall','-Wextra','-I3ds-editor/source','-Inds-editor/source']
    core='3ds-editor/source/seaglass_core.c'
    results=[]
    for label,flags in [('ram',[]),('stream',['-D__NDS__'])]:
        exe=OUT/f'test_stream_{label}.exe'
        build=run(*common,*flags,'nds-editor/test/test_stream.c',core,'-o',exe)
        if build.stderr:print(build.stderr,end='')
        save=opts.save.resolve() if opts.save else OUT/'fixture.sav'
        rom=opts.rom.resolve() if opts.rom else OUT/'fixture.gba'
        result=run(exe,save,rom,OUT/f'{label}-edited.sav')
        results.append(result.stdout)
    assert results[0]==results[1], 'RAM/stream lookup or sprite mismatch'
    a=(OUT/'ram-edited.sav').read_bytes();b=(OUT/'stream-edited.sav').read_bytes()
    assert a==b,'RAM/stream save-byte mismatch'
    s=SeaglassSave(save,rom);assert s.rom_ok
    original=s.party()[0]
    def fnv(data):
        h=2166136261
        for value in data:h=((h^value)*16777619)&0xffffffff
        return h
    for line in results[0].splitlines():
        if line.startswith('sprite_missing '):
            assert not s.sprite_rgba(int(line.split()[1])),line
        if line.startswith('sprite '):
            sp=int(line.split()[1]);result=s.sprite_rgba(sp)
            assert result and f'{fnv(result[2]):08x}'==line.split()[-1],line
        if line.startswith('move '):
            _,idx,name=line.split(' ',2);assert s.move_name(int(idx))==name
        if line.startswith('item '):
            _,idx,name=line.split(' ',2);assert s.item_name(int(idx))==name
    s.recompute_stats(original)
    import struct
    stats=struct.unpack_from('<6H',original.party_tail,8)
    for line in results[0].splitlines():
        if line.startswith('stat '):
            _,idx,value=line.split();assert stats[int(idx)]==int(value),line
    s.set_level(original,60);original.ivs={k:31 for k in STAT_KEYS};s.recompute_stats(original)
    s.write_party_mon(0,original);s.save_as(OUT/'python-edited.sav')
    assert (OUT/'python-edited.sav').read_bytes()==a,'Python/C save mismatch'
    assert SeaglassSave(OUT/'python-edited.sav',rom).verify_checksums()==[]
    before=save.read_bytes();base=0 if s.active_slot==0 else 0xe000
    assert a[:base]==before[:base] and a[base+0xe000:]==before[base+0xe000:]
    for section in range(14):
        at=base+section*0x1000+0xffc;assert a[at:at+4]==before[at:at+4]
    print('PASS: Python/C/DS names, sprite hashes, stat math, byte-identical edits, checksums, inactive slot, counters')
    ui=OUT/'test_ui_stream.exe'
    build=run(*common,'-D__NDS__','nds-editor/test/test_ui.c','nds-editor/source/ui.c',core,'-o',ui)
    if build.stderr:print(build.stderr,end='')
    print(run(ui,OUT/'fixture.sav',OUT/'fixture.gba',OUT/'ui-edited.sav').stdout,end='')
    assert SeaglassSave(OUT/'ui-edited.sav').verify_checksums()==[]
    for path in OUT.glob('*.rgb555'):png_from_raw(path)

if __name__=='__main__':main()
