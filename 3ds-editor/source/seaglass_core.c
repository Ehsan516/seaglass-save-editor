/* seaglass_core.c -- C port of seaglass_save.py (validated against it). */
#include "seaglass_core.h"
#include "g3_tables.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* ---------------- constants (mirror seaglass_save.py) ---------------- */
#define SIGNATURE      0x08012025u
#define SECTION_SIZE   0x1000
#define DATA_SIZE      0xF80
#define FOOTER         0xFF4
#define PARTY_COUNT_OFF 0x234
#define PARTY_OFF      0x238
#define SHEDINJA       292
#define SHINY_THRESH   8
#define MOVE_INFO_STRIDE 0x38
#define ITEM_STRIDE    0x54
#define ABILITY_STRIDE 0x1c

static const int SB1_IDS[4]     = {1,2,3,4};
static const int STORAGE_IDS[9] = {5,6,7,8,9,10,11,12,13};

/* ---------------- state ---------------- */
static uint8_t  g_sav[SAVE_SIZE];
static long     g_sav_len = 0;
static uint8_t *g_rom = NULL;
static long     g_rom_len = 0;
static int      g_sec[14];          /* section id -> file offset (-1 none) */
static int      g_slot = -1;

bool sg_rom_ok = false;
static uint32_t off_name_sp1 = 0x8f087c, off_stride = 0xD0;
static uint32_t off_move_ptr1 = 0x6d2a18, off_item = 0x67e77c, off_abil = 0x6e15b0;

uint16_t sg_species_ids[MAX_LIST]; int sg_species_n = 0;
uint16_t sg_move_ids[1000];        int sg_move_n = 0;
uint16_t sg_item_ids[1300];        int sg_item_n = 0;
uint16_t sg_move_sorted[1000];     int sg_move_sn = 0;
uint16_t sg_item_sorted[1300];     int sg_item_sn = 0;
uint16_t sg_species_sorted[MAX_LIST]; int sg_species_sn = 0;

/* ---------------- little-endian helpers ---------------- */
static uint16_t r16(const uint8_t *p){ return p[0] | (p[1]<<8); }
static uint32_t r32(const uint8_t *p){ return p[0]|(p[1]<<8)|(p[2]<<16)|((uint32_t)p[3]<<24); }
static void w16(uint8_t *p, uint16_t v){ p[0]=v&0xFF; p[1]=v>>8; }
static void w32(uint8_t *p, uint32_t v){ p[0]=v; p[1]=v>>8; p[2]=v>>16; p[3]=v>>24; }

/* ---------------- charset ---------------- */
static void dec_str(const uint8_t *b, int maxlen, char *out, size_t n){
    size_t o=0;
    for(int i=0;i<maxlen;i++){
        uint8_t c=b[i]; if(c==0xFF) break;
        const char *s=G3_DEC[c];
        size_t l=strlen(s);
        if(o+l+1>=n) break;
        memcpy(out+o,s,l); o+=l;
    }
    while(o>0 && out[o-1]==' ') o--;    /* rstrip */
    out[o]=0;
}
static int enc_ascii(const char *s, uint8_t *out, int max){ /* for ROM search */
    int n=0;
    for(;*s && n<max; s++){
        unsigned c=(unsigned char)*s;
        if(c>=128) return -1;
        uint8_t e=G3_ENC[c];
        if(e==0 && c!=' ') return -1;
        out[n++]=e;
    }
    return n;
}
static void enc_name(const char *s, uint8_t *out, int field){ /* nickname etc */
    int i=0;
    for(; s[i] && i<field; i++){
        unsigned c=(unsigned char)s[i];
        uint8_t e=(c<128)?G3_ENC[c]:0;
        out[i]=(e==0 && c!=' ')?0x00:e;
    }
    if(i<field) out[i++]=0xFF;
    while(i<field) out[i++]=0xFF;
}

