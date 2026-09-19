/* Compare RAM-backed and DS-streamed reads, decoded sprites and save edits. */
#include "seaglass_core.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>
static unsigned hash(const unsigned char *p,int n){unsigned h=2166136261u;for(int i=0;i<n;i++)h=(h^p[i])*16777619u;return h;}
int main(int argc,char **argv){
    assert(argc==4);assert(sg_load_save(argv[1]));assert(sg_load_rom(argv[2])&&sg_rom_ok);
    char name[80];unsigned char rgba[16384];
    for(int i=0;i<sg_species_n;i++){
        int sp=sg_species_ids[i];sg_species_name(sp,name,sizeof name);
        if(sg_sprite_rgba(sp,false,rgba))printf("sprite %d %s %08x\n",sp,name,hash(rgba,sizeof rgba));
        else printf("sprite_missing %d\n",sp);
    }
    for(int i=0;i<sg_move_n;i++){sg_move_name(sg_move_ids[i],name,sizeof name);printf("move %u %s\n",sg_move_ids[i],name);}
    for(int i=0;i<sg_item_n;i++){sg_item_name(sg_item_ids[i],name,sizeof name);printf("item %u %s\n",sg_item_ids[i],name);}
    sg_mon m;assert(sg_get_party_mon(0,&m));
    for(int i=0;i<6;i++)printf("stat %d %d\n",i,sg_calc_stat(&m,i));
    sg_set_level(&m,60);for(int i=0;i<6;i++)mon_set_iv(&m,i,31);
    sg_recompute_stats(&m);sg_commit_mon(&m);assert(sg_write_save(argv[3]));
    /* A second load must invalidate page and sprite source/name cache state. */
    assert(sg_load_rom(argv[2])&&sg_rom_ok);sg_move_name(1,name,sizeof name);assert(!strcmp(name,"Pound"));
    puts("PASS streaming and reload");return 0;
}
