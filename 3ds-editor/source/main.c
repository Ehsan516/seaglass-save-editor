/* Seaglass Save Editor -- 3DS, PKSM-style dual-screen UI (citro2d).
 *
 * Top screen:    live summary of the selected Pokemon (sprite, stats, moves)
 * Bottom screen: party strip + 14 PC boxes as a 6x5 grid, touch or D-pad;
 *                edit panel with touch arrows, keyboard nickname, SAVE button.
 *
 * Workflow (no PC): GodMode9 dump -> edit here -> START/SAVE -> GodMode9 inject.
 */
#include <3ds.h>
#include <citro2d.h>
#include <stdio.h>
#include <stdarg.h>
#include <string.h>
#include <stdlib.h>
#include <dirent.h>
#include <time.h>
#include "seaglass_core.h"
#include "g3_tables.h"

#define MAX_FILES 64
#define PATHLEN   256
#define BOXES     14
#define BOX_COLS  6
#define BOX_ROWS  5

/* ---------------- theme ---------------- */
#define C(r,g,b) C2D_Color32(r,g,b,255)
static u32 COL_BG_TOP, COL_BG_BOT, COL_PANEL, COL_PANEL2, COL_ACCENT,
           COL_TEXT, COL_DIM, COL_SEL, COL_SHINY, COL_WARN, COL_EMPTY;
/* 0 = teal (flat neutral dark, matches the desktop redo's default), 1 = emerald
   (the original Seaglass palette, kept as a togglable option). SELECT swaps it. */
static int g_theme=0;
static void apply_palette(int theme){
    if(theme==0){
        COL_BG_TOP=C(11,11,13);   COL_BG_BOT=C(9,9,11);
        COL_PANEL =C(21,21,23);   COL_PANEL2=C(17,17,19);
        COL_ACCENT=C(20,184,166); COL_TEXT  =C(242,242,243);
        COL_DIM   =C(143,143,151);COL_SEL   =C(15,118,110);
        COL_SHINY =C(240,200,60); COL_WARN  =C(240,170,60);
        COL_EMPTY =C(17,17,19);
    } else {
        COL_BG_TOP=C(14,36,32);  COL_BG_BOT=C(12,30,27);
        COL_PANEL =C(24,58,52);  COL_PANEL2=C(20,48,43);
        COL_ACCENT=C(64,200,150);COL_TEXT  =C(226,240,235);
        COL_DIM   =C(140,170,160);COL_SEL  =C(38,110,88);
        COL_SHINY =C(240,200,60);COL_WARN  =C(240,170,60);
        COL_EMPTY =C(17,40,36);
    }
}

/* ---------------- text ---------------- */
static C2D_TextBuf g_txtbuf;
static void draw_text(float x, float y, float sc, u32 col, const char *s){
    C2D_Text t;
    C2D_TextParse(&t, g_txtbuf, s);
    C2D_TextOptimize(&t);
    C2D_DrawText(&t, C2D_WithColor, x, y, 0.5f, sc, sc, col);
}
static void draw_textf(float x, float y, float sc, u32 col, const char *fmt, ...){
    char b[128]; va_list a; va_start(a,fmt); vsnprintf(b,sizeof b,fmt,a); va_end(a);
    draw_text(x,y,sc,col,b);
}
static void draw_text_c(float cx, float y, float sc, u32 col, const char *s){
    C2D_Text t; float w,h;
    C2D_TextParse(&t, g_txtbuf, s);
    C2D_TextOptimize(&t);
    C2D_TextGetDimensions(&t, sc, sc, &w, &h);
    C2D_DrawText(&t, C2D_WithColor, cx-w/2, y, 0.5f, sc, sc, col);
}

/* ---------------- sprite texture cache ---------------- */
typedef struct { uint32_t key; C3D_Tex tex; bool used; } SprSlot;
#define SPR_CACHE 96
static SprSlot g_spr[SPR_CACHE];
static Tex3DS_SubTexture g_subtex = {64,64, 0.0f,1.0f, 1.0f,0.0f};
static C3D_RenderTarget *g_top, *g_bot;

static u32 morton(u32 x,u32 y){
    u32 i=(x&7)|((y&7)<<8);
    i=(i^(i<<2))&0x1313; i=(i^(i<<1))&0x1515; i=(i|(i>>7))&0x3F;
    return i;
}
static bool spr_get(uint16_t sp, bool shiny, C2D_Image *img){
    uint32_t key=((uint32_t)sp<<1)|(shiny?1:0);
    for(int i=0;i<SPR_CACHE;i++)
        if(g_spr[i].used && g_spr[i].key==key){ img->tex=&g_spr[i].tex; img->subtex=&g_subtex; return true; }
    static uint8_t rgba[64*64*4];
    if(!sg_sprite_rgba(sp,shiny,rgba)) return false;
    int slot=-1;
    for(int i=0;i<SPR_CACHE;i++) if(!g_spr[i].used){ slot=i; break; }
    if(slot<0){ /* cache full: recycle everything */
        for(int i=0;i<SPR_CACHE;i++){ C3D_TexDelete(&g_spr[i].tex); g_spr[i].used=false; }
        slot=0;
    }
    if(!C3D_TexInit(&g_spr[slot].tex,64,64,GPU_RGBA8)) return false;
    C3D_TexSetFilter(&g_spr[slot].tex, GPU_NEAREST, GPU_NEAREST);
    u32 *dst=(u32*)g_spr[slot].tex.data;
    for(u32 y=0;y<64;y++) for(u32 x=0;x<64;x++){
        const uint8_t *p=rgba+(y*64+x)*4;
        u32 off = morton(x,y) + ((x&~7)*8) + ((y&~7)*64);
        dst[off]=((u32)p[0]<<24)|((u32)p[1]<<16)|((u32)p[2]<<8)|p[3];  /* RGBA -> ABGR word */
    }
    g_spr[slot].key=key; g_spr[slot].used=true;
    img->tex=&g_spr[slot].tex; img->subtex=&g_subtex;
    return true;
}
static void draw_mon_sprite(uint16_t sp, bool shiny, float x, float y, float scale){
    C2D_Image img;
    if(spr_get(sp,shiny,&img))
        C2D_DrawImageAt(img, x, y, 0.5f, NULL, scale, scale);
}