/* ---------------- save sections ---------------- */
static uint16_t section_checksum(const uint8_t *sec){
    uint32_t s=0;
    for(int i=0;i<DATA_SIZE;i+=4) s+=r32(sec+i);
    return (uint16_t)((s>>16)+(s&0xFFFF));
}
static void fix_section(int sid){
    int off=g_sec[sid];
    if(off<0) return;
    w16(g_sav+off+FOOTER+2, section_checksum(g_sav+off));
}
bool sg_load_save(const char *path){
    FILE *f=fopen(path,"rb"); if(!f) return false;
    g_sav_len=fread(g_sav,1,SAVE_SIZE,f); fclose(f);
    if(g_sav_len < 0xE000) return false;
    uint32_t best_ctr=0; int best=-1;
    int slot_sec[2][14];
    for(int s=0;s<2;s++){
        long base = s? 0xE000:0x0000;
        if(base+14*SECTION_SIZE > g_sav_len) continue;
        uint32_t ctr=0; int found=0;
        for(int i=0;i<14;i++) slot_sec[s][i]=-1;
        for(int i=0;i<14;i++){
            long off=base+i*SECTION_SIZE;
            if(r32(g_sav+off+FOOTER+4)!=SIGNATURE) continue;
            int sid=r16(g_sav+off+FOOTER);
            if(sid<0||sid>13) continue;
            slot_sec[s][sid]=off; found++;
            ctr=r32(g_sav+off+FOOTER+8);
        }
        if(found==14 && (best<0 || ctr>=best_ctr)){ best=s; best_ctr=ctr; }
    }
    if(best<0) return false;
    g_slot=best;
    memcpy(g_sec, slot_sec[best], sizeof g_sec);
    return true;
}
bool sg_write_save(const char *path){
    FILE *f=fopen(path,"wb"); if(!f) return false;
    size_t w=fwrite(g_sav,1,g_sav_len,f); fclose(f);
    return w==(size_t)g_sav_len;
}
int sg_active_slot(void){ return g_slot; }
void sg_trainer_name(char *out, size_t n){
    if(g_sec[0]>=0) dec_str(g_sav+g_sec[0],7,out,n); else snprintf(out,n,"?");
}

/* SaveBlock1 / storage virtual offsets -> file bytes */
static uint8_t sb1_read8(uint32_t o){
    int sid=SB1_IDS[o/DATA_SIZE]; return g_sav[g_sec[sid]+o%DATA_SIZE];
}
static void sb1_write(uint32_t o, const uint8_t *src, int n){
    int touched[4]={0,0,0,0};
    for(int j=0;j<n;j++){
        uint32_t v=o+j; int seg=v/DATA_SIZE; int sid=SB1_IDS[seg];
        g_sav[g_sec[sid]+v%DATA_SIZE]=src[j]; touched[seg]=1;
    }
    for(int s=0;s<4;s++) if(touched[s]) fix_section(SB1_IDS[s]);
}
static uint8_t st_read8(uint32_t o){
    int sid=STORAGE_IDS[o/DATA_SIZE]; return g_sav[g_sec[sid]+o%DATA_SIZE];
}
static void st_write(uint32_t o, const uint8_t *src, int n){
    int touched[9]={0};
    for(int j=0;j<n;j++){
        uint32_t v=o+j; int seg=v/DATA_SIZE; int sid=STORAGE_IDS[seg];
        g_sav[g_sec[sid]+v%DATA_SIZE]=src[j]; touched[seg]=1;
    }
    for(int s=0;s<9;s++) if(touched[s]) fix_section(STORAGE_IDS[s]);
}
static void sb1_read(uint32_t o, uint8_t *dst, int n){ for(int j=0;j<n;j++) dst[j]=sb1_read8(o+j); }
static void st_read (uint32_t o, uint8_t *dst, int n){ for(int j=0;j<n;j++) dst[j]=st_read8(o+j); }

/* ---------------- mon decode/encode ---------------- */
static bool mon_decode(const uint8_t *raw, int len, bool party, sg_mon *m){
    memset(m,0,sizeof *m);
    memcpy(m->head,raw,0x20);
    m->pv=r32(raw); m->otid=r32(raw+4); m->is_party=party;
    if(party && len>=100) memcpy(m->tail, raw+0x50, 0x14);
    uint32_t key=m->pv^m->otid;
    uint8_t blk[48];
    for(int i=0;i<48;i+=4) w32(blk+i, r32(raw+0x20+i)^key);
    uint32_t chk=0; for(int i=0;i<48;i+=2) chk+=r16(blk+i);
    if((chk&0xFFFF)!=r16(raw+0x1C)) return false;
    const unsigned char *p=SUB_POS[m->pv%24];
    memcpy(m->G, blk+p[0]*12, 12); memcpy(m->A, blk+p[1]*12, 12);
    memcpy(m->E, blk+p[2]*12, 12); memcpy(m->M, blk+p[3]*12, 12);
    return true;
}
static int mon_encode(const sg_mon *m, uint8_t *out){ /* returns length */
    memcpy(out,m->head,0x20);
    w32(out,m->pv); w32(out+4,m->otid);
    uint8_t blk[48];
    const unsigned char *p=SUB_POS[m->pv%24];
    memcpy(blk+p[0]*12,m->G,12); memcpy(blk+p[1]*12,m->A,12);
    memcpy(blk+p[2]*12,m->E,12); memcpy(blk+p[3]*12,m->M,12);
    uint32_t chk=0; for(int i=0;i<48;i+=2) chk+=r16(blk+i);
    w16(out+0x1C, chk&0xFFFF);
    uint32_t key=m->pv^m->otid;
    for(int i=0;i<48;i+=4) w32(out+0x20+i, r32(blk+i)^key);
    if(m->is_party){ memcpy(out+0x50,m->tail,0x14); return 100; }
    return 80;
}

