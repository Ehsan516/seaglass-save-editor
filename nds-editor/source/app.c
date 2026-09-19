/* DS front end. Pixel coordinates are shared by drawing and hit testing.
   No console, ANSI escapes, font tiles, or writes to the displayed frame. */
#include "app.h"
#include "ui.h"
#include "seaglass_core.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <dirent.h>

#define MAX_FILES 64
#define PATHLEN 256
#define LIST_ROWS 6
#define SPRITE_CACHE 48
static const char *nature_names[25]={"Hardy","Lonely","Brave","Adamant","Naughty",
 "Bold","Docile","Relaxed","Impish","Lax","Timid","Hasty","Serious","Jolly","Naive",
 "Modest","Mild","Quiet","Bashful","Rash","Calm","Gentle","Sassy","Careful","Quirky"};
static const char *stat_names[6]={"HP","Atk","Def","Spe","SpA","SpD"};
static const char *ball_names[13]={"?","Master Ball","Ultra Ball","Great Ball","Poke Ball",
 "Safari Ball","Net Ball","Dive Ball","Nest Ball","Repeat Ball","Timer Ball","Luxury Ball","Premier Ball"};
static const char *origin_names[16]={"?","Sapphire","Ruby","Emerald","FireRed","LeafGreen",
 "?","?","?","?","?","?","?","?","?","Colosseum/XD"};
enum { ROM_FILES,SAVE_FILES,BROWSE,EDIT,PICKER,KEYBOARD,CONFIRM,LOADING,STORAGE_ERROR };
enum { INFO,STATS,EXTRA,MOVES };
enum { ASK_SAVE,ASK_LEAVE,ASK_RESET };
static int screen=ROM_FILES,return_screen=BROWSE,confirm_action;
static int file_sel,file_n,box,slot,page,field,picker_sel,picker_n,picker_kind;
static bool party=true,have=false,redraw=true,draft_dirty=false,disk_dirty=false;
static bool rom_loaded=false,searching=false,keyboard_shift=false;
static sg_mon current;
/* Original records before their first in-memory commit since load/save.
   Reset restores these exact records, including unmodelled bytes and stats. */
static sg_mon undo_mons[6+14*30];
static bool undo_valid[6+14*30];
static char files[MAX_FILES][PATHLEN],save_path[PATHLEN],message[80],query[24],edit_text[32];
static uint16_t picker_ids[MAX_LIST+1];
static int key_sel,load_percent;
static void (*present_frame)(void);
static unsigned sprite_clock;
static int sprite_budget;
static struct { uint16_t sp; bool shiny,used,ok; unsigned age; uint16_t px[64*64]; } sprites[SPRITE_CACHE];
static uint8_t rgba[64*64*4];

/* Keep these rectangles in one place: both render and input use them. */
static const UiRect back_button={5,171,48,18},save_button={57,171,54,18};
static const UiRect edit_button={173,171,78,18};
static const UiRect reset_button={115,171,54,18};
static const UiRect editor_back={5,171,42,18},editor_save={51,171,42,18};
static const UiRect editor_reset={97,171,48,18},editor_minus={149,171,20,18};
static const UiRect editor_plus={173,171,20,18},editor_change={197,171,54,18};
static const UiRect prev_box={5,26,22,18},next_box={161,26,22,18};
static UiRect box_cell(int i){ return (UiRect){5+(i%6)*30,47+(i/6)*23,28,22}; }
static UiRect party_cell(int i){ return (UiRect){190+(i%2)*31,47+(i/2)*37,29,35}; }
static UiRect list_row(int i){ return (UiRect){7,47+i*19,229,18}; }
static UiRect edit_row(int i){ return (UiRect){6,49+i*18,225,17}; }
static UiRect tab(int i){ return (UiRect){5+i*62,27,60,17}; }
static int start_row(int selected){ return selected/LIST_ROWS*LIST_ROWS; }
static void set_message(const char *s){ snprintf(message,sizeof message,"%s",s); redraw=true; }
static const char *base_name(const char *s){ const char *p=strrchr(s,'/'); return p?p+1:s; }
static void text_num(int x,int y,int value,uint16_t color,int width){ char s[16]; snprintf(s,sizeof s,"%d",value); ui_text(x,y,s,color,width); }