/* ---------------- file scanning ---------------- */
static char g_files[MAX_FILES][PATHLEN]; static int g_nfiles;
static void scan_dir(const char *dir, const char *ext){
    DIR *d=opendir(dir); if(!d) return;
    struct dirent *e;
    while((e=readdir(d)) && g_nfiles<MAX_FILES){
        const char *n=e->d_name; size_t l=strlen(n), xl=strlen(ext);
        if(l>xl && strcasecmp(n+l-xl,ext)==0)
            snprintf(g_files[g_nfiles++],PATHLEN,"%s/%s",dir,n);
    }
    closedir(d);
}
static bool find_rom(char *out){
    FILE *f=fopen("sdmc:/3ds/seaglass/rom.gba","rb");
    if(f){ fclose(f); strcpy(out,"sdmc:/3ds/seaglass/rom.gba"); return true; }
    g_nfiles=0;
    scan_dir("sdmc:/3ds/seaglass",".gba");
    scan_dir("sdmc:/roms/gba",".gba");
    scan_dir("sdmc:/gm9/out",".gba");
    scan_dir("sdmc:",".gba");
    if(g_nfiles){ strcpy(out,g_files[0]); return true; }
    return false;
}

/* ---------------- selection model ---------------- */
typedef struct {
    bool in_party;      /* party strip vs box grid          */
    int  pslot;         /* 0..5                              */
    int  box, bslot;    /* 0..13, 0..29                      */
} Sel;
static bool sel_load(const Sel *s, sg_mon *m){
    if(s->in_party){
        if(s->pslot>=sg_party_count()) return false;
        return sg_get_party_mon(s->pslot,m);
    }
    return sg_get_box_mon(sg_box_slot_off(s->box,s->bslot),m);
}

/* ---------------- keyboard ---------------- */
static void kb_text(const char *initial, char *out, int maxlen){
    SwkbdState kb;
    swkbdInit(&kb, SWKBD_TYPE_QWERTY, 2, maxlen);
    swkbdSetInitialText(&kb, initial);
    swkbdSetHintText(&kb, "Nickname");
    if(swkbdInputText(&kb, out, maxlen+1)!=SWKBD_BUTTON_CONFIRM)
        strcpy(out, initial);
}


/* ---------------- paged alphabetical picker (top screen) ---------------- */
#define PICK_ROWS 18
#define PICK_COLS 2
#define PICK_PER  (PICK_ROWS*PICK_COLS)
static uint16_t run_picker(const char *title, uint16_t *ids, int n,
                           void(*namef)(uint16_t,char*,size_t), uint16_t current){
    int sel=0;
    for(int i=0;i<n;i++) if(ids[i]==current){ sel=i; break; }
    while(aptMainLoop()){
        hidScanInput();
        u32 kd=hidKeysDown();
        touchPosition tp; hidTouchRead(&tp);
        int page=sel/PICK_PER, pages=(n+PICK_PER-1)/PICK_PER;
        if(kd&KEY_B) return 0xFFFF;
        if(kd&KEY_A) return ids[sel];
        if(kd&KEY_UP   && sel>0) sel--;
        if(kd&KEY_DOWN && sel<n-1) sel++;
        if(kd&(KEY_LEFT|KEY_L)){ sel-=PICK_PER; if(sel<0) sel=0; }
        if(kd&(KEY_RIGHT|KEY_R)){ sel+=PICK_PER; if(sel>=n) sel=n-1; }
        if(kd&KEY_TOUCH){                     /* tap a row on the bottom mini-list? no: bottom is hint */
        }
        page=sel/PICK_PER;

        C3D_FrameBegin(C3D_FRAME_SYNCDRAW);
        C2D_TextBufClear(g_txtbuf);
        C2D_TargetClear(g_top,COL_BG_TOP);
        C2D_SceneBegin(g_top);
        C2D_DrawRectSolid(0,0,0,400,20,COL_PANEL);
        char hdr[48]; snprintf(hdr,sizeof hdr,"%s   page %d/%d",title,page+1,pages);
        draw_text(6,3,0.5f,COL_ACCENT,hdr);
        char nm[32];
        for(int i=0;i<PICK_PER;i++){
            int idx=page*PICK_PER+i; if(idx>=n) break;
            int col=i/PICK_ROWS, row=i%PICK_ROWS;
            float x=6+col*198, y=24+row*11.6f;
            bool s=(idx==sel);
            if(s) C2D_DrawRectSolid(x-2,y,0,196,11,COL_SEL);
            namef(ids[idx],nm,sizeof nm);
            draw_textf(x,y,0.4f,s?COL_TEXT:COL_DIM,"%s",nm);
        }
        C2D_TargetClear(g_bot,COL_BG_BOT);
        C2D_SceneBegin(g_bot);
        namef(ids[sel],nm,sizeof nm);
        draw_text_c(160,90,0.6f,COL_TEXT,nm);
        draw_text_c(160,130,0.42f,COL_DIM,"Up/Down: move   L/R or Left/Right: page");
        draw_text_c(160,150,0.42f,COL_DIM,"A: select     B: cancel");
        C3D_FrameEnd(0);
    }
    return 0xFFFF;
}