int sg_party_count(void){
    uint8_t b[4]; sb1_read(PARTY_COUNT_OFF,b,4);
    uint32_t c=r32(b); return c>6?6:(int)c;
}
bool sg_get_party_mon(int i, sg_mon *m){
    uint8_t raw[100]; sb1_read(PARTY_OFF+i*100,raw,100);
    if(!mon_decode(raw,100,true,m)) return false;
    m->party_index=i; return true;
}
int sg_box_scan(uint32_t *offs, int max){
    int n=0;
    uint32_t total=9*DATA_SIZE;
    for(uint32_t o=4; o+80<=total && n<max; o+=80){
        uint8_t raw[80]; st_read(o,raw,80);
        sg_mon t;
        if(!mon_decode(raw,80,false,&t)) continue;
        uint16_t sp=r16(t.G);
        if(sp==0 || sp>1300) continue;
        offs[n++]=o;
    }
    return n;
}
bool sg_get_box_mon(uint32_t off, sg_mon *m){
    uint8_t raw[80]; st_read(off,raw,80);
    if(!mon_decode(raw,80,false,m)) return false;
    m->storage_off=off; return true;
}
void sg_commit_mon(const sg_mon *m){
    uint8_t out[100];
    int n=mon_encode(m,out);
    if(m->is_party) sb1_write(PARTY_OFF+m->party_index*100,out,n);
    else            st_write(m->storage_off,out,n);
}