static bool load_slot(bool p,int index,sg_mon *m){
    bool ok=p?(index<sg_party_count()&&sg_get_party_mon(index,m)):
               sg_get_box_mon(sg_box_slot_off(box,index),m);
    return ok && mon_species(m)!=0;
}
static void select_current(void){ have=load_slot(party,slot,&current); }
static void scan_dir(const char *dir,const char *ext){
    DIR *d=opendir(dir); if(!d)return;
    struct dirent *e;
    while((e=readdir(d)) && file_n<MAX_FILES){
        size_t n=strlen(e->d_name),len=strlen(ext);
        if(n>len && !strcasecmp(e->d_name+n-len,ext)){
            int written=snprintf(files[file_n],PATHLEN,"%s/%s",dir,e->d_name);
            if(written>0 && written<PATHLEN)file_n++;
        }
    }
    closedir(d);
}
static int compare_files(const void *a,const void *b){ return strcasecmp(a,b); }
static void scan_files(bool rom){
    file_n=0; file_sel=0;
    const char *ext=rom?".gba":".sav";
    scan_dir("/roms/gba",ext); scan_dir("/seaglass",ext); scan_dir("/",ext);
    qsort(files,file_n,sizeof files[0],compare_files); redraw=true;
}
static const uint16_t *sprite_pixels(uint16_t species,bool shiny){
    if(!rom_loaded || !sg_rom_ok || !species)return NULL;
    int oldest=0;
    for(int i=0;i<SPRITE_CACHE;i++){
        if(sprites[i].used && sprites[i].sp==species && sprites[i].shiny==shiny){
            sprites[i].age=++sprite_clock; return sprites[i].ok?sprites[i].px:NULL;
        }
        if(!sprites[i].used || sprites[i].age<sprites[oldest].age)oldest=i;
    }
    /* Decode at most one new image per frame so input and music can run
       between SD reads when opening a previously unseen box. */
    if(!sprite_budget){ redraw=true; return NULL; }
    sprite_budget--;
    sprites[oldest].sp=species; sprites[oldest].shiny=shiny;
    sprites[oldest].used=true; sprites[oldest].age=++sprite_clock;
    sprites[oldest].ok=sg_sprite_rgba(species,shiny,rgba);
    if(!sprites[oldest].ok)return NULL;
    for(int i=0;i<4096;i++) sprites[oldest].px[i]=rgba[i*4+3]?UI_RGB(rgba[i*4],rgba[i*4+1],rgba[i*4+2]):0;
    return sprites[oldest].px;
}
static void draw_mon(const sg_mon *m,int x,int y,int size){
    const uint16_t *px=sprite_pixels(mon_species(m),mon_shiny(m));
    if(px)ui_sprite(x,y,size,px);
    else{
        ui_ball(x+size/2,y+size/2,size/3,UI_MUTED);
        if(size>=40){ char s[16]; snprintf(s,sizeof s,"#%u",mon_species(m)); ui_center((UiRect){x,y+size-8,size,8},s,UI_INK); }
    }
}
static void footer_status(void){
    if(message[0]){
        ui_round((UiRect){5,162,246,8},2,UI_WHITE);
        ui_text(9,163,message,UI_INK,238);
    }
}
static void draw_intro(bool rom){
    ui_screen(0); ui_background("SEAGLASS","DS");
    ui_panel((UiRect){10,32,236,116},false);
    ui_ball(128,62,19,UI_ACCENT);
    ui_center((UiRect){16,90,224,12},"Your Pokemon, your adventure.",UI_INK);
    ui_center((UiRect){16,110,224,12},rom?"Choose your Emerald Seaglass ROM.":"Choose a save to start editing.",UI_MUTED);
    ui_center((UiRect){16,126,224,12},"Party  /  Boxes  /  Stats  /  Moves",UI_ACCENT);
    ui_center((UiRect){5,157,246,12},rom?"1. Game ROM     >     2. Save file":
        (rom_loaded&&sg_rom_ok?"Game recognised. Names and sprites ready.":"No verified ROM. Names and sprites unavailable."),UI_INK);
    ui_center((UiRect){5,176,246,10},"EMERALD SEAGLASS  /  SAVE EDITOR",UI_MUTED);
}
static void draw_files(void){
    bool rom=screen==ROM_FILES;
    draw_intro(rom);
    ui_screen(1); ui_background(rom?"Open game ROM":"Open save file",rom?"1 / 2":"2 / 2");
    char s[32]; snprintf(s,sizeof s,"%d file%s on your card",file_n,file_n==1?"":"s");
    ui_text(9,32,s,UI_MUTED,225);
    int first=start_row(file_sel);
    for(int i=0;i<LIST_ROWS && first+i<file_n;i++){
        UiRect r=list_row(i); bool active=first+i==file_sel;
        ui_panel(r,active); ui_text(r.x+8,r.y+5,base_name(files[first+i]),active?UI_ACCENT:UI_INK,r.w-15);
    }
    if(!file_n){
        ui_panel((UiRect){7,49,229,80},false);
        ui_text(16,62,rom?"No .gba files found":"No .sav files found",UI_INK,211);
        ui_text(16,80,"Place files in /roms/gba",UI_MUTED,211);
        ui_text(16,95,"or /seaglass, then tap Rescan.",UI_MUTED,211);
    }
    ui_button((UiRect){239,47,14,50},"^",false);
    ui_button((UiRect){239,107,14,50},"v",false);
    footer_status();
    ui_button(back_button,rom?"Continue":"B Back",false);
    ui_button((UiRect){57,171,61,18},"L Rescan",false);
    ui_button((UiRect){122,171,129,18},rom?"A Use ROM":"A Open save",true);
}
static void draw_summary(void){
    ui_screen(0); ui_background("SEAGLASS",draft_dirty||disk_dirty?"EDITED":"SUMMARY");
    if(!have){
        ui_panel((UiRect){12,38,232,111},false); ui_ball(128,75,21,UI_LINE);
        ui_center((UiRect){20,113,216,12},"Select a Pokemon to see its details",UI_MUTED);
        return;
    }
    char s[64],nm[40];
    ui_panel((UiRect){5,28,246,69},false);
    draw_mon(&current,9,30,64);
    sg_species_name(mon_species(&current),nm,sizeof nm); ui_heading(81,32,nm,UI_INK,163);
    mon_nickname(&current,nm,sizeof nm); ui_text(81,49,nm,UI_MUTED,163);
    snprintf(s,sizeof s,"Lv %d%s  %c  %s%s",sg_level_of(&current),current.is_party?"":"~",
        mon_gender(&current),mon_shiny(&current)?"Shiny":"",mon_is_egg(&current)?" Egg":"");
    ui_text(81,61,s,UI_ACCENT,163);
    snprintf(s,sizeof s,"%s nature",nature_names[mon_nature(&current)]); ui_text(81,74,s,UI_INK,163);
    uint16_t a0,a1; sg_species_abilities(mon_species(&current),&a0,&a1);
    sg_ability_name(mon_ability_slot(&current)&&a1?a1:a0,nm,sizeof nm); ui_text(81,85,nm,UI_MUTED,163);
    ui_panel((UiRect){5,102,109,73},false);
    ui_text(11,108,"STAT   IV   EV",UI_MUTED,96);
    for(int i=0;i<6;i++){
        int y=120+i*8;
        ui_text(11,y,stat_names[i],UI_INK,23);
        text_num(46,y,mon_iv(&current,i),UI_INK,18);
        text_num(77,y,mon_ev(&current,i),UI_INK,27);
    }
    ui_panel((UiRect){119,102,132,73},false); ui_text(126,108,"MOVES",UI_ACCENT,100);
    for(int i=0;i<4;i++){
        sg_move_name(mon_move(&current,i),nm,sizeof nm);
        ui_text(126,121+i*12,nm,UI_INK,118);
    }
    sg_item_name(mon_item(&current),nm,sizeof nm);
    snprintf(s,sizeof s,"Item: %s",nm); ui_text(9,182,s,UI_MUTED,237);
}
static void draw_stats(void){
    ui_screen(0); ui_background("Pokemon stats","IV / EV");
    char s[48]; sg_species_name(mon_species(&current),s,sizeof s); ui_text(10,31,s,UI_INK,170);
    snprintf(s,sizeof s,"Lv %d%s",sg_level_of(&current),current.is_party?"":"~"); ui_text(204,31,s,UI_ACCENT,44);
    ui_panel((UiRect){6,45,244,128},false);
    ui_text(14,52,"STAT",UI_MUTED,40); ui_text(72,52,"BASE",UI_MUTED,30);
    ui_text(118,52,"IV",UI_MUTED,20); ui_text(154,52,"EV",UI_MUTED,25); ui_text(201,52,"TOTAL",UI_ACCENT,40);
    uint8_t base[6]={0}; if(rom_loaded&&sg_rom_ok)sg_base_stats(mon_species(&current),base);
    for(int i=0;i<6;i++){
        int y=69+i*16; if(field/2==i)ui_round((UiRect){10,y-4,236,15},3,UI_PALE);
        ui_text(14,y,stat_names[i],UI_INK,42); text_num(75,y,base[i],UI_MUTED,24);
        text_num(117,y,mon_iv(&current,i),UI_INK,24); text_num(153,y,mon_ev(&current,i),UI_INK,30);
        if(rom_loaded&&sg_rom_ok)text_num(207,y,sg_calc_stat(&current,i),UI_ACCENT,30);
        else ui_text(210,y,"?",UI_MUTED,20);
    }
    ui_center((UiRect){4,180,248,10},current.is_party?"IV: 0-31    EV: 0-255":"Box level is estimated from experience",UI_MUTED);
}
static void draw_browser(void){
    draw_summary(); ui_screen(1); ui_background("Pokemon storage",disk_dirty?"UNSAVED":"BOXES");
    ui_button(prev_box,"<",false); ui_button(next_box,">",false);
    char s[32]; snprintf(s,sizeof s,"BOX %02d",box+1); ui_center((UiRect){29,26,130,18},s,UI_INK);
    ui_center((UiRect){190,26,60,18},"PARTY",UI_ACCENT);
    for(int i=0;i<30;i++){
        UiRect r=box_cell(i); ui_panel(r,!party&&slot==i);
        sg_mon m;
        if(load_slot(false,i,&m)){
            draw_mon(&m,r.x+3,r.y,22);
            if(mon_item(&m))ui_rect(r.x+22,r.y+16,3,4,UI_GOLD);
            if(mon_shiny(&m))ui_rect(r.x+2,r.y+2,2,2,UI_GOLD);
        }else ui_rect(r.x+12,r.y+10,3,2,UI_LINE);
    }
    for(int i=0;i<6;i++){
        UiRect r=party_cell(i); ui_panel(r,party&&slot==i);
        sg_mon m;
        if(load_slot(true,i,&m)){
            draw_mon(&m,r.x,r.y+1,29);
            if(mon_item(&m))ui_rect(r.x+23,r.y+27,3,4,UI_GOLD);
        }else ui_ball(r.x+14,r.y+17,6,UI_LINE);
    }
    if(!rom_loaded||!sg_rom_ok)ui_text(7,163,"No verified ROM: names may be unavailable",UI_RED,241);
    footer_status();
    ui_button(back_button,"B Back",false); ui_button(save_button,"Y Save",false);
    ui_button(reset_button,"X Reset",false);
    ui_button(edit_button,"A Edit",true);
}
static int field_count(void){ return page==INFO?9:page==EXTRA?13:12; }
static void field_text(int f,char *label,char *value){
    value[0]=label[0]=0;
    if(page==INFO)switch(f){
        case 0:strcpy(label,"Species"); sg_species_name(mon_species(&current),value,40);break;
        case 1:strcpy(label,"Nickname");mon_nickname(&current,value,40);break;
        case 2:strcpy(label,"Level");snprintf(value,40,current.is_party?"%d":"%d (estimated)",sg_level_of(&current));break;
        case 3:strcpy(label,"Nature");snprintf(value,40,"%s",nature_names[mon_nature(&current)]);break;
        case 4:strcpy(label,"Gender");snprintf(value,40,"%s",mon_gender(&current)=='M'?"Male":mon_gender(&current)=='F'?"Female":"Genderless");break;
        case 5:strcpy(label,"Shiny");strcpy(value,mon_shiny(&current)?"Yes":"No");break;
        case 6:{strcpy(label,"Ability");uint16_t a,b;sg_species_abilities(mon_species(&current),&a,&b);sg_ability_name(mon_ability_slot(&current)&&b?b:a,value,40);break;}
        case 7:strcpy(label,"Friendship");snprintf(value,40,"%u",mon_friendship(&current));break;
        case 8:strcpy(label,"Held item");sg_item_name(mon_item(&current),value,40);break;
    }else if(page==STATS){
        snprintf(label,20,"%s %s",stat_names[f/2],f%2?"EV":"IV");
        snprintf(value,40,"%u",f%2?mon_ev(&current,f/2):mon_iv(&current,f/2));
    }else if(page==MOVES){
        int i=f/3,t=f%3; snprintf(label,20,"%s %d",t==0?"Move":t==1?"PP":"PP Up",i+1);
        if(t==0)sg_move_name(mon_move(&current,i),value,40);
        else snprintf(value,40,"%u",t==1?mon_pp(&current,i):mon_pp_up(&current,i));
    }else{
        static const char *labels[]={"Cool","Beauty","Cute","Smart","Tough","Sheen","Pkrs strain","Pkrs days","Met level","Met location","Origin game","Caught in","OT gender"};
        snprintf(label,20,"%s",labels[f]);
        if(f<6)snprintf(value,40,"%u",mon_contest(&current,f));
        else switch(f){
            case 6:snprintf(value,40,"%u",mon_pokerus_strain(&current));break;
            case 7:snprintf(value,40,"%u",mon_pokerus_days(&current));break;
            case 8:snprintf(value,40,"%u",mon_met_level(&current));break;
            case 9:snprintf(value,40,"%u",mon_met_location(&current));break;
            case 10:snprintf(value,40,"%s",origin_names[mon_origin_game(&current)&15]);break;
            case 11:snprintf(value,40,"%s",ball_names[mon_poke_ball(&current)%13]);break;
            case 12:strcpy(value,mon_ot_gender(&current)?"Female":"Male");break;
        }
    }
}
static void draw_edit(void){
    if(page==STATS)draw_stats();else draw_summary();
    ui_screen(1); ui_background("Edit Pokemon",draft_dirty||disk_dirty?"UNSAVED":"EDITOR");
    const char *tabs[]={"Pokemon","Stats","Extra","Moves"};
    for(int i=0;i<4;i++)ui_button(tab(i),tabs[i],page==i);
    int first=start_row(field);
    for(int i=0;i<LIST_ROWS&&first+i<field_count();i++){
        char lab[20],val[40];field_text(first+i,lab,val); UiRect r=edit_row(i);
        ui_panel(r,field==first+i);ui_text(12,r.y+5,lab,UI_MUTED,64);ui_text(83,r.y+5,val,UI_INK,139);
    }
    ui_button((UiRect){235,49,17,45},"^",false);ui_button((UiRect){235,109,17,45},"v",false);
    footer_status();
    ui_button(editor_back,"B Back",false);ui_button(editor_save,"Y Save",false);
    ui_button(editor_reset,"X Reset",false);
    ui_button(editor_minus,"-",false);ui_button(editor_plus,"+",false);
    ui_button(editor_change,"A Change",true);
}
static void picker_name(uint16_t id,char *out,size_t n){
    if(picker_kind==0)sg_species_name(id,out,n);
    else if(picker_kind==1)sg_item_name(id,out,n);
    else sg_move_name(id,out,n);
}
static bool contains(const char *s,const char *q){
    size_t n=strlen(q); if(!n)return true;
    for(;*s;s++)if(!strncasecmp(s,q,n))return true;
    return false;
}
static void rebuild_picker(uint16_t selected){
    uint16_t *ids=picker_kind==0?sg_species_sorted:picker_kind==1?sg_item_sorted:sg_move_sorted;
    int n=picker_kind==0?sg_species_sn:picker_kind==1?sg_item_sn:sg_move_sn;
    picker_n=0;picker_sel=0;
    if(picker_kind!=0 && !query[0])picker_ids[picker_n++]=0;
    for(int i=0;i<n && picker_n<MAX_LIST;i++){
        char s[64];picker_name(ids[i],s,sizeof s);
        if(contains(s,query)){
            if(ids[i]==selected)picker_sel=picker_n;
            picker_ids[picker_n++]=ids[i];
        }
    }
}
static void draw_picker(void){
    draw_summary();ui_screen(1);
    ui_background(picker_kind==0?"Choose species":picker_kind==1?"Choose held item":"Choose move","PICKER");
    char s[64];snprintf(s,sizeof s,"%d results  %s",picker_n,query);
    ui_text(9,32,s,UI_MUTED,220);
    int first=start_row(picker_sel);
    for(int i=0;i<LIST_ROWS&&first+i<picker_n;i++){
        UiRect r=list_row(i);ui_panel(r,first+i==picker_sel);
        picker_name(picker_ids[first+i],s,sizeof s);ui_text(r.x+8,r.y+5,s,UI_INK,210);
    }
    if(!picker_n)ui_text(14,65,"No matching names. Try another search.",UI_MUTED,230);
    ui_button((UiRect){239,47,14,50},"^",false);ui_button((UiRect){239,107,14,50},"v",false);
    ui_button(back_button,"B Back",false);ui_button((UiRect){57,171,85,18},"SELECT Search",false);
    ui_button((UiRect){146,171,105,18},"A Select",true);
}
static const char *keyboard_chars="1234567890qwertyuiopasdfghjkl'zxcvbnm.-?";
static UiRect key_rect(int i){ return (UiRect){8+(i%10)*24,67+(i/10)*22,22,20}; }
static void draw_keyboard(void){
    draw_summary();ui_screen(1);ui_background(searching?"Search names":"Edit nickname","KEYBOARD");
    ui_panel((UiRect){8,30,240,26},true);ui_text(16,40,edit_text,UI_INK,221);
    for(int i=0;i<40;i++){
        char s[2]={keyboard_chars[i],0};if(keyboard_shift&&s[0]>='a'&&s[0]<='z')s[0]-=32;
        UiRect r=key_rect(i);ui_panel(r,key_sel==i);ui_center(r,s,UI_INK);
    }
    ui_button((UiRect){8,157,64,14},keyboard_shift?"L lower":"L UPPER",false);
    ui_button((UiRect){77,157,92,14},"Space",false);ui_button((UiRect){174,157,74,14},"SELECT Del",false);
    ui_button((UiRect){8,175,78,15},"B Back",false);ui_button((UiRect){92,175,156,15},"START Done",true);
}
static void draw_confirm(void){
    draw_summary();ui_screen(1);ui_background(confirm_action==ASK_SAVE?"Save changes":confirm_action==ASK_RESET?"Reset changes":"Leave this save?","CONFIRM");
    ui_panel((UiRect){10,38,236,103},false);ui_ball(128,62,13,UI_ACCENT);
    ui_center((UiRect){17,86,222,12},confirm_action==ASK_SAVE?"Write your changes to this save?":confirm_action==ASK_RESET?"Reset ALL unsaved Pokemon edits?":"Discard unsaved changes?",UI_INK);
    ui_center((UiRect){17,106,222,12},base_name(save_path),UI_MUTED);
    ui_center((UiRect){17,123,222,12},confirm_action==ASK_SAVE?"This replaces the file on your SD card.":confirm_action==ASK_RESET?"Restore values from the last load/save.":"The original file will stay unchanged.",UI_MUTED);
    ui_button((UiRect){10,160,112,26},"B Cancel",false);
    ui_button((UiRect){132,160,114,26},confirm_action==ASK_SAVE?"A Save":confirm_action==ASK_RESET?"A Reset":"A Discard",true);
}
static void draw_loading(void){
    draw_intro(true);ui_screen(1);ui_background("Opening your game","ROM");
    ui_panel((UiRect){14,49,228,85},false);
    ui_center((UiRect){20,62,216,14},"Finding names and sprites",UI_INK);
    ui_round((UiRect){26,87,204,12},5,UI_LINE);
    if(load_percent)ui_round((UiRect){27,88,202*load_percent/100,10},4,UI_ACCENT);
    char s[24];snprintf(s,sizeof s,"%d%% complete",load_percent);
    ui_center((UiRect){20,111,216,12},s,UI_MUTED);
    ui_center((UiRect){10,160,236,12},"Large ROMs can take a little while.",UI_MUTED);
}
void app_draw(void){
    if(!redraw)return;
    redraw=false;sprite_budget=1;
    switch(screen){
        case ROM_FILES:case SAVE_FILES:draw_files();break;
        case BROWSE:draw_browser();break;
        case EDIT:draw_edit();break;
        case PICKER:draw_picker();break;
        case KEYBOARD:draw_keyboard();break;
        case CONFIRM:draw_confirm();break;
        case LOADING:draw_loading();break;
        default:
            draw_intro(true);ui_screen(1);ui_background("SD card unavailable","ERROR");
            ui_panel((UiRect){10,43,236,91},false);
            ui_text(20,58,"Could not open the SD card.",UI_RED,218);
            ui_text(20,80,"Launch with a homebrew loader",UI_INK,218);
            ui_text(20,96,"that supports your card.",UI_INK,218);
            ui_button((UiRect){60,156,136,25},"START Exit",true);break;
    }
    if(present_frame)present_frame();
}
static void progress(int percent){ load_percent=percent;redraw=true;app_draw(); }
bool app_loading(void){return screen==LOADING;}
void app_init(bool storage_ok,void (*present)(void)){
    present_frame=present;screen=storage_ok?ROM_FILES:STORAGE_ERROR;redraw=true;
    sg_set_progress_callback(progress);
    if(storage_ok)scan_files(true);
}
static int clamp(int n,int lo,int hi){ return n<lo?lo:n>hi?hi:n; }
static void move_list(int *selected,int n,UiInput in){
    if(!n){*selected=0;return;}
    unsigned k=in.down|in.repeat;
    int d=(k&UI_UP)?-1:(k&UI_DOWN)?1:(k&UI_LEFT)?-LIST_ROWS:(k&UI_RIGHT)?LIST_ROWS:0;
    if(in.touch){
        if(ui_hit((UiRect){239,47,14,50},in.x,in.y))d=-LIST_ROWS;
        if(ui_hit((UiRect){239,107,14,50},in.x,in.y))d=LIST_ROWS;
        for(int i=0;i<LIST_ROWS;i++)if(ui_hit(list_row(i),in.x,in.y)){
            int at=start_row(*selected)+i;if(at<n)*selected=at;break;
        }
    }
    *selected=clamp(*selected+d,0,n-1);
}
static bool tapped(UiInput in,UiRect r){return in.touch&&ui_hit(r,in.x,in.y);}
static void begin_keyboard(bool search){
    searching=search;key_sel=0;keyboard_shift=false;
    if(search)snprintf(edit_text,sizeof edit_text,"%s",query);
    else mon_nickname(&current,edit_text,sizeof edit_text);
    screen=KEYBOARD;
}
static void change_field(int delta){
    sg_mon before=current;
    if(page==INFO)switch(field){
        case 0:case 1:case 8:return; /* names use a picker */
        case 2:if(current.is_party)sg_set_level(&current,mon_level(&current)+delta);else set_message("Box level is estimated; edit in your party.");break;
        case 3:if(rom_loaded&&sg_rom_ok)sg_reroll_pv(&current,(mon_nature(&current)+(delta>0?1:24))%25,-1,0);else set_message("Load a verified ROM to change nature.");break;
        case 4:if(rom_loaded&&sg_rom_ok){char g=mon_gender(&current);if(g=='M'||g=='F')sg_reroll_pv(&current,-1,-1,g=='M'?'F':'M');}break;
        case 5:if(rom_loaded&&sg_rom_ok)sg_reroll_pv(&current,-1,!mon_shiny(&current),0);else set_message("Load a verified ROM to change shininess.");break;
        case 6:mon_set_ability_slot(&current,!mon_ability_slot(&current));break;
        case 7:mon_set_friendship(&current,clamp(mon_friendship(&current)+delta,0,255));break;
    }else if(page==STATS){
        int i=field/2;if(field%2)mon_set_ev(&current,i,clamp(mon_ev(&current,i)+delta,0,255));
        else mon_set_iv(&current,i,clamp(mon_iv(&current,i)+delta,0,31));
    }else if(page==MOVES){
        int i=field/3,t=field%3;
        if(t==1)mon_set_pp(&current,i,clamp(mon_pp(&current,i)+delta,0,99));
        if(t==2)mon_set_pp_up(&current,i,clamp(mon_pp_up(&current,i)+delta,0,3));
    }else{
        if(field<6)mon_set_contest(&current,field,clamp(mon_contest(&current,field)+delta,0,255));
        else switch(field){
            case 6:mon_set_pokerus(&current,clamp(mon_pokerus_strain(&current)+delta,0,15),mon_pokerus_days(&current));break;
            case 7:mon_set_pokerus(&current,mon_pokerus_strain(&current),clamp(mon_pokerus_days(&current)+delta,0,15));break;
            case 8:mon_set_met_level(&current,clamp(mon_met_level(&current)+delta,0,100));break;
            case 9:mon_set_met_location(&current,clamp(mon_met_location(&current)+delta,0,255));break;
            case 10:mon_set_origin_game(&current,(mon_origin_game(&current)+delta+160)%16);break;
            case 11:mon_set_poke_ball(&current,(mon_poke_ball(&current)+delta+130)%13);break;
            case 12:mon_set_ot_gender(&current,!mon_ot_gender(&current));break;
        }
    }
    if(memcmp(&before,&current,sizeof current))draft_dirty=true;
}
static void activate_field(void){
    if(page==INFO&&field==1){begin_keyboard(false);return;}
    if((page==INFO&&(field==0||field==8)) || (page==MOVES&&field%3==0)){
        if(!rom_loaded||!sg_rom_ok){set_message("Choose a verified ROM to browse names.");return;}
        picker_kind=page==MOVES?2:field==0?0:1;query[0]=0;
        uint16_t id=picker_kind==0?mon_species(&current):picker_kind==1?mon_item(&current):mon_move(&current,field/3);
        rebuild_picker(id);screen=PICKER;
    }else change_field(1);
}
static void apply_draft(void){
    if(!draft_dirty)return;
    int i=current.is_party?current.party_index:6+(int)(current.storage_off-4)/80;
    if(i<0||i>=6+14*30){set_message("Cannot apply this Pokemon slot.");return;}
    if(!undo_valid[i]){
        bool ok=current.is_party?sg_get_party_mon(current.party_index,&undo_mons[i]):
                                  sg_get_box_mon(current.storage_off,&undo_mons[i]);
        if(!ok){set_message("Cannot read original Pokemon.");return;}
        undo_valid[i]=true;
    }
    if(current.is_party&&rom_loaded&&sg_rom_ok)sg_recompute_stats(&current);
    sg_commit_mon(&current);draft_dirty=false;disk_dirty=true;
}
static void reset_changes(void){
    for(int i=0;i<6+14*30;i++)if(undo_valid[i])sg_commit_mon(&undo_mons[i]);
    memset(undo_valid,0,sizeof undo_valid);
    draft_dirty=disk_dirty=false;select_current();
    set_message("Reset to the values last loaded or saved.");
}
static void ask(int action){confirm_action=action;return_screen=screen;screen=CONFIRM;}
bool app_input(UiInput in){
    if(!in.down&&!in.repeat&&!in.touch)return true;
    redraw=true;unsigned k=in.down|in.repeat;
    if(screen==STORAGE_ERROR)return !(in.down&UI_START)&&!in.touch;
    /* Keep the requested face-button actions consistent in edit submenus. */
    if(screen==BROWSE||screen==EDIT||screen==PICKER||screen==KEYBOARD){
        if((in.down&UI_X)||(screen==BROWSE&&tapped(in,reset_button))||
           (screen==EDIT&&tapped(in,editor_reset))){ask(ASK_RESET);return true;}
        if(in.down&UI_Y){
            if(screen==KEYBOARD&&!searching){mon_set_nickname(&current,edit_text);draft_dirty=true;}
            if(screen==KEYBOARD||screen==PICKER)screen=EDIT;
            ask(ASK_SAVE);return true;
        }
    }
    if(screen==ROM_FILES||screen==SAVE_FILES){
        bool rom=screen==ROM_FILES;
        if(in.down&UI_SELECT){
            if(rom_loaded){char a[20],b[20];sg_species_name(1,a,sizeof a);sg_move_name(1,b,sizeof b);snprintf(message,sizeof message,"ROM check: %s / %s",a,b);}return true;
        }
        if((in.down&UI_L)||tapped(in,(UiRect){57,171,61,18})){scan_files(rom);return true;}
        if(tapped(in,back_button)||(in.down&UI_B)||(in.down&UI_START)){
            if(rom&&(in.down&UI_B))return false;
            if(!rom&&(in.down&UI_START))return false;
            screen=rom?SAVE_FILES:ROM_FILES;scan_files(screen==ROM_FILES);message[0]=0;return true;
        }
        move_list(&file_sel,file_n,in);
        if(((in.down&UI_A)||tapped(in,(UiRect){122,171,129,18}))&&file_n){
            message[0]=0;
            if(rom){
                screen=LOADING;load_percent=0;app_draw();
                for(int i=0;i<SPRITE_CACHE;i++)sprites[i].used=false;
                rom_loaded=sg_load_rom(files[file_sel]);
                if(!rom_loaded||!sg_rom_ok)set_message("ROM not recognised. Check the game file.");
                screen=SAVE_FILES;scan_files(false);
            }else if(sg_load_save(files[file_sel])){
                snprintf(save_path,sizeof save_path,"%s",files[file_sel]);
                screen=BROWSE;party=true;slot=box=0;draft_dirty=disk_dirty=false;select_current();
                memset(undo_valid,0,sizeof undo_valid);
            }else set_message("Cannot open this save. Expected a GBA save.");
        }
    }else if(screen==BROWSE){
        if((in.down&UI_B)||tapped(in,back_button)){
            if(disk_dirty)ask(ASK_LEAVE);else{screen=SAVE_FILES;scan_files(false);}return true;
        }
        if((in.down&UI_Y)||tapped(in,save_button)){ask(ASK_SAVE);return true;}
        if((k&UI_L)||tapped(in,prev_box)){box=(box+13)%14;}
        if((k&UI_R)||tapped(in,next_box)){box=(box+1)%14;}
        if(in.touch){
            for(int i=0;i<30;i++)if(tapped(in,box_cell(i))){party=false;slot=i;}
            for(int i=0;i<6;i++)if(tapped(in,party_cell(i))){party=true;slot=i;}
        }
        if(party){
            if(k&UI_UP)slot=clamp(slot-2,0,5);
            if(k&UI_DOWN)slot=clamp(slot+2,0,5);
            if(k&UI_LEFT){if(slot%2)slot--;else{party=false;slot=(slot/2)*6+5;}}
            if(k&UI_RIGHT)slot=slot%2?slot:slot+1;
        }else{
            if(k&UI_UP)slot=clamp(slot-6,0,29);
            if(k&UI_DOWN)slot=clamp(slot+6,0,29);
            if(k&UI_LEFT)slot=clamp(slot-1,0,29);
            if(k&UI_RIGHT){if(slot%6==5){party=true;slot=clamp(slot/6,0,2)*2;}else slot++;}
        }
        select_current();
        if(((in.down&UI_A)||tapped(in,edit_button))&&have){screen=EDIT;page=INFO;field=0;message[0]=0;}
    }else if(screen==EDIT){
        if((in.down&UI_B)||tapped(in,editor_back)){apply_draft();screen=BROWSE;set_message(disk_dirty?"Unsaved edits kept. Y Save / X Reset.":"");return true;}
        if(tapped(in,editor_save)){ask(ASK_SAVE);return true;}
        if(k&UI_L){page=(page+1)%4;field=0;}
        if(in.touch)for(int i=0;i<4;i++)if(tapped(in,tab(i))){page=i;field=0;message[0]=0;}
        if(k&UI_UP)field=clamp(field-1,0,field_count()-1);
        if(k&UI_DOWN)field=clamp(field+1,0,field_count()-1);
        if(tapped(in,(UiRect){235,49,17,45}))field=clamp(field-LIST_ROWS,0,field_count()-1);
        if(tapped(in,(UiRect){235,109,17,45}))field=clamp(field+LIST_ROWS,0,field_count()-1);
        for(int i=0;i<LIST_ROWS;i++)if(tapped(in,edit_row(i))){int at=start_row(field)+i;if(at<field_count())field=at;break;}
        int step=(in.held&UI_R)?10:1;
        if((k&UI_LEFT)||tapped(in,editor_minus))change_field(-step);
        if((k&UI_RIGHT)||tapped(in,editor_plus))change_field(step);
        if((in.down&UI_A)||tapped(in,editor_change))activate_field();
    }else if(screen==PICKER){
        if((in.down&UI_B)||tapped(in,back_button)){screen=EDIT;return true;}
        if((in.down&UI_SELECT)||tapped(in,(UiRect){57,171,85,18})){begin_keyboard(true);return true;}
        move_list(&picker_sel,picker_n,in);
        if(((in.down&UI_A)||tapped(in,(UiRect){146,171,105,18}))&&picker_n){
            uint16_t id=picker_ids[picker_sel];
            if(picker_kind==0)mon_set_species(&current,id);
            else if(picker_kind==1)mon_set_item(&current,id);
            else mon_set_move(&current,field/3,id);
            draft_dirty=true;screen=EDIT;
        }
    }else if(screen==KEYBOARD){
        if((in.down&UI_B)||tapped(in,(UiRect){8,175,78,15})){screen=searching?PICKER:EDIT;return true;}
        if((in.down&UI_START)||tapped(in,(UiRect){92,175,156,15})){
            if(searching){snprintf(query,sizeof query,"%.23s",edit_text);rebuild_picker(0);screen=PICKER;}
            else{mon_set_nickname(&current,edit_text);draft_dirty=true;screen=EDIT;}
            return true;
        }
        if(k&UI_UP)key_sel=clamp(key_sel-10,0,39);
        if(k&UI_DOWN)key_sel=clamp(key_sel+10,0,39);
        if(k&UI_LEFT)key_sel=clamp(key_sel-1,0,39);
        if(k&UI_RIGHT)key_sel=clamp(key_sel+1,0,39);
        if((in.down&UI_L)||tapped(in,(UiRect){8,157,64,14}))keyboard_shift=!keyboard_shift;
        size_t len=strlen(edit_text);
        if(((in.down&UI_SELECT)||tapped(in,(UiRect){174,157,74,14}))&&len)edit_text[--len]=0;
        char ch=0;
        if(in.down&UI_A)ch=keyboard_chars[key_sel];
        for(int i=0;i<40;i++)if(tapped(in,key_rect(i))){key_sel=i;ch=keyboard_chars[i];}
        if(tapped(in,(UiRect){77,157,92,14})||(in.down&UI_R))ch=' ';
        if(keyboard_shift&&ch>='a'&&ch<='z')ch-=32;
        if(ch&&len<(searching?23:10)){edit_text[len]=ch;edit_text[len+1]=0;}
    }else if(screen==CONFIRM){
        if((in.down&UI_B)||tapped(in,(UiRect){10,160,112,26})){screen=return_screen;return true;}
        if((in.down&UI_A)||tapped(in,(UiRect){132,160,114,26})){
            if(confirm_action==ASK_LEAVE){disk_dirty=draft_dirty=false;memset(undo_valid,0,sizeof undo_valid);screen=SAVE_FILES;message[0]=0;scan_files(false);}
            else if(confirm_action==ASK_RESET){
                reset_changes();screen=return_screen==BROWSE?BROWSE:EDIT;
            }
            else{
                apply_draft();
                if(sg_write_save(save_path)){disk_dirty=false;memset(undo_valid,0,sizeof undo_valid);set_message("Saved to your SD card.");}
                else set_message("Write failed. Changes are still in memory.");
                screen=return_screen;
            }
        }
    }
    return true;
}
