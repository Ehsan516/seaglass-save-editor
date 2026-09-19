/* Execute the real app input/render paths on a host with synthetic files.
   app.c is included so assertions can inspect state without test-only APIs. */
#include <assert.h>
#include "../source/app.c"

static void press(unsigned key){assert(app_input((UiInput){.down=key,.held=key}));}
static void tap(UiRect r){assert(app_input((UiInput){.touch=true,.x=r.x+r.w/2,.y=r.y+r.h/2}));}
static void capture(const char *path){
    redraw=true;
    for(int i=0;i<80 && redraw;i++)app_draw();
    assert(!redraw); /* cache converges instead of constantly reloading */
    FILE *f=fopen(path,"wb");assert(f);
    assert(fwrite(ui_pixels,1,sizeof ui_pixels,f)==sizeof ui_pixels);assert(!fclose(f));
    for(int s=0;s<2;s++)for(int i=0;i<UI_W*UI_H;i++)assert(ui_pixels[s][i]&0x8000);
}
int main(int argc,char **argv){
    assert(argc==4);
    assert(sg_load_save(argv[1]));assert(sg_load_rom(argv[2])&&sg_rom_ok);
    rom_loaded=true;present_frame=NULL;
    snprintf(save_path,sizeof save_path,"%s",argv[3]);
    screen=BROWSE;party=true;slot=0;box=0;select_current();assert(have);
    capture("nds-editor/build/test/storage.rgb555");
    /* Every visible slot is touch-selectable, including all five bottom rows. */
    for(int i=0;i<30;i++){tap(box_cell(i));assert(!party&&slot==i);}
    for(int i=0;i<6;i++){tap(party_cell(i));assert(party&&slot==i);}
    tap(party_cell(0));press(UI_A);assert(screen==EDIT);
    sg_mon original_party[2],original_box,restored;
    for(int i=0;i<2;i++)assert(sg_get_party_mon(i,&original_party[i]));
    assert(sg_get_box_mon(sg_box_slot_off(0,0),&original_box));
    /* Reset can be cancelled; confirmation restores drafts and changes kept
       while navigating several party/box Pokemon, without resetting to zero. */
    tap(tab(STATS));press(UI_RIGHT);unsigned changed=mon_iv(&current,0);
    press(UI_X);assert(screen==CONFIRM&&confirm_action==ASK_RESET);
    press(UI_B);assert(screen==EDIT&&mon_iv(&current,0)==changed&&draft_dirty);
    tap(editor_reset);assert(screen==CONFIRM);press(UI_A);
    assert(screen==EDIT&&!draft_dirty&&!disk_dirty&&!memcmp(&current,&original_party[0],sizeof current));
    press(UI_RIGHT);tap(editor_back);assert(screen==BROWSE&&disk_dirty);
    tap(party_cell(1));press(UI_A);field=7;press(UI_RIGHT);press(UI_B);
    tap(box_cell(0));press(UI_A);field=7;press(UI_RIGHT);press(UI_B);
    tap(reset_button);assert(screen==CONFIRM&&confirm_action==ASK_RESET);
    capture("nds-editor/build/test/reset.rgb555");press(UI_A);
    assert(screen==BROWSE&&!draft_dirty&&!disk_dirty);
    for(int i=0;i<2;i++){assert(sg_get_party_mon(i,&restored));assert(!memcmp(&restored,&original_party[i],sizeof restored));}
    assert(sg_get_box_mon(sg_box_slot_off(0,0),&restored));assert(!memcmp(&restored,&original_box,sizeof restored));
    tap(party_cell(0));press(UI_A);
    tap(tab(STATS));assert(page==STATS&&field==0);
    unsigned before[6];for(int i=0;i<6;i++)before[i]=mon_iv(&current,i);
    press(UI_RIGHT);assert(mon_iv(&current,0)==before[0]+1);
    for(int i=1;i<6;i++)assert(mon_iv(&current,i)==before[i]);
    unsigned ev=mon_ev(&current,0);press(UI_DOWN);press(UI_RIGHT);assert(mon_ev(&current,0)==ev+1);
    capture("nds-editor/build/test/stats.rgb555");
    tap(tab(MOVES));press(UI_A);assert(screen==PICKER&&picker_n>0);
    press(UI_SELECT);assert(screen==KEYBOARD);snprintf(edit_text,sizeof edit_text,"Mud");press(UI_START);
    assert(screen==PICKER&&picker_n==1);press(UI_A);assert(mon_move(&current,0)==4&&screen==EDIT);
    capture("nds-editor/build/test/moves.rgb555");
    tap(tab(INFO));press(UI_DOWN);press(UI_A);assert(screen==KEYBOARD);
    capture("nds-editor/build/test/keyboard.rgb555");
    snprintf(edit_text,sizeof edit_text,"Seaglass");press(UI_START);
    char nickname[32];mon_nickname(&current,nickname,sizeof nickname);assert(!strcmp(nickname,"Seaglass"));
    press(UI_B);assert(screen==BROWSE&&disk_dirty&&!draft_dirty);
    select_current();assert(mon_iv(&current,0)==before[0]+1); /* apply survives navigation */
    press(UI_B);assert(screen==CONFIRM&&confirm_action==ASK_LEAVE);press(UI_B);assert(screen==BROWSE&&disk_dirty);
    press(UI_Y);assert(screen==CONFIRM);capture("nds-editor/build/test/confirm.rgb555");
    press(UI_A);assert(screen==BROWSE&&!disk_dirty);assert(sg_load_save(argv[3]));
    select_current();assert(mon_move(&current,0)==4);
    /* A successful save advances the reset baseline; a failed write does not. */
    sg_mon saved=current;
    press(UI_A);tap(tab(STATS));press(UI_RIGHT);press(UI_B);press(UI_A);
    tap(tab(STATS));press(UI_RIGHT);press(UI_X);press(UI_A);
    assert(screen==EDIT&&!disk_dirty&&!draft_dirty&&!memcmp(&current,&saved,sizeof current));
    press(UI_RIGHT);snprintf(save_path,sizeof save_path,"%s/missing.sav",argv[3]);
    press(UI_Y);press(UI_A);assert(screen==EDIT&&disk_dirty);
    press(UI_X);press(UI_A);assert(!disk_dirty&&!memcmp(&current,&saved,sizeof current));
    snprintf(save_path,sizeof save_path,"%s",argv[3]);
    /* X/Y keep their reset/save meanings in nested pickers and keyboard. */
    tap(tab(MOVES));press(UI_A);assert(screen==PICKER);
    press(UI_X);assert(screen==CONFIRM&&confirm_action==ASK_RESET);press(UI_B);assert(screen==PICKER);
    press(UI_SELECT);assert(screen==KEYBOARD);press(UI_X);assert(screen==CONFIRM);press(UI_B);
    assert(screen==KEYBOARD);press(UI_Y);assert(screen==CONFIRM&&confirm_action==ASK_SAVE);press(UI_B);
    assert(screen==EDIT);
    screen=SAVE_FILES;file_n=64;file_sel=63;message[0]=0;
    for(int i=0;i<64;i++)snprintf(files[i],PATHLEN,"/roms/gba/Pokemon Emerald Seaglass save %02d.sav",i+1);
    capture("nds-editor/build/test/files.rgb555");
    press(UI_UP);assert(file_sel==62);
    /* Empty picker must never index -1 or select arbitrary id zero. */
    screen=PICKER;picker_n=0;picker_sel=0;press(UI_A);assert(screen==PICKER);press(UI_DOWN);assert(picker_sel==0);
    capture("nds-editor/build/test/empty-picker.rgb555");
    puts("PASS: controls, reset/cancel across Pokemon, save baseline, failed write, nested menus, touch grid, IV/EV mapping, search, nickname, paging, framebuffer bounds");
    return 0;
}
