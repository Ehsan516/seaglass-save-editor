/* Small, portable RGB555 renderer shared by the DS and host preview. */
#pragma once
#include <stdint.h>
#include <stdbool.h>
#define UI_W 256
#define UI_H 192
#define UI_RGB(r,g,b) ((uint16_t)(0x8000 | ((r)>>3) | (((g)>>3)<<5) | (((b)>>3)<<10)))
#define UI_INK UI_RGB(40,52,80)
#define UI_MUTED UI_RGB(103,119,148)
#define UI_WHITE UI_RGB(248,252,255)
#define UI_ACCENT UI_RGB(26,133,151)
#define UI_BLUE UI_RGB(75,91,164)
#define UI_LINE UI_RGB(190,204,222)
#define UI_PALE UI_RGB(226,242,244)
#define UI_GOLD UI_RGB(225,165,66)
#define UI_RED UI_RGB(178,64,88)
typedef struct { int x,y,w,h; } UiRect;
extern uint16_t ui_pixels[2][UI_W*UI_H];
void ui_screen(int screen);
void ui_rect(int x,int y,int w,int h,uint16_t color);
void ui_round(UiRect r,int radius,uint16_t color);
void ui_panel(UiRect r,bool selected);
void ui_text(int x,int y,const char *s,uint16_t color,int max_width);
void ui_heading(int x,int y,const char *s,uint16_t color,int max_width);
int ui_text_width(const char *s);
void ui_center(UiRect r,const char *s,uint16_t color);
void ui_button(UiRect r,const char *label,bool primary);
void ui_background(const char *title,const char *tag);
void ui_ball(int cx,int cy,int radius,uint16_t color);
void ui_sprite(int x,int y,int size,const uint16_t *pixels);
bool ui_hit(UiRect r,int x,int y);