/* ---------------- mon field access ---------------- */
uint16_t mon_species(const sg_mon*m){ return r16(m->G); }
void mon_set_species(sg_mon*m,uint16_t v){ w16(m->G,v); }
uint16_t mon_item(const sg_mon*m){ return r16(m->G+2); }
void mon_set_item(sg_mon*m,uint16_t v){ w16(m->G+2,v); }
uint32_t mon_exp(const sg_mon*m){ return r32(m->G+4); }
uint8_t mon_friendship(const sg_mon*m){ return m->G[9]; }
void mon_set_friendship(sg_mon*m,uint8_t v){ m->G[9]=v; }
uint16_t mon_move(const sg_mon*m,int i){ return r16(m->A+i*2); }
void mon_set_move(sg_mon*m,int i,uint16_t v){ w16(m->A+i*2,v); }
uint8_t mon_pp(const sg_mon*m,int i){ return m->A[8+i]; }
void mon_set_pp(sg_mon*m,int i,uint8_t v){ m->A[8+i]=v; }
uint8_t mon_pp_up(const sg_mon*m,int i){ return (m->G[8]>>(2*i))&3; }
void mon_set_pp_up(sg_mon*m,int i,uint8_t v){
    uint8_t b=m->G[8]; b &= ~(3<<(2*i)); b |= (v&3)<<(2*i); m->G[8]=b;
}
uint8_t mon_ev(const sg_mon*m,int i){ return m->E[i]; }
void mon_set_ev(sg_mon*m,int i,uint8_t v){ m->E[i]=v; }
uint8_t mon_contest(const sg_mon*m,int i){ return m->E[6+i]; }
void mon_set_contest(sg_mon*m,int i,uint8_t v){ m->E[6+i]=v; }
uint8_t mon_pokerus_strain(const sg_mon*m){ return (m->M[0]>>4)&0xF; }
uint8_t mon_pokerus_days(const sg_mon*m){ return m->M[0]&0xF; }
void mon_set_pokerus(sg_mon*m,uint8_t strain,uint8_t days){ m->M[0]=((strain&0xF)<<4)|(days&0xF); }
uint8_t mon_met_location(const sg_mon*m){ return m->M[1]; }
void mon_set_met_location(sg_mon*m,uint8_t v){ m->M[1]=v; }
static uint16_t origins(const sg_mon*m){ return r16(m->M+2); }
static void set_origins(sg_mon*m,uint16_t mask,int shift,uint16_t v){
    uint16_t w=origins(m); w &= ~(uint16_t)(mask<<shift); w |= (uint16_t)((v&mask)<<shift);
    w16(m->M+2,w);
}
uint8_t mon_met_level(const sg_mon*m){ return origins(m)&0x7F; }
void mon_set_met_level(sg_mon*m,uint8_t v){ set_origins(m,0x7F,0,v); }
uint8_t mon_origin_game(const sg_mon*m){ return (origins(m)>>7)&0xF; }
void mon_set_origin_game(sg_mon*m,uint8_t v){ set_origins(m,0xF,7,v); }
uint8_t mon_poke_ball(const sg_mon*m){ return (origins(m)>>11)&0xF; }
void mon_set_poke_ball(sg_mon*m,uint8_t v){ set_origins(m,0xF,11,v); }
int mon_ot_gender(const sg_mon*m){ return (origins(m)>>15)&1; }
void mon_set_ot_gender(sg_mon*m,int v){ set_origins(m,1,15,v?1:0); }
static uint32_t ivw(const sg_mon*m){ return r32(m->M+4); }
uint8_t mon_iv(const sg_mon*m,int i){ return (ivw(m)>>(5*i))&31; }
void mon_set_iv(sg_mon*m,int i,uint8_t v){
    uint32_t w=ivw(m); w &= ~(31u<<(5*i)); w |= (uint32_t)(v&31)<<(5*i);
    w32(m->M+4,w);
}
int mon_ability_slot(const sg_mon*m){ return (ivw(m)>>31)&1; }
void mon_set_ability_slot(sg_mon*m,int v){
    uint32_t w=ivw(m)&0x7FFFFFFFu; if(v) w|=0x80000000u; w32(m->M+4,w);
}
int mon_nature(const sg_mon*m){ return m->pv%25; }
bool mon_is_egg(const sg_mon*m){ return (ivw(m)>>30)&1; }
bool mon_shiny(const sg_mon*m){
    return (((m->otid&0xFFFF)^(m->otid>>16)^(m->pv&0xFFFF)^(m->pv>>16)) < SHINY_THRESH);
}
int mon_level(const sg_mon*m){ return m->is_party ? m->tail[0x04] : 0; }
void mon_nickname(const sg_mon*m,char*out,size_t n){ dec_str(m->head+0x08,10,out,n); }
void mon_set_nickname(sg_mon*m,const char*s){ enc_name(s,m->head+0x08,10); }
uint8_t sg_gender_ratio(uint16_t sp);
char mon_gender(const sg_mon*m){
    uint8_t r = g_rom ? sg_gender_ratio(mon_species(m)) : 255;
    if(r==255) return 'N';
    if(r==254) return 'F';
    if(r==0)   return 'M';
    return ((m->pv&0xFF)<r)?'F':'M';
}

/* ---------------- experience / growth ---------------- */
static uint32_t exp_at(int g,int n){
    if(n<=1) return 0;
    long long n3=(long long)n*n*n;
    switch(g){
    case 0: return (uint32_t)n3;
    case 4: return (uint32_t)(4*n3/5);
    case 5: return (uint32_t)(5*n3/4);
    case 3: return (uint32_t)((6*n3)/5 - 15LL*n*n + 100LL*n - 140);
    case 1:
        if(n<=50)  return (uint32_t)(n3*(100-n)/50);
        if(n<=68)  return (uint32_t)(n3*(150-n)/100);
        if(n<=98)  return (uint32_t)(n3*((1911-10*n)/3)/500);
        return (uint32_t)(n3*(160-n)/100);
    case 2:
        if(n<=15)  return (uint32_t)(n3*(((n+1)/3)+24)/50);
        if(n<=36)  return (uint32_t)(n3*(n+14)/50);
        return (uint32_t)(n3*((n/2)+32)/50);
    }
    return (uint32_t)n3;
}
static int detect_growth(uint32_t exp,int level){
    long long bestd=-1; int bestg=0;
    for(int g=0; g<6; g++){
        uint32_t lo=exp_at(g,level);
        uint32_t hi=(level<100)?exp_at(g,level+1):lo+1;
        if(lo<=exp && exp<hi) return g;
        long long d=(long long)exp-lo; if(d<0)d=-d;
        if(bestd<0 || d<bestd){ bestd=d; bestg=g; }
    }
    return bestg;
}
void sg_set_level(sg_mon*m,int level){
    if(level<1)level=1;
    if(level>100)level=100;
    int cur=m->tail[0x04]; if(cur<1)cur=1;
    int g=detect_growth(mon_exp(m),cur);
    w32(m->G+4, exp_at(g,level));
    m->tail[0x04]=(uint8_t)level;
}