/* ---------------- top screen: summary ---------------- */
static const char *STAT_NM[6]={"HP","Atk","Def","Spe","SpA","SpD"};
static void draw_top_summary(sg_mon *m, bool have, const char *banner){
    C2D_DrawRectSolid(0,0,0,400,240,COL_BG_TOP);
    C2D_DrawRectSolid(0,0,0,400,24,COL_PANEL);
    draw_text(8,4,0.55f,COL_ACCENT,"Seaglass Save Editor");
    if(banner) draw_text_c(200,228,0.42f,COL_WARN,banner);
    if(!have){ draw_text_c(200,110,0.5f,COL_DIM,"Empty slot"); return; }

    char nm[32],nick[32],val[40];
    uint16_t sp=mon_species(m);
    sg_species_name(sp,nm,sizeof nm);
    mon_nickname(m,nick,sizeof nick);

    /* left panel */
    C2D_DrawRectSolid(6,30,0,192,192,COL_PANEL);
    draw_mon_sprite(sp,mon_shiny(m),12,34,1.5f);
    draw_textf(112,38,0.55f,COL_TEXT,"%s",nm);
    draw_textf(112,56,0.45f,COL_DIM,"#%u  %c%s",sp,mon_gender(m),mon_is_egg(m)?" EGG":"");
    if(mon_shiny(m)) draw_text(112,72,0.45f,COL_SHINY,"SHINY");
    int y=134;
    draw_textf(14,y,0.45f,COL_TEXT,"'%s'",nick); y+=16;
    if(m->is_party){ draw_textf(14,y,0.45f,COL_TEXT,"Lv %d",mon_level(m)); y+=16; }
    draw_textf(14,y,0.45f,COL_TEXT,"Nature  %s",NATURES[mon_nature(m)]); y+=16;
    uint16_t a0,a1; sg_species_abilities(sp,&a0,&a1);
    sg_ability_name(mon_ability_slot(m)&&a1?a1:a0,val,sizeof val);
    draw_textf(14,y,0.45f,COL_TEXT,"Ability %s",val); y+=16;
    sg_item_name(mon_item(m),val,sizeof val);
    draw_textf(14,y,0.45f,COL_TEXT,"Item    %s",val);

    /* right: IV/EV table + moves */
    C2D_DrawRectSolid(204,30,0,190,110,COL_PANEL);
    draw_text(212,34,0.42f,COL_DIM,"      IV   EV");
    for(int i=0;i<6;i++)
        draw_textf(212,50+i*14,0.42f,COL_TEXT,"%-3s  %2d  %3d",
                   STAT_NM[i],mon_iv(m,i),mon_ev(m,i));
    C2D_DrawRectSolid(204,146,0,190,76,COL_PANEL);
    draw_text(212,150,0.42f,COL_DIM,"Moves");
    for(int i=0;i<4;i++){
        sg_move_name(mon_move(m,i),val,sizeof val);
        draw_textf(212,164+i*14,0.42f,COL_TEXT,"%s",val);
    }
}

/* ---------------- bottom: browser ---------------- */
/* layout: party strip y=0..40 (6 x 40px cells at x=40..280),
   box header y=44..64, grid 6x5 cells 40x32 y=66..226 */
static void draw_bottom_browser(const Sel *sel){
    C2D_DrawRectSolid(0,0,0,320,240,COL_BG_BOT);
    /* party strip */
    int pc=sg_party_count();
    sg_mon m;
    for(int i=0;i<6;i++){
        float x=40+i*40;
        bool s = sel->in_party && sel->pslot==i;
        C2D_DrawRectSolid(x+1,1,0,38,38,s?COL_SEL:COL_PANEL);
        if(i<pc && sg_get_party_mon(i,&m))
            draw_mon_sprite(mon_species(&m),mon_shiny(&m),x+3,3,0.53f);
    }
    draw_text(4,12,0.4f,COL_DIM,"PARTY");
    /* box header */
    C2D_DrawRectSolid(60,44,0,200,18,COL_PANEL2);
    char t[16]; snprintf(t,sizeof t,"BOX %d",sel->box+1);
    draw_text_c(160,46,0.45f,COL_TEXT,t);
    draw_text(44,46,0.5f,COL_ACCENT,"<");
    draw_text(268,46,0.5f,COL_ACCENT,">");
    /* grid */
    for(int r=0;r<BOX_ROWS;r++) for(int c2=0;c2<BOX_COLS;c2++){
        float x=40+c2*40, y=66+r*32;
        bool s = !sel->in_party && sel->bslot==r*BOX_COLS+c2;
        C2D_DrawRectSolid(x+1,y+1,0,38,30,s?COL_SEL:COL_EMPTY);
        if(sg_get_box_mon(sg_box_slot_off(sel->box,r*BOX_COLS+c2),&m))
            draw_mon_sprite(mon_species(&m),mon_shiny(&m),x+7,y-1,0.5f);
    }
    C2D_DrawRectSolid(0,228,0,320,12,COL_PANEL);
    draw_text_c(160,229,0.36f,COL_DIM,"A edit  L/R box  X save  B back  SELECT theme");
}

/* ---------------- bottom: edit panel ---------------- */
enum{ E_SPECIES,E_NICK,E_LEVEL,E_NATURE,E_GENDER,E_SHINY,E_ABIL,E_FRIEND,E_ITEM,
      E_MV0,E_MV1,E_MV2,E_MV3, E_ROWS };
enum{ S_IV0=0,S_IV5=5,S_EV0=6,S_EV5=11,S_ROWS=12 };
/* Contest stats, Pokerus, and met/origin data -- same fields the desktop editor
   added; too many rows for one screen, so they get their own page (see g_page). */
enum{ M_COOL,M_BEAUTY,M_CUTE,M_SMART,M_TOUGH,M_SHEEN,
      M_PKRS_STRAIN,M_PKRS_DAYS,M_METLV,M_METLOC,M_ORIGIN,M_BALL,M_OTGENDER, M_ROWS };
enum{ P_PP0,P_PP1,P_PP2,P_PP3,P_UP0,P_UP1,P_UP2,P_UP3, P_ROWS };
enum{ PAGE_FIELDS,PAGE_STATS,PAGE_MISC,PAGE_PP, PAGE_COUNT };
/* poke_ball / origin_game indices are vanilla-fixed, not expansion-renumbered
   (see seaglass_save.py BALL_NAMES / ORIGIN_GAME_NAMES, which this mirrors). */
