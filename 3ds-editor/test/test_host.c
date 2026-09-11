/* Host-side validation of seaglass_core against the real save + ROM. */
#include "../source/seaglass_core.h"
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
const char *NATURES_X(int n);

int main(int argc, char **argv){
    if(argc<3){ printf("usage: %s save rom [out.sav]\n",argv[0]); return 1; }
    if(!sg_load_save(argv[1])){ printf("FAIL load save\n"); return 1; }
    if(!sg_load_rom(argv[2])){ printf("FAIL load rom\n"); return 1; }
    char nm[32], nick[32];
    sg_trainer_name(nm,sizeof nm);
    printf("trainer=%s slot=%c rom_ok=%d\n", nm, "AB"[sg_active_slot()], sg_rom_ok);
    printf("lists: species=%d moves=%d items=%d\n", sg_species_n, sg_move_n, sg_item_n);

    int pc=sg_party_count();
    printf("party=%d\n",pc);
    for(int i=0;i<pc;i++){
        sg_mon m;
        if(!sg_get_party_mon(i,&m)){ printf("  %d: DECODE FAIL\n", i); continue; }
        sg_species_name(mon_species(&m),nm,sizeof nm);
        mon_nickname(&m,nick,sizeof nick);
        char mv[32]; sg_move_name(mon_move(&m,0),mv,sizeof mv);
        char it[32]; sg_item_name(mon_item(&m),it,sizeof it);
        printf("  %d: %-11s Lv%-3d '%s' %c%s nat=%s item=%s mv1=%s\n",
            i+1,nm,mon_level(&m),nick,mon_gender(&m),
            mon_shiny(&m)?" SHINY":"",NATURES_X(mon_nature(&m)),it,mv);
    }
    uint32_t offs[64]; int bn=sg_box_scan(offs,64);
    printf("box mons=%d\n",bn);
    for(int i=0;i<bn;i++){
        sg_mon m; sg_get_box_mon(offs[i],&m);
        sg_species_name(mon_species(&m),nm,sizeof nm);
        mon_nickname(&m,nick,sizeof nick);
        printf("  @%05x %-11s '%s'\n",offs[i],nm,nick);
    }

    if(argc>=4){
        /* edit party mon 0: shiny on, level 60, max IVs, recompute, write */
        sg_mon m; sg_get_party_mon(0,&m);
        sg_reroll_pv(&m,-1,1,0);
        for(int s=0;s<6;s++) mon_set_iv(&m,s,31);
        sg_set_level(&m,60);
        sg_recompute_stats(&m);
        sg_commit_mon(&m);
        if(!sg_write_save(argv[3])){ printf("FAIL write\n"); return 1; }
        printf("edited mon0 -> shiny lv60 maxIV, wrote %s\n",argv[3]);
    }
    return 0;
}
/* tiny helper to reach NATURES from tables without extra header exposure */
#include "../source/g3_tables.h"
const char *NATURES_X(int n){ return NATURES[n]; }
