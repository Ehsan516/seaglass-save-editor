#include "ui.h"
#include <string.h>

uint16_t ui_pixels[2][UI_W*UI_H] __attribute__((aligned(32)));
static uint16_t *canvas=ui_pixels[0];
/* Original compact 5x7 face. Proportional spacing; no terminal font or VRAM
   font tiles. All glyph writes are clipped to the requested pixel width. */
static const uint8_t glyphs[95][7]={
 {0}, {4,4,4,4,4,0,4}, {10,10,10,0,0,0,0}, {10,31,10,10,31,10,0},
 {4,15,20,14,5,30,4}, {25,26,2,4,8,11,19}, {12,18,20,8,21,18,13}, {4,4,8,0,0,0,0},
 {2,4,8,8,8,4,2}, {8,4,2,2,2,4,8}, {0,4,21,14,21,4,0}, {0,4,4,31,4,4,0},
 {0,0,0,0,0,4,8}, {0,0,0,31,0,0,0}, {0,0,0,0,0,0,4}, {1,2,2,4,8,8,16},
 {14,17,19,21,25,17,14}, {4,12,4,4,4,4,14}, {14,17,1,2,4,8,31}, {30,1,1,14,1,1,30},
 {2,6,10,18,31,2,2}, {31,16,16,30,1,1,30}, {6,8,16,30,17,17,14}, {31,1,2,4,8,8,8},
 {14,17,17,14,17,17,14}, {14,17,17,15,1,2,12}, {0,4,0,0,4,0,0}, {0,4,0,0,4,4,8},
 {1,2,4,8,4,2,1}, {0,0,31,0,31,0,0}, {16,8,4,2,4,8,16}, {14,17,1,2,4,0,4},
 {14,17,23,21,23,16,14}, {14,17,17,31,17,17,17}, {30,17,17,30,17,17,30}, {14,17,16,16,16,17,14},
 {30,17,17,17,17,17,30}, {31,16,16,30,16,16,31}, {31,16,16,30,16,16,16}, {14,17,16,23,17,17,15},
 {17,17,17,31,17,17,17}, {14,4,4,4,4,4,14}, {7,2,2,2,18,18,12}, {17,18,20,24,20,18,17},
 {16,16,16,16,16,16,31}, {17,27,21,21,17,17,17}, {17,25,25,21,19,19,17}, {14,17,17,17,17,17,14},
 {30,17,17,30,16,16,16}, {14,17,17,17,21,18,13}, {30,17,17,30,20,18,17}, {15,16,16,14,1,1,30},
 {31,4,4,4,4,4,4}, {17,17,17,17,17,17,14}, {17,17,17,17,17,10,4}, {17,17,17,21,21,27,17},
 {17,17,10,4,10,17,17}, {17,17,10,4,4,4,4}, {31,1,2,4,8,16,31}, {14,8,8,8,8,8,14},
 {16,8,8,4,2,2,1}, {14,2,2,2,2,2,14}, {4,10,17,0,0,0,0}, {0,0,0,0,0,0,31},
 {8,4,2,0,0,0,0}, {0,0,14,1,15,17,15}, {16,16,30,17,17,17,30}, {0,0,14,16,16,17,14},
 {1,1,15,17,17,17,15}, {0,0,14,17,31,16,14}, {6,9,8,28,8,8,8}, {0,14,17,17,15,1,14},
 {16,16,30,17,17,17,17}, {4,0,12,4,4,4,14}, {2,0,6,2,2,18,12}, {16,16,18,20,24,20,18},
 {12,4,4,4,4,4,14}, {0,0,26,21,21,21,21}, {0,0,30,17,17,17,17}, {0,0,14,17,17,17,14},
 {0,30,17,17,30,16,16}, {0,15,17,17,15,1,1}, {0,0,22,25,16,16,16}, {0,0,15,16,14,1,30},
 {8,8,28,8,8,9,6}, {0,0,17,17,17,17,15}, {0,0,17,17,17,10,4}, {0,0,17,17,21,21,10},
 {0,0,17,10,4,10,17}, {0,17,17,17,15,1,14}, {0,0,31,2,4,8,31}, {2,4,4,8,4,4,2},
 {4,4,4,4,4,4,4}, {8,4,4,2,4,4,8}, {0,0,8,21,2,0,0}
};
static int next_char(const char **s){
    unsigned char c=(unsigned char)*(*s)++;
    if(c==0xc3 && (unsigned char)**s==0xa9){ (*s)++; return 'e'; }
    if(c>=128){ while(((unsigned char)**s&0xc0)==0x80) (*s)++; return '?'; }
    return c>=32 && c<=126?c:'?';
}
static void bounds(int c,int *left,int *right){
    int mask=0; for(int y=0;y<7;y++) mask|=glyphs[c-32][y];
    *left=0; *right=4;
    if(!mask){ *right=1; return; }
    while(!(mask&(16>>*left))) (*left)++;
    while(!(mask&(16>>*right))) (*right)--;
}
void ui_screen(int screen){ canvas=ui_pixels[screen!=0]; }
void ui_rect(int x,int y,int w,int h,uint16_t color){
    int x2=x+w,y2=y+h;
    if(x<0)x=0;
    if(y<0)y=0;
    if(x2>UI_W)x2=UI_W;
    if(y2>UI_H)y2=UI_H;
    for(int j=y;j<y2;j++) for(int i=x;i<x2;i++) canvas[j*UI_W+i]=color;
}
void ui_round(UiRect r,int radius,uint16_t color){
    for(int y=0;y<r.h;y++){
        int inset=0;
        if(y<radius || y>=r.h-radius){
            int dy=y<radius?radius-1-y:y-(r.h-radius);
            while(inset<radius && (radius-inset)*(radius-inset)+dy*dy>radius*radius) inset++;
        }
        ui_rect(r.x+inset,r.y+y,r.w-2*inset,1,color);
    }
}
void ui_panel(UiRect r,bool selected){
    ui_round((UiRect){r.x,r.y+1,r.w,r.h},4,UI_RGB(166,184,207));
    ui_round(r,4,selected?UI_ACCENT:UI_LINE);
    int inset=selected?2:1;
    ui_round((UiRect){r.x+inset,r.y+inset,r.w-2*inset,r.h-2*inset},3,selected?UI_PALE:UI_WHITE);
}
int ui_text_width(const char *s){
    int w=0; while(*s){ int l,r,c=next_char(&s); bounds(c,&l,&r); w+=r-l+2; } return w?w-1:0;
}
void ui_text(int x,int y,const char *s,uint16_t color,int max_width){
    int end=x+max_width;
    while(*s){
        int c=next_char(&s),l,r; bounds(c,&l,&r);
        int w=r-l+1; if(x+w>end) break;
        for(int j=0;j<7;j++) for(int i=l;i<=r;i++)
            if(glyphs[c-32][j]&(16>>i)) ui_rect(x+i-l,y+j,1,1,color);
        x+=w+1;
    }
}
void ui_center(UiRect r,const char *s,uint16_t color){
    int w=ui_text_width(s); if(w>r.w-4)w=r.w-4;
    ui_text(r.x+(r.w-w)/2,r.y+(r.h-7)/2,s,color,r.w-4);
}
void ui_button(UiRect r,const char *label,bool primary){
    ui_round(r,4,primary?UI_ACCENT:UI_RGB(218,227,240));
    ui_center(r,label,primary?UI_WHITE:UI_INK);
}
void ui_heading(int x,int y,const char *s,uint16_t color,int max_width){
    int end=x+max_width;
    while(*s){
        int c=next_char(&s),l,r;bounds(c,&l,&r);
        int w=(r-l+1)*2;if(x+w>end)break;
        for(int j=0;j<7;j++)for(int i=l;i<=r;i++)
            if(glyphs[c-32][j]&(16>>i))ui_rect(x+(i-l)*2,y+j*2,2,2,color);
        x+=w+2;
    }
}
void ui_ball(int cx,int cy,int radius,uint16_t color){
    for(int y=-radius;y<=radius;y++) for(int x=-radius;x<=radius;x++){
        int d=x*x+y*y;
        if(d<=radius*radius && (d>(radius-2)*(radius-2) || (y>=-1&&y<=1))) ui_rect(cx+x,cy+y,1,1,color);
    }
    ui_round((UiRect){cx-3,cy-3,7,7},3,color);
    ui_round((UiRect){cx-1,cy-1,3,3},1,UI_WHITE);
}
void ui_background(const char *title,const char *tag){
    for(int y=0;y<UI_H;y++) ui_rect(0,y,UI_W,1,UI_RGB(224-y/9,235-y/12,246-y/16));
    /* Quiet glass facets, laid behind the cards. */
    for(int i=0;i<10;i++){
        int x=(i*67+19)%250,y=26+(i*41)%155;
        ui_rect(x,y,3+(i%3)*3,3+(i%3)*3,UI_RGB(233,243,250));
    }
    ui_rect(0,0,256,22,UI_BLUE); ui_rect(0,22,256,1,UI_WHITE);
    ui_ball(12,10,6,UI_WHITE);
    ui_text(24,7,title,UI_WHITE,174);
    if(tag) ui_text(250-ui_text_width(tag),7,tag,UI_RGB(199,231,242),60);
}
void ui_sprite(int x,int y,int size,const uint16_t *pixels){
    for(int j=0;j<size;j++) for(int i=0;i<size;i++){
        uint16_t c=pixels[(j*64/size)*64+i*64/size];
        if(c&0x8000) ui_rect(x+i,y+j,1,1,c);
    }
}
bool ui_hit(UiRect r,int x,int y){ return x>=r.x&&y>=r.y&&x<r.x+r.w&&y<r.y+r.h; }