static const char *BALL_NM[13]={"?","Master Ball","Ultra Ball","Great Ball","Poke Ball",
    "Safari Ball","Net Ball","Dive Ball","Nest Ball","Repeat Ball","Timer Ball","Luxury Ball","Premier Ball"};
static const char *ORIGIN_NM[16]={"?","Sapphire","Ruby","Emerald","FireRed","LeafGreen",
    "?","?","?","?","?","?","?","?","?","Colosseum/XD"};
static int  g_edit_sel=0;
static int  g_page=PAGE_FIELDS;
static bool g_dirty=false;

static int page_rows(int page){
    switch(page){ case PAGE_STATS: return S_ROWS; case PAGE_MISC: return M_ROWS;
                  case PAGE_PP: return P_ROWS; default: return E_ROWS; }
}
/* button label shows the page you'd switch TO, same convention as the
   desktop theme toggle */
static const char* page_next_label(void){
    switch(g_page){ case PAGE_FIELDS: return "STATS"; case PAGE_STATS: return "MISC";
                    case PAGE_MISC: return "PP"; default: return "FIELDS"; }
}

static int idx_of(uint16_t *ids,int n,uint16_t v){ for(int i=0;i<n;i++) if(ids[i]==v) return i; return -1; }
static uint16_t cyc(uint16_t *ids,int n,uint16_t cur,int d){
    if(!n) return cur;
    int i=idx_of(ids,n,cur); if(i<0) i=0; else i=((i+d)%n+n)%n;
    return ids[i];
}

static void misc_row_text(sg_mon *m,int row,char *lab,char *val){
    if(row<=M_SHEEN){
        static const char *NM[6]={"Cool","Beauty","Cute","Smart","Tough","Sheen"};
        strcpy(lab,NM[row]); snprintf(val,40,"%d",mon_contest(m,row)); return;
    }
    switch(row){
    case M_PKRS_STRAIN: strcpy(lab,"Pkrs Strain"); snprintf(val,40,"%d",mon_pokerus_strain(m)); return;
    case M_PKRS_DAYS:   strcpy(lab,"Pkrs Days");   snprintf(val,40,"%d",mon_pokerus_days(m)); return;
    case M_METLV:       strcpy(lab,"Met Level");   snprintf(val,40,"%d",mon_met_level(m)); return;
    case M_METLOC:      strcpy(lab,"Met Loc #");   snprintf(val,40,"%d",mon_met_location(m)); return;
    case M_ORIGIN:      strcpy(lab,"Origin Game"); snprintf(val,40,"%s",ORIGIN_NM[mon_origin_game(m)&0xF]); return;
    case M_BALL:        strcpy(lab,"Caught In");   snprintf(val,40,"%s",BALL_NM[mon_poke_ball(m)%13]); return;
    case M_OTGENDER:    strcpy(lab,"OT Gender");   snprintf(val,40,"%s",mon_ot_gender(m)?"Female":"Male"); return;
    }
}
static void misc_adjust(sg_mon *m,int row,int d,bool a_press){
    if(row<=M_SHEEN){
        if(d){ int v=mon_contest(m,row)+d; if(v<0)v=0; if(v>255)v=255; mon_set_contest(m,row,(uint8_t)v); }
        return;
    }
    switch(row){
    case M_PKRS_STRAIN: if(d){ int v=mon_pokerus_strain(m)+d; if(v<0)v=0; if(v>15)v=15;
                            mon_set_pokerus(m,(uint8_t)v,mon_pokerus_days(m)); } return;
    case M_PKRS_DAYS:    if(d){ int v=mon_pokerus_days(m)+d; if(v<0)v=0; if(v>15)v=15;
                            mon_set_pokerus(m,mon_pokerus_strain(m),(uint8_t)v); } return;
    case M_METLV:        if(d){ int v=mon_met_level(m)+d; if(v<0)v=0; if(v>100)v=100; mon_set_met_level(m,(uint8_t)v); } return;
    case M_METLOC:       if(d){ int v=mon_met_location(m)+d; if(v<0)v=0; if(v>255)v=255; mon_set_met_location(m,(uint8_t)v); } return;
    case M_ORIGIN:       if(d){ int v=((int)mon_origin_game(m)+d+16)%16; mon_set_origin_game(m,(uint8_t)v); } return;
    case M_BALL:         if(d){ int v=((int)mon_poke_ball(m)+d+13)%13; mon_set_poke_ball(m,(uint8_t)v); } return;
    case M_OTGENDER:     if(d||a_press) mon_set_ot_gender(m,!mon_ot_gender(m)); return;
    }
}
static void pp_row_text(sg_mon *m,int row,char *lab,char *val){
    if(row<4){ snprintf(lab,16,"PP %d",row+1); snprintf(val,40,"%d",mon_pp(m,row)); }
    else      { int i=row-4; snprintf(lab,16,"PP Up %d",i+1); snprintf(val,40,"%d",mon_pp_up(m,i)); }
}
static void pp_adjust(sg_mon *m,int row,int d,bool a_press){
    (void)a_press;
    if(row<4){ if(d){ int v=mon_pp(m,row)+d; if(v<0)v=0; if(v>99)v=99; mon_set_pp(m,row,(uint8_t)v); } }
    else      { int i=row-4; if(d){ int v=mon_pp_up(m,i)+d; if(v<0)v=0; if(v>3)v=3; mon_set_pp_up(m,i,(uint8_t)v); } }
}

