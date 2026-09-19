#pragma once
#include <stdbool.h>
#include <stdint.h>
enum { UI_A=1,UI_B=2,UI_LEFT=4,UI_RIGHT=8,UI_UP=16,UI_DOWN=32,
       UI_L=64,UI_R=128,UI_X=256,UI_Y=512,UI_START=1024,UI_SELECT=2048 };
typedef struct { unsigned down,repeat,held; bool touch; int x,y; } UiInput;
/* Present is invoked during blocking ROM loading, and once after a changed
   screen. The platform owns VRAM and VBlank; the app never prints to it. */
void app_init(bool storage_ok,void (*present)(void));
bool app_input(UiInput input); /* false requests exit */
void app_draw(void);
bool app_loading(void);