/* ---------------- ROM ---------------- */
static long rom_find(const uint8_t *pat,int n,long start){
    if(!g_rom||n<=0) return -1;
    for(long i=start; i+n<=g_rom_len; i++)
        if(g_rom[i]==pat[0] && !memcmp(g_rom+i,pat,n)) return i;
    return -1;
}
static uint32_t name_addr(uint16_t sp){ return off_name_sp1 + (uint32_t)(sp-1)*off_stride; }
void sg_species_name(uint16_t sp,char*out,size_t n){
    if(!g_rom||sp==0){ snprintf(out,n,"#%u",sp); return; }
    uint32_t a=name_addr(sp);
    if(a+12>(uint32_t)g_rom_len){ snprintf(out,n,"#%u",sp); return; }
    dec_str(g_rom+a,12,out,n);
    if(!out[0]) snprintf(out,n,"#%u",sp);
}
void sg_base_stats(uint16_t sp,uint8_t out[6]){
    uint32_t a=name_addr(sp)-0x2c;
    for(int i=0;i<6;i++) out[i]=g_rom?g_rom[a+i]:0;
}
uint8_t sg_gender_ratio(uint16_t sp){
    uint32_t a=name_addr(sp)-0x2c+0x12;
    return g_rom?g_rom[a]:255;
}
void sg_species_abilities(uint16_t sp,uint16_t*a0,uint16_t*a1){
    uint32_t b=name_addr(sp)-0x2c+0x18;
    *a0=g_rom?r16(g_rom+b):0; *a1=g_rom?r16(g_rom+b+2):0;
}
void sg_move_name(uint16_t mv,char*out,size_t n){
    if(mv==0){ snprintf(out,n,"-"); return; }
    if(!g_rom){ snprintf(out,n,"move%u",mv); return; }
    uint32_t p=off_move_ptr1+(uint32_t)(mv-1)*MOVE_INFO_STRIDE;
    if(p+4>(uint32_t)g_rom_len){ snprintf(out,n,"move%u",mv); return; }
    uint32_t ptr=r32(g_rom+p);
    if(ptr<0x08000000u||ptr>=0x0A000000u){ snprintf(out,n,"move%u",mv); return; }
    dec_str(g_rom+ptr-0x08000000u,16,out,n);
    if(!out[0]) snprintf(out,n,"move%u",mv);
}
void sg_item_name(uint16_t it,char*out,size_t n){
    if(it==0){ snprintf(out,n,"(none)"); return; }
    if(!g_rom){ snprintf(out,n,"item%u",it); return; }
    dec_str(g_rom+off_item+(uint32_t)it*ITEM_STRIDE,14,out,n);
    if(!out[0]) snprintf(out,n,"item%u",it);
}
void sg_ability_name(uint16_t ab,char*out,size_t n){
    if(ab==0){ snprintf(out,n,"(none)"); return; }
    if(!g_rom){ snprintf(out,n,"abil%u",ab); return; }
    dec_str(g_rom+off_abil+(uint32_t)ab*ABILITY_STRIDE,13,out,n);
    if(!out[0]) snprintf(out,n,"abil%u",ab);
}