static void edit_adjust(sg_mon *m,int row,int d,bool a_press){
    g_dirty=true;
    if(g_page==PAGE_FIELDS) switch(row){
    case E_SPECIES: if(d) mon_set_species(m,cyc(sg_species_ids,sg_species_n,mon_species(m),d)); break;
    case E_NICK: if(a_press){ char cur[32],nn[16]; mon_nickname(m,cur,sizeof cur);
                    kb_text(cur,nn,10); mon_set_nickname(m,nn);} break;
    case E_LEVEL: if(d&&m->is_party) sg_set_level(m,mon_level(m)+d); break;
    case E_NATURE: if(d) sg_reroll_pv(m,(mon_nature(m)+(d>0?1:-1)+25)%25,-1,0); break;
    case E_GENDER: if(d||a_press){ char g=mon_gender(m);
                    if(g=='M') sg_reroll_pv(m,-1,-1,'F');
                    else if(g=='F') sg_reroll_pv(m,-1,-1,'M'); } break;
    case E_SHINY: if(d||a_press) sg_reroll_pv(m,-1,mon_shiny(m)?0:1,0); break;
    case E_ABIL: if(d||a_press) mon_set_ability_slot(m,!mon_ability_slot(m)); break;
    case E_FRIEND: if(d){ int v=mon_friendship(m)+d; if(v<0)v=0; if(v>255)v=255;
                    mon_set_friendship(m,(uint8_t)v);} break;
    case E_ITEM: if(d) mon_set_item(m,cyc(sg_item_ids,sg_item_n,mon_item(m),d)); break;
    default: if(row>=E_MV0&&row<=E_MV3&&d)
                mon_set_move(m,row-E_MV0,cyc(sg_move_ids,sg_move_n,mon_move(m,row-E_MV0),d));
    }
    else if(g_page==PAGE_STATS){
        int stat=row/2; bool isEV=row&1;
        if(isEV){ int v=mon_ev(m,stat)+d; if(v<0)v=0; if(v>255)v=255; mon_set_ev(m,stat,(uint8_t)v); }
        else    { int v=mon_iv(m,stat)+d; if(v<0)v=0; if(v>31)v=31;  mon_set_iv(m,stat,(uint8_t)v); }
    }
    else if(g_page==PAGE_MISC) misc_adjust(m,row,d,a_press);
    else if(g_page==PAGE_PP)   pp_adjust(m,row,d,a_press);
}
static void edit_row_text(sg_mon *m,int row,char *lab,char *val){
    char b[40];
    if(g_page==PAGE_FIELDS) switch(row){
    case E_SPECIES: sg_species_name(mon_species(m),b,sizeof b);
        strcpy(lab,"Species"); snprintf(val,40,"%s",b); return;
    case E_NICK: mon_nickname(m,b,sizeof b);
        strcpy(lab,"Nickname"); snprintf(val,40,"%s  [A]",b); return;
    case E_LEVEL: strcpy(lab,"Level");
        snprintf(val,40, m->is_party?"%d":"(box)", mon_level(m)); return;
    case E_NATURE: strcpy(lab,"Nature"); snprintf(val,40,"%s",NATURES[mon_nature(m)]); return;
    case E_GENDER: strcpy(lab,"Gender"); snprintf(val,40,"%c",mon_gender(m)); return;
    case E_SHINY: strcpy(lab,"Shiny"); snprintf(val,40,"%s",mon_shiny(m)?"YES":"no"); return;
    case E_ABIL: { uint16_t a0,a1; sg_species_abilities(mon_species(m),&a0,&a1);
        sg_ability_name(mon_ability_slot(m)&&a1?a1:a0,b,sizeof b);
        strcpy(lab,"Ability"); snprintf(val,40,"%s",b); return; }
    case E_FRIEND: strcpy(lab,"Friendship"); snprintf(val,40,"%d",mon_friendship(m)); return;
    case E_ITEM: sg_item_name(mon_item(m),b,sizeof b);
        strcpy(lab,"Item"); snprintf(val,40,"%s",b); return;
    default: sg_move_name(mon_move(m,row-E_MV0),b,sizeof b);
        snprintf(lab,16,"Move %d",row-E_MV0+1); snprintf(val,40,"%s",b); return;
    }
    else if(g_page==PAGE_STATS){
        if(row<=S_IV5){ snprintf(lab,16,"IV %s",STAT_NM[row]); snprintf(val,40,"%d",mon_iv(m,row)); }
        else { int i=row-S_EV0; snprintf(lab,16,"EV %s",STAT_NM[i]); snprintf(val,40,"%d",mon_ev(m,i)); }
    }
    else if(g_page==PAGE_MISC) misc_row_text(m,row,lab,val);
    else if(g_page==PAGE_PP)   pp_row_text(m,row,lab,val);
}
/* buttons along the bottom of the edit panel */
typedef struct{ float x,w; const char *lab; } Btn;
static const Btn BTNS[4]={{4,74,"STATS"},{82,74,"MAX IV"},{160,74,"SAVE"},{238,78,"BACK"}};


/* live stat total helper for the bottom editor's totals column */
static void draw_bottom_stats(sg_mon *m){
    C2D_DrawRectSolid(0,0,0,320,240,COL_BG_BOT);
    C2D_DrawRectSolid(0,0,0,320,18,COL_PANEL2);
    draw_text(8,3,0.44f,COL_ACCENT,"Edit IV / EV");
    draw_text(150,3,0.4f,COL_DIM,"IV");
    draw_text(238,3,0.4f,COL_DIM,"EV");
    float rh=28; 
    for(int i=0;i<6;i++){
        float y=22+i*rh;
        int selIV = (g_edit_sel==i*2), selEV=(g_edit_sel==i*2+1);
        C2D_DrawRectSolid(2,y,0,316,rh-2,(i&1)?COL_PANEL:COL_PANEL2);
        draw_text(8,y+7,0.5f,COL_TEXT,STAT_NM[i]);
        draw_textf(52,y+7,0.5f,COL_ACCENT,"%d",sg_calc_stat(m,i));   /* live total */
        /* IV field */
        C2D_DrawRectSolid(128,y+2,0,84,rh-6,selIV?COL_SEL:COL_EMPTY);
        draw_text(132,y+7,0.5f,COL_ACCENT,"-");
        draw_textf(156,y+7,0.5f,COL_TEXT,"%2d",mon_iv(m,i));
        draw_text(198,y+7,0.5f,COL_ACCENT,"+");
        /* EV field */
        C2D_DrawRectSolid(216,y+2,0,92,rh-6,selEV?COL_SEL:COL_EMPTY);
        draw_text(220,y+7,0.5f,COL_ACCENT,"-");
        draw_textf(244,y+7,0.5f,COL_TEXT,"%3d",mon_ev(m,i));
        draw_text(292,y+7,0.5f,COL_ACCENT,"+");
    }
    for(int i=0;i<4;i++){
        C2D_DrawRectSolid(BTNS[i].x,216,0,BTNS[i].w,20,
            (i==2&&g_dirty)?COL_ACCENT:COL_PANEL);
        draw_text_c(BTNS[i].x+BTNS[i].w/2,219,0.44f,
            (i==2&&g_dirty)?COL_BG_BOT:COL_TEXT, (i==0)?page_next_label():BTNS[i].lab);
    }
}

