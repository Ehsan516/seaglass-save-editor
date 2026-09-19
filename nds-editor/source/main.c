/* DS platform: bitmap display, input, FAT, and optional streamed audio.
   app.c owns the interface; ui.c renders into RAM, never the visible VRAM. */
#include <nds.h>
#include <fat.h>
#include <maxmod9.h>
#include <stdio.h>
#include <string.h>
#include "app.h"
#include "ui.h"

static uint16_t *top_vram,*bottom_vram;
static FILE *music_file;
static long music_start;
static uint32_t music_bytes,music_left,music_rate;
static bool music_playing;

static uint32_t read32(const unsigned char *p){return p[0]|(p[1]<<8)|(p[2]<<16)|((uint32_t)p[3]<<24);}
static mm_word music_callback(mm_word frames,mm_addr dest,mm_stream_formats format){
    (void)format;
    unsigned char *p=dest;size_t left=(size_t)frames*4;
    while(left && music_file && music_bytes){
        if(!music_left){
            if(fseek(music_file,music_start,SEEK_SET))break;
            music_left=music_bytes;
        }
        size_t want=left<music_left?left:music_left;
        size_t got=fread(p,1,want,music_file);
        p+=got;left-=got;music_left-=got;
        if(got!=want)break;
    }
    if(left)memset(p,0,left);
    return frames;
}
static void music_open(void){
    FILE *f=fopen("/seaglass/theme.wav","rb");if(!f)return;
    unsigned char h[16];bool valid=false;
    if(fread(h,1,12,f)!=12||memcmp(h,"RIFF",4)||memcmp(h+8,"WAVE",4)){fclose(f);return;}
    while(fread(h,1,8,f)==8){
        uint32_t n=read32(h+4);long start=ftell(f);
        if(n>0x10000000u)break;
        if(!memcmp(h,"fmt ",4)){
            if(n<16||fread(h,1,16,f)!=16)break;
            music_rate=read32(h+4);
            valid=h[0]==1&&h[1]==0&&h[2]==2&&h[3]==0&&h[14]==16&&h[15]==0&&music_rate>=1024&&music_rate<=32768;
        }else if(!memcmp(h,"data",4)){
            if(!valid||n<4)break;
            music_file=f;music_start=start;music_bytes=music_left=n&~3u;
            mm_ds_system sys={0};sys.fifo_channel=7;mmInit(&sys);return;
        }
        if(fseek(f,start+(long)n+(n&1),SEEK_SET))break;
    }
    fclose(f);
}
static void music_update(void){
    if(!music_file)return;
    if(app_loading()){
        if(music_playing){mmStreamClose();music_playing=false;}
        return;
    }
    if(!music_playing){
        mm_stream stream={0};stream.sampling_rate=music_rate;stream.buffer_length=4096;
        stream.callback=music_callback;stream.format=MM_STREAM_16BIT_STEREO;
        stream.timer=MM_TIMER0;stream.manual=true;
        mmStreamOpen(&stream);music_playing=true;
    }
    /* FAT is accessed only from the main thread, never an audio interrupt. */
    mmStreamUpdate();
}
static void present(void){
    music_update();
    DC_FlushRange(ui_pixels,sizeof ui_pixels);
    /* Give each 96 KiB copy its own VBlank. Drawing happens in cached RAM,
       so a half-drawn screen is never visible while sprites are decoded. */
    swiWaitForVBlank();dmaCopy(ui_pixels[0],top_vram,sizeof ui_pixels[0]);
    swiWaitForVBlank();dmaCopy(ui_pixels[1],bottom_vram,sizeof ui_pixels[1]);
}
static unsigned map_keys(unsigned k){
    unsigned out=0;
    if(k&KEY_A)out|=UI_A;
    if(k&KEY_B)out|=UI_B;
    if(k&KEY_LEFT)out|=UI_LEFT;
    if(k&KEY_RIGHT)out|=UI_RIGHT;
    if(k&KEY_UP)out|=UI_UP;
    if(k&KEY_DOWN)out|=UI_DOWN;
    if(k&KEY_L)out|=UI_L;
    if(k&KEY_R)out|=UI_R;
    if(k&KEY_X)out|=UI_X;
    if(k&KEY_Y)out|=UI_Y;
    if(k&KEY_START)out|=UI_START;
    if(k&KEY_SELECT)out|=UI_SELECT;
    return out;
}
int main(void){
    videoSetMode(MODE_5_2D);videoSetModeSub(MODE_5_2D);
    vramSetBankA(VRAM_A_MAIN_BG);vramSetBankC(VRAM_C_SUB_BG);
    top_vram=bgGetGfxPtr(bgInit(3,BgType_Bmp16,BgSize_B16_256x256,0,0));
    bottom_vram=bgGetGfxPtr(bgInitSub(3,BgType_Bmp16,BgSize_B16_256x256,0,0));
    lcdMainOnTop();keysSetRepeat(22,5);
    bool storage_ok=fatInitDefault();
    app_init(storage_ok,present);
    if(storage_ok)music_open();
    app_draw();
    while(1){
        swiWaitForVBlank();scanKeys();
        unsigned kd=keysDown();touchPosition p={0};if(kd&KEY_TOUCH)touchRead(&p);
        UiInput in={map_keys(kd),map_keys(keysDownRepeat())&(UI_LEFT|UI_RIGHT|UI_UP|UI_DOWN|UI_L),
                    map_keys(keysHeld()),(kd&KEY_TOUCH)!=0,p.px,p.py};
        music_update();
        if(!app_input(in))break;
        app_draw();music_update();
    }
    if(music_playing)mmStreamClose();
    if(music_file)fclose(music_file);
    return 0;
}