/* species-list filter (port of _looks_like_species, ASCII approximation) */
static bool looks_like_species(const char *s){
    int len=strlen(s);
    if(len<3) return false;
    if(!(s[0]>='A'&&s[0]<='Z')) return false;
    bool vowel=false; int alpha=0; char first=0; bool allsame=true;
    for(int i=0;i<len;i++){
        char c=s[i];
        bool ok = (c>='A'&&c<='Z')||(c>='a'&&c<='z')||(c>='0'&&c<='9')||
                  c==' '||c=='-'||c=='.'||c=='\''||c==':'||(unsigned char)c>=128;
        if(!ok) return false;
        if(strchr("aeiouyAEIOUY",c)||(unsigned char)c>=128) vowel=true;
        if((c>='A'&&c<='Z')||(c>='a'&&c<='z')){
            char lc = (c>='A')&&(c<='Z') ? c+32 : c;
            if(!alpha) first=lc; else if(lc!=first) allsame=false;
            alpha++;
        }
        if(c>='0'&&c<='9'&&i!=len-1) return false;
    }
    if(!vowel||allsame||alpha==0) return false;
    /* no single-letter tokens */
    int tl=0;
    for(int i=0;i<=len;i++){
        if(i==len||s[i]==' '){ if(tl==1) return false; tl=0; }
        else tl++;
    }
    return true;
}
static bool clean_name(const char *s){
    char c0=s[0];   /* real names start alnum, have letters+a vowel, no junk symbols */
    if(!((c0>='A'&&c0<='Z')||(c0>='a'&&c0<='z')||(c0>='0'&&c0<='9'))) return false;
    if(c0>='0'&&c0<='9'){                 /* digits must form a standalone number */
        const char *p=s; while(*p>='0'&&*p<='9') p++;
        if(*p!=' '&&*p!=0) return false;
    }
    int letters=0, vowels=0; char firstl=0; bool allsame=true;
    for(const char *p=s; *p; p++){
        if(*p=='/'||*p=='!'||*p=='?') return false;
        if(*p==' '&&p[1]==' ') return false;      /* no double spaces */
        if((*p>='A'&&*p<='Z')||(*p>='a'&&*p<='z')){
            char lc = (*p>='A'&&*p<='Z')? *p+32 : *p;
            letters++;
            if(!firstl) firstl=lc; else if(lc!=firstl) allsame=false;
            if(strchr("aeiouy",lc)) vowels++;
        }
    }
    return letters>=2 && vowels>=1 && !allsame;
}
static void build_sorted(void);
static uint32_t exp_at(int g,int n);
static void build_lists(void){
    char nm[24];
    sg_species_n=sg_move_n=sg_item_n=0;
    if(!sg_rom_ok) return;
    for(uint16_t sp=1; sp<1600; sp++){
        sg_species_name(sp,nm,sizeof nm);
        if(looks_like_species(nm)) sg_species_ids[sg_species_n++]=sp;
    }
    for(uint16_t mv=1; mv<1000 && sg_move_n<1000; mv++){
        sg_move_name(mv,nm,sizeof nm);
        if(clean_name(nm) && strncmp(nm,"move",4)!=0) sg_move_ids[sg_move_n++]=mv;
    }
    for(uint16_t it=1; it<1300 && sg_item_n<1300; it++){
        sg_item_name(it,nm,sizeof nm);
        if(clean_name(nm) && strncmp(nm,"item",4)!=0) sg_item_ids[sg_item_n++]=it;
    }
    build_sorted();
}

/* insertion-sort ids by their name (small n, once at load) */
static void sort_ids(uint16_t *dst,int n,void(*namef)(uint16_t,char*,size_t)){
    for(int i=0;i<n;i++){
        uint16_t id=dst[i]; char a[24]; namef(id,a,sizeof a);
        int j=i-1;
        while(j>=0){ char b[24]; namef(dst[j],b,sizeof b);
            if(strcasecmp(b,a)<=0) break; dst[j+1]=dst[j]; j--; }
        dst[j+1]=id;
    }
}
static void build_sorted(void){
    sg_move_sn=sg_move_n; memcpy(sg_move_sorted,sg_move_ids,sg_move_n*sizeof(uint16_t));
    sort_ids(sg_move_sorted,sg_move_sn,sg_move_name);
    sg_item_sn=sg_item_n; memcpy(sg_item_sorted,sg_item_ids,sg_item_n*sizeof(uint16_t));
    sort_ids(sg_item_sorted,sg_item_sn,sg_item_name);
    sg_species_sn=sg_species_n; memcpy(sg_species_sorted,sg_species_ids,sg_species_n*sizeof(uint16_t));
    sort_ids(sg_species_sorted,sg_species_sn,sg_species_name);
}