/* dedicated top screen while editing stats: current totals, live */
static void draw_top_stats(sg_mon *m){
    C2D_DrawRectSolid(0,0,0,400,240,COL_BG_TOP);
    C2D_DrawRectSolid(0,0,0,400,24,COL_PANEL);
    draw_text(8,4,0.55f,COL_ACCENT,"Current Stats");
    char nm[32]; sg_species_name(mon_species(m),nm,sizeof nm);
    draw_textf(200,6,0.5f,COL_TEXT,"%s  Lv%d",nm,sg_level_of(m));
    uint8_t base[6]; sg_base_stats(mon_species(m),base);
    int nb=mon_nature(m)/5, nl=mon_nature(m)%5;
    C2D_DrawRectSolid(40,34,0,320,190,COL_PANEL);
    draw_text(56,42,0.5f,COL_DIM,"STAT");
    draw_text(150,42,0.5f,COL_DIM,"BASE");
    draw_text(216,42,0.5f,COL_DIM,"IV");
    draw_text(264,42,0.5f,COL_DIM,"EV");
    draw_text(320,42,0.5f,COL_DIM,"NOW");
    for(int i=0;i<6;i++){
        float y=66+i*25;
        bool sel=(g_edit_sel/2==i);
        if(sel) C2D_DrawRectSolid(44,y-2,0,312,24,COL_SEL);
        u32 col=COL_TEXT;
        char arrow[4]=""; int ni=i-1;
        if(i>0 && nb!=nl){ if(ni==nb){ strcpy(arrow,"+"); col=C(120,220,150);} else if(ni==nl){ strcpy(arrow,"-"); col=C(230,150,140);} }
        draw_textf(56,y,0.6f,col,"%s%s",STAT_NM[i],arrow);
        draw_textf(156,y,0.6f,COL_TEXT,"%d",base[i]);
        draw_textf(216,y,0.6f,COL_TEXT,"%d",mon_iv(m,i));
        draw_textf(264,y,0.6f,COL_TEXT,"%d",mon_ev(m,i));
        draw_textf(320,y,0.6f,COL_ACCENT,"%d",sg_calc_stat(m,i));
    }
    if(!m->is_party) draw_text_c(200,228,0.4f,COL_DIM,"(box: level estimated from EXP)");
}

static void draw_bottom_edit(sg_mon *m){
    C2D_DrawRectSolid(0,0,0,320,240,COL_BG_BOT);
    int rows = page_rows(g_page);
    float rh = 15.5f;
    char lab[16],val[40];
    for(int r=0;r<rows;r++){
        float y=4+r*rh;
        bool s=(r==g_edit_sel);
        C2D_DrawRectSolid(2,y,0,316,rh-1,s?COL_SEL:((r&1)?COL_PANEL:COL_PANEL2));
        edit_row_text(m,r,lab,val);
        draw_text(8,y+1,0.42f,COL_DIM,lab);
        draw_text(100,y+1,0.42f,COL_TEXT,val);
        if(s){ draw_text(86,y+1,0.42f,COL_ACCENT,"<");
               draw_text(304,y+1,0.42f,COL_ACCENT,">"); }
    }
    for(int i=0;i<4;i++){
        C2D_DrawRectSolid(BTNS[i].x,216,0,BTNS[i].w,20,
            (i==2&&g_dirty)?COL_ACCENT:COL_PANEL);
        draw_text_c(BTNS[i].x+BTNS[i].w/2,219,0.44f,
            (i==2&&g_dirty)?COL_BG_BOT:COL_TEXT,
            (i==0)?page_next_label():BTNS[i].lab);
    }
}

/* ---------------- save picker (bottom) ---------------- */
static void draw_bottom_picker(int sel){
    C2D_DrawRectSolid(0,0,0,320,240,COL_BG_BOT);
    draw_text(8,4,0.5f,COL_ACCENT,"Pick a save");
    if(!g_nfiles){
        draw_text(8,40,0.42f,COL_TEXT,"No .sav files found.");
        draw_text(8,60,0.42f,COL_DIM,"Dump your GBA VC save with");
        draw_text(8,74,0.42f,COL_DIM,"GodMode9 (lands in /gm9/out),");
        draw_text(8,88,0.42f,COL_DIM,"then press Y to rescan.");
        return;
    }
    for(int i=0;i<g_nfiles && i<12;i++){
        float y=26+i*17;
        C2D_DrawRectSolid(2,y,0,316,16,i==sel?COL_SEL:((i&1)?COL_PANEL:COL_PANEL2));
        const char *base=strrchr(g_files[i],'/'); base=base?base+1:g_files[i];
        draw_textf(8,y+1,0.42f,COL_TEXT,"%s",base);
    }
    draw_text_c(160,229,0.36f,COL_DIM,"A open  Y rescan  START exit  SELECT theme");
}

