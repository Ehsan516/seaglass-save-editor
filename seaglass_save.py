"""
seaglass_save.py -- read/WRITE save editor core for Pokemon Emerald Seaglass
(pokeemerald-expansion). Validated against a real save.

Layout facts (reverse-engineered & verified):
  * 128 KB Flash save: 2 slots x 14 sections x 4 KB. Footer @0xFF4:
    u16 sectionID, u16 checksum, u32 signature(0x08012025), u32 saveCounter.
    Active slot = highest counter. Edit active slot only; counter untouched.
  * Species indexed by National Dex number.
  * Party: SaveBlock1+0x234 u32 count, +0x238 six 100-byte Pokemon.
  * Pokemon crypto: key = PV ^ OTID; substructure order = PV % 24;
    checksum = sum of 24 LE u16 over decrypted 48-byte block.
  * ROM species-info: SpeciesInfo struct, stride 0xD0.
      name      @ struct+0x2c   (so struct base = nameAddr - 0x2c)
      baseStats @ struct+0x00   [HP,Atk,Def,Spe,SpA,SpD] u8
      genderRatio @ struct+0x12
      nameAddr(species) = 0x8f087c + (species-1)*0xD0
  * Ability is stored in Misc bit31 (NOT derived from PV) -> nature edits
    (which must change PV) preserve ability automatically.
"""
import struct, random

SIGNATURE = 0x08012025
SECTION_SIZE = 0x1000
DATA_SIZE = 0xF80
FOOTER = 0xFF4
SLOT_BASES = (0x0000, 0xE000)
SB1_SECTION_IDS = [1, 2, 3, 4]
STORAGE_SECTION_IDS = list(range(5, 14))

PARTY_COUNT_OFF = 0x234
PARTY_OFF = 0x238
PARTY_MON_SIZE = 100
BOX_MON_SIZE = 80

# ROM tables
ROM_NAME_SP1 = 0x8f087c
ROM_STRIDE = 0xD0
ROM_STATS_REL = -0x2c      # base stats relative to name addr
ROM_GENDER_REL = -0x2c + 0x12
SHEDINJA = 292
SHINY_THRESHOLD = 8   # shiny if (TID^SID^PVhi^PVlo) < this (Gen3 determination)
MOVE_NAME_PTR1 = 0x6d2a18   # gMovesInfo[1].name pointer field
MOVE_INFO_STRIDE = 0x38
ITEM_NAME_BASE = 0x67e77c    # gItemsInfo[0].name (embedded), stride 0x54
ITEM_STRIDE = 0x54
ABILITY_NAME_BASE = 0x6e15b0   # gAbilitiesInfo names, stride 0x1c (vanilla ability IDs)
ABILITY_STRIDE = 0x1c
SPECIES_ABILITIES_REL = -0x2c + 0x18   # abilities[3] (slot0,slot1,hidden) u8 each

NATURES = ["Hardy","Lonely","Brave","Adamant","Naughty","Bold","Docile","Relaxed",
"Impish","Lax","Timid","Hasty","Serious","Jolly","Naive","Modest","Mild","Quiet",
"Bashful","Rash","Calm","Gentle","Sassy","Careful","Quirky"]
SUBSTRUCT_ORDER = ["GAEM","GAME","GEAM","GEMA","GMAE","GMEA","AGEM","AGME","AEGM","AEMG",
"AMGE","AMEG","EGAM","EGMA","EAGM","EAMG","EMGA","EMAG","MGAE","MGEA","MAGE","MAEG","MEGA","MEAG"]
STAT_KEYS = ["hp","atk","defense","speed","spatk","spdef"]   # internal/stored order

_CH = {0x00:' ', 0xAE:'-', 0xAD:'.', 0xBA:'/', 0xAB:'!', 0xAC:'?'}
for _i in range(10): _CH[0xA1+_i] = chr(48+_i)
for _i in range(26): _CH[0xBB+_i] = chr(65+_i)
for _i in range(26): _CH[0xD5+_i] = chr(97+_i)
_CH[0xB4] = "'"
_CH[0x1B] = 'é'
_REV = {v:k for k,v in _CH.items()}