static int level_from_exp(uint32_t exp){
    for(int L=1; L<100; L++)
        for(int g=0; g<6; g++)
            if(exp_at(g,L)<=exp && exp<exp_at(g,L+1)) return L;
    return 100;
}
int sg_level_of(const sg_mon *m){
    if(m->is_party) return m->tail[0x04];
    return level_from_exp(mon_exp(m));
}
int sg_calc_stat(const sg_mon *m, int i){
    if(!g_rom) return 0;
    uint8_t base[6]; sg_base_stats(mon_species(m),base);
    int L=sg_level_of(m); if(L<1) L=1;
    int core=((2*base[i]+mon_iv(m,i)+mon_ev(m,i)/4)*L)/100;
    if(i==0) return (mon_species(m)==SHEDINJA)?1:core+L+10;
    int nb=mon_nature(m)/5, nl=mon_nature(m)%5, ni=i-1, v=core+5;
    if(nb!=nl){ if(ni==nb) v=v*110/100; else if(ni==nl) v=v*90/100; }
    return v;
}
static bool detect_offsets(void){
    uint8_t pat[24]; int n; bool ok=true;
    /* species: Bulbasaur validated by Ivysaur @+stride, Venusaur @+2*stride */
    n=enc_ascii("Bulbasaur",pat,sizeof pat);
    long pos=0; bool found=false;
    while(n>0){
        long b=rom_find(pat,n,pos); if(b<0) break;
        uint8_t iv[16]; int ivn=enc_ascii("Ivysaur",iv,sizeof iv);
        long i=rom_find(iv,ivn,b);
        if(i>b){
            long stride=i-b;
            if(stride>=0x80 && stride<=0x200){
                char v[16]; dec_str(g_rom+b+2*stride,8,v,sizeof v);
                if(!strcmp(v,"Venusaur")){ off_name_sp1=b; off_stride=stride; found=true; break; }
            }
        }
        pos=b+1;
    }
    if(!found) ok=false;
    /* moves: pointer to "Karate Chop"+terminator */
    n=enc_ascii("Karate Chop",pat,sizeof pat); pat[n]=0xFF;
    long kc=rom_find(pat,n+1,0);
    if(kc>=0){
        uint8_t pp[4]; w32(pp,0x08000000u|kc);
        long L=rom_find(pp,4,0);
        if(L>=0) off_move_ptr1=L-MOVE_INFO_STRIDE; else ok=false;
    } else ok=false;
    /* items: "Master Ball"+term (item 4); verify item1 = Poke Ball */
    n=enc_ascii("Master Ball",pat,sizeof pat); pat[n]=0xFF;
    pos=0; found=false;
    while(1){
        long mb=rom_find(pat,n+1,pos); if(mb<0) break;
        long cand=mb-4*ITEM_STRIDE;
        if(cand>=0){
            char t[16]; dec_str(g_rom+cand+ITEM_STRIDE,14,t,sizeof t);
            if(!strncmp(t,"Pok",3)){ off_item=cand; found=true; break; }
        }
        pos=mb+1;
    }
    if(!found) ok=false;
    /* abilities: "Overgrow"+terminator (id 65) */
    n=enc_ascii("Overgrow",pat,sizeof pat); pat[n]=0xFF;
    long og=rom_find(pat,n+1,0);
    if(og>=0) off_abil=og-65*ABILITY_STRIDE; else ok=false;
    /* verify */
    char a[24],b2[24],c[24],d[24];
    sg_species_name(1,a,sizeof a); sg_move_name(1,b2,sizeof b2);
    sg_item_name(1,c,sizeof c); sg_ability_name(65,d,sizeof d);
    if(strncmp(a,"Bulbas",6)||strcmp(b2,"Pound")||strncmp(c,"Pok",3)||strcmp(d,"Overgrow"))
        ok=false;
    return ok;
}
bool sg_load_rom(const char *path){
    FILE *f=fopen(path,"rb"); if(!f) return false;
    fseek(f,0,SEEK_END); g_rom_len=ftell(f); fseek(f,0,SEEK_SET);
    if(g_rom) free(g_rom);
    g_rom=malloc(g_rom_len);
    if(!g_rom){ fclose(f); g_rom_len=0; return false; }
    if(fread(g_rom,1,g_rom_len,f)!=(size_t)g_rom_len){ fclose(f); free(g_rom); g_rom=NULL; return false; }
    fclose(f);
    sg_rom_ok=detect_offsets();
    build_lists();
    return true;
}