/* ================= main ================= */
int main(void){
    gfxInitDefault();
    C3D_Init(C3D_DEFAULT_CMDBUF_SIZE);
    C2D_Init(C2D_DEFAULT_MAX_OBJECTS);
    C2D_Prepare();
    g_top=C2D_CreateScreenTarget(GFX_TOP,GFX_LEFT);
    g_bot=C2D_CreateScreenTarget(GFX_BOTTOM,GFX_LEFT);
    g_txtbuf=C2D_TextBufNew(4096);
    srand(time(NULL));

    apply_palette(g_theme);

    char rom[PATHLEN]; bool rom_found=find_rom(rom);
    bool rom_loaded = rom_found && sg_load_rom(rom);

    enum{ ST_PICK, ST_BROWSE, ST_EDIT } st=ST_PICK;
    int pick=0;
    Sel sel={true,0,0,0};
    sg_mon cur; bool have=false;
    char savepath[PATHLEN]={0};
    const char *banner = (!rom_found)? "No ROM! Put it at /3ds/seaglass/rom.gba" :
                         (!rom_loaded||!sg_rom_ok)? "ROM not recognised - names may be wrong" : NULL;
    int banner_timer=0; char banner_buf[64];

    g_nfiles=0;
    scan_dir("sdmc:/gm9/out",".sav");
    scan_dir("sdmc:/3ds/seaglass",".sav");
    scan_dir("sdmc:",".sav");

    while(aptMainLoop()){
        hidScanInput();
        u32 kd=hidKeysDown(), kh=hidKeysHeld();
        touchPosition tp; hidTouchRead(&tp);
        int step=(kh&KEY_R)?10:1;
        if(kd&KEY_SELECT){ g_theme=!g_theme; apply_palette(g_theme); }

        if(st==ST_PICK){
            if(kd&KEY_START) break;
            if(kd&KEY_Y){ g_nfiles=0;
                scan_dir("sdmc:/gm9/out",".sav");
                scan_dir("sdmc:/3ds/seaglass",".sav");
                scan_dir("sdmc:",".sav"); }
            if(g_nfiles){
                if(kd&KEY_DOWN && pick<g_nfiles-1) pick++;
                if(kd&KEY_UP   && pick>0) pick--;
                if(kd&KEY_TOUCH){
                    int r=(tp.py-26)/17;
                    if(r>=0&&r<g_nfiles){ if(r==pick) kd|=KEY_A; pick=r; }
                }
                if(kd&KEY_A){
                    if(sg_load_save(g_files[pick])){
                        strcpy(savepath,g_files[pick]);
                        sel=(Sel){true,0,0,0};
                        have=sel_load(&sel,&cur);
                        st=ST_BROWSE;
                    } else { snprintf(banner_buf,sizeof banner_buf,
                        "Not a valid 128KB save"); banner=banner_buf; banner_timer=180; }
                }
            }
        }
        else if(st==ST_BROWSE){
            if(kd&KEY_B){ st=ST_PICK; }
            if(kd&KEY_L){ sel.box=(sel.box+BOXES-1)%BOXES; sel.in_party=false; }
            if(kd&KEY_R){ sel.box=(sel.box+1)%BOXES;       sel.in_party=false; }
            if(kd&KEY_TOUCH){
                if(tp.py<42 && tp.px>=40 && tp.px<280){         /* party strip */
                    int i=(tp.px-40)/40;
                    if(sel.in_party&&sel.pslot==i) kd|=KEY_A;
                    sel.in_party=true; sel.pslot=i;
                }else if(tp.py>=44&&tp.py<64){                   /* box arrows  */
                    if(tp.px<60){ sel.box=(sel.box+BOXES-1)%BOXES; sel.in_party=false; }
                    if(tp.px>260){ sel.box=(sel.box+1)%BOXES;      sel.in_party=false; }
                }else if(tp.py>=66&&tp.py<226&&tp.px>=40&&tp.px<280){
                    int c2=(tp.px-40)/40, r=(tp.py-66)/32, s=r*BOX_COLS+c2;
                    if(!sel.in_party&&sel.bslot==s) kd|=KEY_A;
                    sel.in_party=false; sel.bslot=s;
                }
            }
            if(kd&(KEY_UP|KEY_DOWN|KEY_LEFT|KEY_RIGHT)){
                if(sel.in_party){
                    if(kd&KEY_LEFT && sel.pslot>0) sel.pslot--;
                    if(kd&KEY_RIGHT&& sel.pslot<5) sel.pslot++;
                    if(kd&KEY_DOWN){ sel.in_party=false; sel.bslot=sel.pslot; }
                }else{
                    int r=sel.bslot/BOX_COLS, c2=sel.bslot%BOX_COLS;
                    if(kd&KEY_LEFT && c2>0) c2--;
                    if(kd&KEY_RIGHT&& c2<BOX_COLS-1) c2++;
                    if(kd&KEY_DOWN && r<BOX_ROWS-1) r++;
                    if(kd&KEY_UP){ if(r>0) r--; else { sel.in_party=true; sel.pslot=c2<6?c2:5; } }
                    sel.bslot=r*BOX_COLS+c2;
                }
            }
            have=sel_load(&sel,&cur);
            if((kd&KEY_A) && have){ st=ST_EDIT; g_edit_sel=0; g_page=PAGE_FIELDS; g_dirty=false; }
            if(kd&KEY_X){
                if(sg_write_save(savepath)){ snprintf(banner_buf,sizeof banner_buf,
                    "Saved! Re-inject with GodMode9"); }
                else snprintf(banner_buf,sizeof banner_buf,"Write failed!");
                banner=banner_buf; banner_timer=240;
            }
        }
        else { /* ST_EDIT */
            int rows=page_rows(g_page);
            if(kd&KEY_B){ st=ST_BROWSE; }
            if(kd&KEY_DOWN && g_edit_sel<rows-1) g_edit_sel++;
            if(kd&KEY_UP   && g_edit_sel>0)      g_edit_sel--;
            if(kd&KEY_Y){ for(int i=0;i<6;i++) mon_set_iv(&cur,i,31); g_dirty=true; }
            int d=(kd&KEY_RIGHT)?step:(kd&KEY_LEFT)?-step:0;
            if((kd&KEY_A) && g_page==PAGE_FIELDS &&
               (g_edit_sel==E_SPECIES || g_edit_sel==E_ITEM ||
                (g_edit_sel>=E_MV0 && g_edit_sel<=E_MV3))){
                uint16_t r=0xFFFF;
                if(g_edit_sel==E_SPECIES)
                    r=run_picker("Species", sg_species_sorted, sg_species_sn, sg_species_name, mon_species(&cur));
                else if(g_edit_sel==E_ITEM)
                    r=run_picker("Item", sg_item_sorted, sg_item_sn, sg_item_name, mon_item(&cur));
                else
                    r=run_picker("Move", sg_move_sorted, sg_move_sn, sg_move_name, mon_move(&cur,g_edit_sel-E_MV0));
                if(r!=0xFFFF){
                    if(g_edit_sel==E_SPECIES) mon_set_species(&cur,r);
                    else if(g_edit_sel==E_ITEM) mon_set_item(&cur,r);
                    else mon_set_move(&cur,g_edit_sel-E_MV0,r);
                    g_dirty=true;
                }
            } else if(d||(kd&KEY_A)) edit_adjust(&cur,g_edit_sel,d,(kd&KEY_A)!=0);
            if(kd&KEY_TOUCH){
                if(tp.py>=216){
                    for(int i=0;i<4;i++)
                        if(tp.px>=BTNS[i].x&&tp.px<BTNS[i].x+BTNS[i].w){
                            if(i==0){ g_page=(g_page+1)%PAGE_COUNT; g_edit_sel=0; }
                            if(i==1){ for(int s=0;s<6;s++) mon_set_iv(&cur,s,31); g_dirty=true; }
                            if(i==2) kd|=KEY_START;
                            if(i==3) st=ST_BROWSE;
                        }
                }else if(g_page==PAGE_STATS){
                    int i=(int)((tp.py-22)/28);       /* stat row */
                    if(i>=0&&i<6){
                        if(tp.px>=128&&tp.px<212){     /* IV field */
                            g_edit_sel=i*2;
                            if(tp.px<156) edit_adjust(&cur,g_edit_sel,-step,false);
                            else if(tp.px>194) edit_adjust(&cur,g_edit_sel,step,false);
                        }else if(tp.px>=216&&tp.px<=308){ /* EV field */
                            g_edit_sel=i*2+1;
                            if(tp.px<244) edit_adjust(&cur,g_edit_sel,-step,false);
                            else if(tp.px>292) edit_adjust(&cur,g_edit_sel,step,false);
                        }
                    }
                }else{
                    int r=(int)((tp.py-4)/15.5f);
                    if(r>=0&&r<rows){
                        if(r==g_edit_sel){
                            bool pick = (g_page==PAGE_FIELDS) && (r==E_SPECIES||r==E_ITEM||(r>=E_MV0&&r<=E_MV3));
                            if(tp.px<100 && !pick) edit_adjust(&cur,r,-step,false);
                            else if(tp.px>290 && !pick) edit_adjust(&cur,r,step,false);
                            else { uint16_t rr=0xFFFF;
                                if(!pick) edit_adjust(&cur,r,0,true);
                                else {
                                    if(r==E_SPECIES) rr=run_picker("Species",sg_species_sorted,sg_species_sn,sg_species_name,mon_species(&cur));
                                    else if(r==E_ITEM) rr=run_picker("Item",sg_item_sorted,sg_item_sn,sg_item_name,mon_item(&cur));
                                    else rr=run_picker("Move",sg_move_sorted,sg_move_sn,sg_move_name,mon_move(&cur,r-E_MV0));
                                    if(rr!=0xFFFF){ if(r==E_SPECIES)mon_set_species(&cur,rr);
                                        else if(r==E_ITEM)mon_set_item(&cur,rr);
                                        else mon_set_move(&cur,r-E_MV0,rr); g_dirty=true; }
                                }
                            }
                        }
                        g_edit_sel=r;
                    }
                }
            }
            if(kd&KEY_START){
                if(cur.is_party) sg_recompute_stats(&cur);
                sg_commit_mon(&cur);
                if(sg_write_save(savepath))
                    snprintf(banner_buf,sizeof banner_buf,"Saved! Re-inject with GodMode9");
                else snprintf(banner_buf,sizeof banner_buf,"Write failed!");
                banner=banner_buf; banner_timer=240; g_dirty=false;
            }
        }
        if(banner_timer>0 && --banner_timer==0) banner=NULL;

        /* ---------------- render ---------------- */
        C3D_FrameBegin(C3D_FRAME_SYNCDRAW);
        C2D_TextBufClear(g_txtbuf);

        C2D_TargetClear(g_top,COL_BG_TOP);
        C2D_SceneBegin(g_top);
        if(st==ST_PICK){
            C2D_DrawRectSolid(0,0,0,400,24,COL_PANEL);
            draw_text(8,4,0.55f,COL_ACCENT,"Seaglass Save Editor");
            draw_text_c(200,90,0.5f,COL_TEXT,"Pick a save on the bottom screen");
            draw_text_c(200,116,0.42f,COL_DIM,"GodMode9 dump -> edit -> GodMode9 inject");
            if(banner) draw_text_c(200,150,0.45f,COL_WARN,banner);
        } else if(st==ST_EDIT && g_page==PAGE_STATS){
            draw_top_stats(&cur);
        } else {
            draw_top_summary(&cur,have,banner);
        }

        C2D_TargetClear(g_bot,COL_BG_BOT);
        C2D_SceneBegin(g_bot);
        if(st==ST_PICK)        draw_bottom_picker(pick);
        else if(st==ST_BROWSE) draw_bottom_browser(&sel);
        else                   { if(g_page==PAGE_STATS) draw_bottom_stats(&cur); else draw_bottom_edit(&cur); }

        C3D_FrameEnd(0);
    }
    C2D_TextBufDelete(g_txtbuf);
    for(int i=0;i<SPR_CACHE;i++) if(g_spr[i].used) C3D_TexDelete(&g_spr[i].tex);
    C2D_Fini(); C3D_Fini();
    gfxExit();
    return 0;
}