def decode_str(b):
    out=[]
    for c in b:
        if c==0xFF: break
        out.append(_CH.get(c,''))
    return ''.join(out).rstrip()

def encode_str(s, length):
    out=bytearray()
    for ch in s[:length]:
        out.append(_REV.get(ch,0x00))
    out.append(0xFF)
    out += b'\xFF'*(length+1-len(out))
    return bytes(out[:length+1])

# ---------- experience / growth ----------
def _exp_at(group, n):
    if n<=1: return 0
    n3=n*n*n
    if group==0:   return n3                                   # Medium Fast
    if group==4:   return 4*n3//5                              # Fast
    if group==5:   return 5*n3//4                              # Slow
    if group==3:   return (6*n3)//5 - 15*n*n + 100*n - 140     # Medium Slow
    if group==1:                                               # Erratic
        if n<=50:  return n3*(100-n)//50
        if n<=68:  return n3*(150-n)//100
        if n<=98:  return n3*((1911-10*n)//3)//500
        return n3*(160-n)//100
    if group==2:                                               # Fluctuating
        if n<=15:  return n3*(((n+1)//3)+24)//50
        if n<=36:  return n3*(n+14)//50
        return n3*((n//2)+32)//50
    return n3

def detect_growth(exp, level):
    """Identify growth group from a known (exp,level) pair."""
    best=(99,0)
    for g in range(6):
        lo=_exp_at(g, level)
        hi=_exp_at(g, level+1) if level<100 else lo+1
        if lo<=exp<hi:
            return g
        d=abs(exp-lo)
        if d<best[0]: best=(d,g)
    return best[1]

NATURE_MOD = []  # per nature: (boosted_stat_idx, lowered_stat_idx) in 0..4 (Atk,Def,Spe,SpA,SpD)
for _n in range(25):
    NATURE_MOD.append((_n//5, _n%5))

# --------------------------------------------------------------------------- #
class Mon:
    """Decoded Pokemon with editable fields. Stores original substructs so
    untouched data (contest stats, ribbons, met info, ppBonuses) is preserved."""
    def __init__(self, raw, is_party):
        self.is_party = is_party
        self.raw_head = bytearray(raw[:0x20])     # PV,OTID,nick,otname,checksum...
        self.party_tail = bytearray(raw[0x50:0x64]) if is_party and len(raw)>=100 else bytearray()
        pv, otid = struct.unpack_from("<II", raw, 0)
        self.pv = pv; self.otid = otid
        key = pv ^ otid
        enc = bytearray(raw[0x20:0x20+48])
        for i in range(0,48,4):
            struct.pack_into("<I", enc, i, struct.unpack_from("<I",enc,i)[0]^key)
        order = SUBSTRUCT_ORDER[pv%24]
        self.G=bytearray(enc[order.index('G')*12:order.index('G')*12+12])
        self.A=bytearray(enc[order.index('A')*12:order.index('A')*12+12])
        self.E=bytearray(enc[order.index('E')*12:order.index('E')*12+12])
        self.M=bytearray(enc[order.index('M')*12:order.index('M')*12+12])

    # --- decoded properties ---
    @property
    def species(self): return struct.unpack_from("<H", self.G, 0)[0]
    @species.setter
    def species(self, v): struct.pack_into("<H", self.G, 0, v)
    @property
    def held_item(self): return struct.unpack_from("<H", self.G, 2)[0]
    @held_item.setter
    def held_item(self, v): struct.pack_into("<H", self.G, 2, v)
    @property
    def experience(self): return struct.unpack_from("<I", self.G, 4)[0]
    @experience.setter
    def experience(self, v): struct.pack_into("<I", self.G, 4, v)
    @property
    def friendship(self): return self.G[9]
    @friendship.setter
    def friendship(self, v): self.G[9]=v & 0xFF

    @property
    def moves(self): return list(struct.unpack_from("<HHHH", self.A, 0))
    @moves.setter
    def moves(self, mv):
        for i,m in enumerate(mv[:4]): struct.pack_into("<H", self.A, i*2, m)
    @property
    def pp(self): return list(self.A[8:12])
    @pp.setter
    def pp(self, p):
        for i,v in enumerate(p[:4]): self.A[8+i]=v & 0xFF

    @property
    def evs(self): return {k:self.E[i] for i,k in enumerate(STAT_KEYS)}
    @evs.setter
    def evs(self, d):
        for i,k in enumerate(STAT_KEYS): self.E[i]=d[k] & 0xFF

    @property
    def _ivword(self): return struct.unpack_from("<I", self.M, 4)[0]
    @property
    def ivs(self):
        w=self._ivword
        return {STAT_KEYS[i]:(w>>(5*i))&31 for i in range(6)}
    @ivs.setter
    def ivs(self, d):
        w=self._ivword & ~((1<<30)-1)  # keep isEgg/ability bits (30,31)
        for i,k in enumerate(STAT_KEYS): w |= (d[k]&31)<<(5*i)
        struct.pack_into("<I", self.M, 4, w & 0xFFFFFFFF)
    @property
    def ability_slot(self): return (self._ivword>>31)&1
    @ability_slot.setter
    def ability_slot(self, v):
        w = self._ivword & ~(1<<31)
        if v: w |= (1<<31)
        struct.pack_into("<I", self.M, 4, w & 0xFFFFFFFF)
    @property
    def is_egg(self): return bool((self._ivword>>30)&1)

    @property
    def nature(self): return self.pv % 25
    @property
    def nickname(self): return decode_str(self.raw_head[0x08:0x12])
    @nickname.setter
    def nickname(self, s): self.raw_head[0x08:0x12]=encode_str(s,10)[:10]
    @property
    def ot_name(self): return decode_str(self.raw_head[0x14:0x1B])
    @property
    def shiny(self):
        return ((self.otid&0xFFFF)^(self.otid>>16)^(self.pv&0xFFFF)^(self.pv>>16))<SHINY_THRESHOLD
    @property
    def level(self):
        return self.party_tail[0x04] if self.is_party else 0

    # --- encode back to bytes ---
    def encode(self):
        struct.pack_into("<I", self.raw_head, 0, self.pv)
        struct.pack_into("<I", self.raw_head, 4, self.otid)
        order=SUBSTRUCT_ORDER[self.pv%24]
        block=bytearray(48)
        for letter,buf in (('G',self.G),('A',self.A),('E',self.E),('M',self.M)):
            block[order.index(letter)*12:order.index(letter)*12+12]=buf
        chk=sum(struct.unpack_from("<H",block,i)[0] for i in range(0,48,2))&0xFFFF
        struct.pack_into("<H", self.raw_head, 0x1C, chk)
        key=self.pv^self.otid
        enc=bytearray(block)
        for i in range(0,48,4):
            struct.pack_into("<I", enc, i, struct.unpack_from("<I",enc,i)[0]^key)
        out=bytearray(self.raw_head)+enc
        if self.is_party:
            out+=self.party_tail
        return bytes(out)


# --------------------------------------------------------------------------- #
class SeaglassSave:
    def __init__(self, save_path, rom_path=None):
        self.path=save_path
        self.data=bytearray(open(save_path,"rb").read())
        self.rom=open(rom_path,"rb").read() if rom_path else None
        self.sections={}; self.active_slot=None; self.save_counter=-1
        self._load_slots()

    # ---- slot handling ----
    def _slot_sections(self, base):
        secs={}
        for i in range(14):
            off=base+i*SECTION_SIZE
            sid,chk=struct.unpack_from("<HH",self.data,off+FOOTER)
            sig,ctr=struct.unpack_from("<II",self.data,off+FOOTER+4)
            if sig==SIGNATURE: secs[sid]=(off,ctr)
        return secs
    def _load_slots(self):
        best=None
        for idx,base in enumerate(SLOT_BASES):
            secs=self._slot_sections(base)
            if not secs: continue
            ctr=max(v[1] for v in secs.values())
            if best is None or ctr>best[0]: best=(ctr,idx,secs)
        if best is None: raise ValueError("No valid save slot.")
        self.save_counter,self.active_slot,secs=best
        self.sections={sid:off for sid,(off,_) in secs.items()}

    def _reassemble(self, ids):
        out=bytearray()
        for sid in ids:
            if sid in self.sections:
                out+=self.data[self.sections[sid]:self.sections[sid]+DATA_SIZE]
        return out
    @property
    def sb1(self): return self._reassemble(SB1_SECTION_IDS)
    @property
    def storage(self): return self._reassemble(STORAGE_SECTION_IDS)

    @staticmethod
    def _checksum(block):
        t=0
        for i in range(0,len(block),4):
            t=(t+struct.unpack_from("<I",block,i)[0])&0xFFFFFFFF
        return ((t>>16)+(t&0xFFFF))&0xFFFF
    def _fix_section_checksum(self, sid):
        off=self.sections[sid]
        chk=self._checksum(self.data[off:off+DATA_SIZE])
        struct.pack_into("<H", self.data, off+FOOTER+2, chk)
    def verify_checksums(self):
        bad=[]
        for sid,off in self.sections.items():
            stored=struct.unpack_from("<H",self.data,off+FOOTER+2)[0]
            if stored!=self._checksum(self.data[off:off+DATA_SIZE]): bad.append(sid)
        return bad

    # ---- SaveBlock1 offset -> raw file offset ----
    def _write_sb1(self, sb1_off, payload):
        touched=set()
        for j,byte in enumerate(payload):
            o=sb1_off+j
            seg=o//DATA_SIZE; within=o%DATA_SIZE
            sid=SB1_SECTION_IDS[seg]
            self.data[self.sections[sid]+within]=byte
            touched.add(sid)
        for sid in touched: self._fix_section_checksum(sid)

    def _write_storage(self, st_off, payload):
        touched=set()
        for j,byte in enumerate(payload):
            o=st_off+j
            seg=o//DATA_SIZE; within=o%DATA_SIZE
            sid=STORAGE_SECTION_IDS[seg]
            self.data[self.sections[sid]+within]=byte
            touched.add(sid)
        for sid in touched: self._fix_section_checksum(sid)

    # ---- ROM lookups ----
    def _name_addr(self, sp): return ROM_NAME_SP1+(sp-1)*ROM_STRIDE
    def species_name(self, sp):
        if not self.rom or sp==0: return f"#{sp}"
        a=self._name_addr(sp)
        return decode_str(self.rom[a:a+12]) or f"#{sp}"
    def base_stats(self, sp):
        a=self._name_addr(sp)+ROM_STATS_REL
        return list(self.rom[a:a+6])           # HP,Atk,Def,Spe,SpA,SpD
    def gender_ratio(self, sp):
        return self.rom[self._name_addr(sp)+ROM_GENDER_REL]
    def move_name(self, m):
        if not self.rom: return f"move{m}"
        if m == 0: return "—"
        p = MOVE_NAME_PTR1 + (m-1)*MOVE_INFO_STRIDE
        ptr = int.from_bytes(self.rom[p:p+4], "little")
        if not (0x08000000 <= ptr < 0x0A000000): return f"move{m}"
        return decode_str(self.rom[ptr-0x08000000:ptr-0x08000000+16]) or f"move{m}"

    def item_name(self, i):
        if not self.rom: return f"item{i}"
        if i == 0: return "(none)"
        s = decode_str(self.rom[ITEM_NAME_BASE+i*ITEM_STRIDE:ITEM_NAME_BASE+i*ITEM_STRIDE+14])
        return s or f"item{i}"

    def move_list(self):
        out = [(0, "—")]
        for m in range(1, 1000):
            nm = self.move_name(m)
            if nm and not nm.startswith("move") and any(c.isalnum() for c in nm):
                out.append((m, nm))
        return out

    def ability_name(self, i):
        if not self.rom: return f"ability{i}"
        if i == 0: return "(none)"
        s = decode_str(self.rom[ABILITY_NAME_BASE+i*ABILITY_STRIDE:ABILITY_NAME_BASE+i*ABILITY_STRIDE+13])
        return s or f"ability{i}"

    def species_abilities(self, sp):
        """Regular ability slots for a species as [(slot, id, name)] (slots 0 and 1)."""
        if not self.rom: return [(0, 0, "?")]
        b = self._name_addr(sp) + SPECIES_ABILITIES_REL
        a0 = int.from_bytes(self.rom[b:b+2], "little")     # slot 0 @ +0x18
        a1 = int.from_bytes(self.rom[b+2:b+4], "little")    # slot 1 @ +0x1a
        out = [(0, a0, self.ability_name(a0))]
        if a1 and a1 != a0:
            out.append((1, a1, self.ability_name(a1)))
        return out

    def item_list(self):
        out = [(0, "(none)")]
        for i in range(1, 1300):
            nm = self.item_name(i)
            if nm and not nm.startswith("item") and any(c.isalnum() for c in nm):
                out.append((i, nm))
        return out

    def species_list(self):
        out=[]
        for sp in range(1,1600):
            nm=self.species_name(sp)
            if nm and not nm.startswith('#') and nm[0].isalpha():
                out.append((sp,nm))
        return out

    # ---- gender / shiny helpers ----
    def gender_of(self, pv, sp):
        r=self.gender_ratio(sp) if self.rom else 255
        if r==255: return 'N'
        if r==254: return 'F'
        if r==0:   return 'M'
        return 'F' if (pv&0xFF)<r else 'M'
    @staticmethod
    def _is_shiny(pv,otid):
        return ((otid&0xFFFF)^(otid>>16)^(pv&0xFFFF)^(pv>>16))<SHINY_THRESHOLD

    def reroll_pv(self, mon, nature=None, shiny=None, gender=None):
        """Find a PV satisfying the requested nature / shiny / gender.
        Unspecified constraints keep the current value. Ability is stored
        separately (Misc bit31), so it is preserved automatically."""
        if nature is None: nature = mon.nature
        if shiny is None:  shiny  = self._is_shiny(mon.pv, mon.otid)
        otid  = mon.otid
        ratio = self.gender_ratio(mon.species) if self.rom else None
        fixed = (ratio is None) or (ratio in (0, 254, 255))
        if fixed or gender is None:
            gender = self.gender_of(mon.pv, mon.species)
        tidsid = (otid & 0xFFFF) ^ (otid >> 16)
        rng = random.Random((mon.pv ^ otid ^ 0x5EA61A55) & 0xFFFFFFFF)

        def gender_ok(pv):
            if fixed: return True
            return ('F' if (pv & 0xFF) < ratio else 'M') == gender
        def low_byte():
            if fixed: return rng.getrandbits(8)
            return rng.randrange(0, ratio) if gender == 'F' else rng.randrange(ratio, 256)

        if shiny:
            # construct PVhi so that PVlo^PVhi^tidsid < SHINY_THRESHOLD
            for _ in range(2_000_000):
                pvlo = (rng.getrandbits(8) << 8) | low_byte()
                pvhi = (pvlo ^ tidsid ^ rng.randrange(SHINY_THRESHOLD)) & 0xFFFF
                pv = (pvhi << 16) | pvlo
                if pv % 25 == nature and gender_ok(pv) and self._is_shiny(pv, otid):
                    return pv
            return mon.pv
        else:
            for _ in range(2_000_000):
                pv = rng.getrandbits(32)
                if pv % 25 != nature: continue
                if self._is_shiny(pv, otid): continue
                if not gender_ok(pv): continue
                return pv
            return mon.pv

    # ---- stat recompute (party) ----
    def recompute_stats(self, mon):
        if not (self.is_party_capable() and mon.is_party): return
        base=self.base_stats(mon.species); iv=mon.ivs; ev=mon.evs; L=mon.party_tail[0x04]
        b,l=NATURE_MOD[mon.nature]
        def core(i):
            return ((2*base[i]+iv[STAT_KEYS[i]]+ev[STAT_KEYS[i]]//4)*L)//100
        hp = 1 if mon.species==SHEDINJA else core(0)+L+10
        stats=[hp]
        for i in range(1,6):
            v=core(i)+5
            ni=i-1
            if b!=l:
                if ni==b: v=v*110//100
                elif ni==l: v=v*90//100
            stats.append(v)
        # write current hp = maxHP (full heal), then maxHP, atk, def, spe, spatk, spdef
        struct.pack_into("<H", mon.party_tail, 0x06, stats[0])  # current hp @0x56
        struct.pack_into("<H", mon.party_tail, 0x08, stats[0])  # max hp   @0x58
        struct.pack_into("<H", mon.party_tail, 0x0A, stats[1])  # atk      @0x5A
        struct.pack_into("<H", mon.party_tail, 0x0C, stats[2])  # def      @0x5C
        struct.pack_into("<H", mon.party_tail, 0x0E, stats[3])  # speed    @0x5E
        struct.pack_into("<H", mon.party_tail, 0x10, stats[4])  # spatk    @0x60
        struct.pack_into("<H", mon.party_tail, 0x12, stats[5])  # spdef    @0x62
    def is_party_capable(self): return self.rom is not None

    def set_level(self, mon, level):
        level=max(1,min(100,level))
        g=detect_growth(mon.experience, mon.party_tail[0x04] or 1)
        mon.experience=_exp_at(g, level)
        mon.party_tail[0x04]=level

    # ---- read parties / boxes ----
    def party(self):
        sb1=self.sb1
        cnt=struct.unpack_from("<I",sb1,PARTY_COUNT_OFF)[0]
        return [Mon(sb1[PARTY_OFF+i*100:PARTY_OFF+i*100+100], True) for i in range(min(cnt,6))]
    def party_count(self):
        return struct.unpack_from("<I",self.sb1,PARTY_COUNT_OFF)[0]
    def box_mons(self):
        blk=self.storage; out=[]
        i=4
        while i+80<=len(blk):
            head=blk[i:i+80]
            m=self._try_box(head, i)
            if m: out.append((i,m))
            i+=80
        return out
    def _try_box(self, head, off):
        if len(head)<80: return None
        pv,otid=struct.unpack_from("<II",head,0)
        stored=struct.unpack_from("<H",head,0x1C)[0]
        key=pv^otid; enc=bytearray(head[0x20:0x20+48])
        for k in range(0,48,4):
            struct.pack_into("<I",enc,k,struct.unpack_from("<I",enc,k)[0]^key)
        if sum(struct.unpack_from("<H",enc,k)[0] for k in range(0,48,2))&0xFFFF!=stored: return None
        order=SUBSTRUCT_ORDER[pv%24]
        sp=struct.unpack_from("<H", enc[order.index('G')*12:], 0)[0]
        if sp==0 or sp>1300: return None
        return Mon(head, False)

    # ---- write a party mon back ----
    def write_party_mon(self, index, mon):
        off=PARTY_OFF+index*PARTY_MON_SIZE
        self._write_sb1(off, mon.encode())
    def write_box_mon(self, storage_off, mon):
        self._write_storage(storage_off, mon.encode())

    def trainer(self):
        from_ = self.sections.get(0)
        name = decode_str(self.data[from_:from_+7]) if from_ is not None else "?"
        gender = self.data[from_+0x08] if from_ is not None else 0
        tid, sid = struct.unpack_from("<HH", self.data, from_+0x0A) if from_ is not None else (0,0)
        return dict(name=name, gender=("F" if gender else "M"), tid=tid, sid=sid)

    def save_as(self, out_path):
        with open(out_path,"wb") as f: f.write(self.data)