/* ---------------- sprites (LZ77 front pic @+0x58, palettes +0x68/+0x70) --- */
static int lz77(uint32_t off, uint8_t *out, int maxout){
    if(off+4>(uint32_t)g_rom_len || g_rom[off]!=0x10) return -1;
    int size=g_rom[off+1]|(g_rom[off+2]<<8)|(g_rom[off+3]<<16);
    if(size<=0 || size>maxout) return -1;
    uint32_t p=off+4; int o=0;
    while(o<size){
        if(p>=(uint32_t)g_rom_len) return -1;
        uint8_t fl=g_rom[p++];
        for(int b=0;b<8 && o<size;b++){
            if(fl&(0x80>>b)){
                if(p+1>=(uint32_t)g_rom_len) return -1;
                uint8_t hi=g_rom[p], lo=g_rom[p+1]; p+=2;
                int n=(hi>>4)+3, disp=(((hi&0xF)<<8)|lo)+1;
                if(disp>o) return -1;
                for(int i=0;i<n && o<size;i++){ out[o]=out[o-disp]; o++; }
            } else {
                if(p>=(uint32_t)g_rom_len) return -1;
                out[o++]=g_rom[p++];
            }
        }
    }
    return size;
}
bool sg_sprite_rgba(uint16_t sp, bool shiny, uint8_t *out){
    static uint8_t tiles[4096], palb[64];
    if(!g_rom) return false;
    uint32_t base=name_addr(sp)-0x2c;
    if(base+0x78>(uint32_t)g_rom_len) return false;
    uint32_t pic =r32(g_rom+base+0x58);
    uint32_t palp=r32(g_rom+base+(shiny?0x70:0x68));
    if(pic<0x08000000u||palp<0x08000000u) return false;
    if(lz77(pic -0x08000000u,tiles,sizeof tiles) < 2048) return false;
    if(lz77(palp-0x08000000u,palb ,sizeof palb ) < 32)   return false;
    uint8_t pal[16][4];
    for(int i=0;i<16;i++){
        uint16_t c=palb[i*2]|(palb[i*2+1]<<8);
        pal[i][0]=(c&0x1F)*255/31; pal[i][1]=((c>>5)&0x1F)*255/31;
        pal[i][2]=((c>>10)&0x1F)*255/31; pal[i][3]=255;
    }
    pal[0][3]=0; pal[0][0]=pal[0][1]=pal[0][2]=0;
    for(int ty=0;ty<8;ty++) for(int tx=0;tx<8;tx++){
        const uint8_t *t=tiles+(ty*8+tx)*32;
        for(int row=0;row<8;row++) for(int col=0;col<4;col++){
            uint8_t byte=t[row*4+col];
            for(int half=0;half<2;half++){
                int ci = half? (byte>>4) : (byte&0xF);
                int px = tx*8+col*2+half, py=ty*8+row;
                memcpy(out+(py*64+px)*4, pal[ci], 4);
            }
        }
    }
    return true;
}

uint32_t sg_box_slot_off(int box, int slot){ return 4 + (uint32_t)(box*30+slot)*80; }

/* ---------------- stat recompute / reroll ---------------- */
void sg_recompute_stats(sg_mon*m){
    if(!m->is_party || !g_rom) return;
    uint8_t base[6]; sg_base_stats(mon_species(m),base);
    int L=m->tail[0x04];
    int nb=mon_nature(m)/5, nl=mon_nature(m)%5;
    int stats[6];
    for(int i=0;i<6;i++){
        int core=((2*base[i]+mon_iv(m,i)+mon_ev(m,i)/4)*L)/100;
        if(i==0) stats[0]=(mon_species(m)==SHEDINJA)?1:core+L+10;
        else{
            int v=core+5, ni=i-1;
            if(nb!=nl){ if(ni==nb) v=v*110/100; else if(ni==nl) v=v*90/100; }
            stats[i]=v;
        }
    }
    w16(m->tail+0x06,stats[0]); w16(m->tail+0x08,stats[0]);   /* hp = maxhp */
    w16(m->tail+0x0A,stats[1]); w16(m->tail+0x0C,stats[2]);
    w16(m->tail+0x0E,stats[3]); w16(m->tail+0x10,stats[4]); w16(m->tail+0x12,stats[5]);
}
static uint32_t rnd32(void){
    return ((uint32_t)(rand()&0xFFFF)<<16) | (uint32_t)(rand()&0xFFFF);
}
void sg_reroll_pv(sg_mon*m,int nature,int shiny,char gender){
    if(nature<0) nature=mon_nature(m);
    if(shiny<0)  shiny=mon_shiny(m)?1:0;
    uint8_t ratio=g_rom?sg_gender_ratio(mon_species(m)):255;
    bool fixed=(ratio==0||ratio==254||ratio==255);
    if(gender==0 && !fixed) gender=mon_gender(m);
    for(long tries=0; tries<400000; tries++){
        uint32_t pv=rnd32();
        if((int)(pv%25)!=nature) continue;
        bool s=(((m->otid&0xFFFF)^(m->otid>>16)^(pv&0xFFFF)^(pv>>16))<SHINY_THRESH);
        if(s!=(bool)shiny) continue;
        if(!fixed && gender){
            char gg=((pv&0xFF)<ratio)?'F':'M';
            if(gg!=gender) continue;
        }
        m->pv=pv; return;
    }
}
