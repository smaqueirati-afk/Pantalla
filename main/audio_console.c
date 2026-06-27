/**
 * @file audio_console.c  — fullscreen 1024×600, barras ancladas al fondo
 */
#include <stdio.h>
#include <string.h>
#include <stdbool.h>
#include "lvgl.h"
#include "esp_log.h"
#include "audio_console.h"
#include "app_nav.h"

static const char *TAG = "audio_console";

/* ── Layout ─────────────────────────────────────── */
#define SCR_W   1024
#define SCR_H   600
#define TOP_H   44
#define LEFT_W  390
#define PAD     12
#define SL_W    68
#define SL_H    430
#define SL_GAP  38
#define SPEC_N  24
#define VU_H    20

/* ── Colores ─────────────────────────────────────── */
#define C_BG      0x0D0D1A
#define C_TOP     0x11112A
#define C_LINE    0x1E1E3A
#define C_TRACK   0x1A1A2E
#define C_KNOB    0xD0D8FF
#define C_FILL2   0x00D4FF
#define C_LABEL   0x7F77DD
#define C_VALUE   0x00D4FF
#define C_DIM     0x5050AA
#define C_ZERO    0x5060AA
#define C_SPEC_BG 0x111122
#define C_VU_BG   0x1A1A2E
#define C_MUTE_BD 0xCC4444
#define C_MUTE_TX 0xCC6666
#define C_BTN_BD  0x3A3A6A
#define C_TITLE   0xC8C8FF
#define C_NAV     0xA0A0CC
#define C_FREQ    0x3A3A6A

static const int8_t ZONE[SPEC_N]={0,0,0,0,0,0,0,0,0,0,0,1,1,1,2,2,0,0,0,0,0,0,0,0};
static const float  BASE[SPEC_N]={
    0.30f,0.42f,0.55f,0.65f,0.72f,0.78f,0.74f,0.69f,
    0.76f,0.82f,0.88f,0.91f,0.97f,0.85f,0.78f,0.82f,
    0.76f,0.71f,0.68f,0.65f,0.60f,0.55f,0.50f,0.44f};

static struct {
    lv_obj_t *trk[3],*fill[3],*knob[3],*val_lbl[3]; int db[3];
    lv_obj_t *bar[SPEC_N],*peak[SPEC_N];
    float h[SPEC_N],ph[SPEC_N];
    int   bar_base_y;   /* Y absoluta dentro de sbox donde vive la base */
    int   bar_h_max;    /* max px de las barras */
    int   bar_w;
    lv_obj_t *vu_fill[2],*vu_val[2]; int vu_w;
    lv_obj_t *btn_mute,*lbl_mute; bool muted;
    lv_timer_t *tmr;
} ac;

static lv_color_t hx(uint32_t c){return lv_color_hex(c);}
static lv_color_t zbar(int z){return z==2?hx(0xDD6600):z==1?hx(0xDDAA00):hx(0x22CC44);}
static lv_color_t zpeak(int z){return z==2?hx(0xFF8800):z==1?hx(0xFFDD00):hx(0x44FF88);}

static lv_obj_t* plain(lv_obj_t*p,int x,int y,int w,int h,uint32_t c){
    lv_obj_t*o=lv_obj_create(p);if(!o)return NULL;
    lv_obj_set_size(o,w,h);lv_obj_set_pos(o,x,y);
    lv_obj_set_style_bg_color(o,hx(c),0);lv_obj_set_style_bg_opa(o,LV_OPA_COVER,0);
    lv_obj_set_style_border_width(o,0,0);lv_obj_set_style_radius(o,0,0);
    lv_obj_set_style_shadow_width(o,0,0);lv_obj_set_style_pad_all(o,0,0);
    lv_obj_clear_flag(o,LV_OBJ_FLAG_SCROLLABLE|LV_OBJ_FLAG_CLICKABLE);return o;}

__attribute__((weak)) void on_audio_back(void)     {app_nav_goto(SCREEN_SPOTIFY);}
__attribute__((weak)) void on_audio_master(int db) {(void)db;}
__attribute__((weak)) void on_audio_bass(int db)   {(void)db;}
__attribute__((weak)) void on_audio_treble(int db) {(void)db;}
__attribute__((weak)) void on_audio_mute(bool m)   {(void)m;}

static float db_to_pct(int db){return(float)(db+24)/36.f;}
static int   pct_to_db(float p){return LV_CLAMP(-24,(int)(p*36.f+.5f)-24,12);}

static void sl_apply(int i,float pct){
    pct=LV_CLAMP(0.f,pct,1.f); ac.db[i]=pct_to_db(pct);
    int fh=LV_MAX(2,(int)(pct*SL_H));
    if(ac.fill[i]){lv_obj_set_height(ac.fill[i],fh);lv_obj_align(ac.fill[i],LV_ALIGN_BOTTOM_MID,0,0);}
    if(ac.knob[i])lv_obj_set_y(ac.knob[i],LV_CLAMP(0,SL_H-fh-9,SL_H-18));
    char b[8];snprintf(b,8,ac.db[i]>=0?"+%d":"%d",ac.db[i]);
    if(ac.val_lbl[i])lv_label_set_text(ac.val_lbl[i],b);}

static void cb_sl(lv_event_t*e){
    int i=(int)(intptr_t)lv_event_get_user_data(e);
    if(i<0||i>2)return;
    lv_indev_t*ind=lv_indev_get_act();if(!ind)return;
    lv_point_t pt;lv_indev_get_point(ind,&pt);
    lv_area_t a;lv_obj_get_coords(ac.trk[i],&a);
    int H=a.y2-a.y1;if(H<=0)return;
    float pct=1.f-(float)(pt.y-a.y1)/(float)H;
    sl_apply(i,pct);
    if(i==0)on_audio_master(ac.db[0]);
    if(i==1)on_audio_bass(ac.db[1]);
    if(i==2)on_audio_treble(ac.db[2]);}

static void cb_mute(lv_event_t*e){
    (void)e;ac.muted=!ac.muted;
    if(ac.btn_mute)lv_obj_set_style_bg_color(ac.btn_mute,ac.muted?hx(C_MUTE_BD):hx(0x151528),0);
    if(ac.lbl_mute)lv_obj_set_style_text_color(ac.lbl_mute,ac.muted?hx(0xFFFFFF):hx(C_MUTE_TX),0);
    on_audio_mute(ac.muted);}

static void cb_back(lv_event_t*e){(void)e;on_audio_back();}

/* ── ANIMACIÓN ──────────────────────────────────── */
static void anim_tick(lv_timer_t*t){
    (void)t;
    int bmax=ac.bar_h_max, base=ac.bar_base_y;
    for(int i=0;i<SPEC_N;i++){
        if(!ac.bar[i]||!ac.peak[i])continue;
        float noise=((float)(lv_rand(0,200))/200.f-0.5f)*0.2f;
        float tgt=BASE[i]+noise; if(ac.muted)tgt*=0.05f;
        tgt=LV_CLAMP(0.04f,tgt,1.f);
        ac.h[i]=ac.h[i]*0.5f+tgt*0.5f;
        if(ac.h[i]>ac.ph[i])ac.ph[i]=ac.h[i];
        else ac.ph[i]=LV_MAX(ac.h[i],ac.ph[i]-0.02f);

        int bh=LV_MAX(2,(int)(ac.h[i]*bmax));
        lv_obj_set_height(ac.bar[i],bh);
        lv_obj_set_y(ac.bar[i],base-bh);          /* base fija, crece hacia arriba */

        int py=LV_MAX(0,base-(int)(ac.ph[i]*bmax)-3);
        lv_obj_set_y(ac.peak[i],py);
    }
    /* VU */
    int lw=ac.muted?0:(int)(LV_CLAMP(0.f,0.62f+((float)(lv_rand(0,100))/100.f)*0.22f,1.f)*ac.vu_w);
    int rw=ac.muted?0:(int)(LV_CLAMP(0.f,0.52f+((float)(lv_rand(0,100))/100.f)*0.28f,1.f)*ac.vu_w);
    if(ac.vu_fill[0])lv_obj_set_width(ac.vu_fill[0],lw);
    if(ac.vu_fill[1])lv_obj_set_width(ac.vu_fill[1],rw);
    if(ac.vu_val[0]&&ac.vu_w>0){char b[8];int d=(int)((float)lw/ac.vu_w*18.f)-18;
        snprintf(b,8,ac.muted?"--":"%d",ac.muted?0:d);lv_label_set_text(ac.vu_val[0],b);}
    if(ac.vu_val[1]&&ac.vu_w>0){char b[8];int d=(int)((float)rw/ac.vu_w*18.f)-18;
        snprintf(b,8,ac.muted?"--":"%d",ac.muted?0:d);lv_label_set_text(ac.vu_val[1],b);}}

/* ── CREATE ─────────────────────────────────────── */
void audio_console_create(lv_obj_t *parent)
{
    ESP_LOGI(TAG,"Creando audio console");
    memset(&ac,0,sizeof(ac));
    for(int i=0;i<SPEC_N;i++){ac.h[i]=BASE[i];ac.ph[i]=BASE[i];}

    lv_obj_set_style_bg_color(parent,hx(C_BG),0);
    lv_obj_set_style_bg_opa(parent,LV_OPA_COVER,0);
    lv_obj_set_style_pad_all(parent,0,0);
    lv_obj_set_style_border_width(parent,0,0);
    lv_obj_clear_flag(parent,LV_OBJ_FLAG_SCROLLABLE);

    /* ── TOPBAR ── */
    lv_obj_t*top=plain(parent,0,0,SCR_W,TOP_H,C_TOP);
    if(!top)return;
    lv_obj_add_flag(top,LV_OBJ_FLAG_CLICKABLE);

    lv_obj_t*bbtn=lv_btn_create(top);
    if(bbtn){lv_obj_set_size(bbtn,120,32);lv_obj_align(bbtn,LV_ALIGN_LEFT_MID,8,0);
        lv_obj_set_style_bg_color(bbtn,hx(0x151528),0);lv_obj_set_style_bg_opa(bbtn,LV_OPA_COVER,0);
        lv_obj_set_style_border_color(bbtn,hx(C_BTN_BD),0);lv_obj_set_style_border_width(bbtn,1,0);
        lv_obj_set_style_shadow_width(bbtn,0,0);lv_obj_set_style_radius(bbtn,7,0);
        lv_obj_add_event_cb(bbtn,cb_back,LV_EVENT_CLICKED,NULL);
        lv_obj_t*bl=lv_label_create(bbtn);
        if(bl){lv_label_set_text(bl,LV_SYMBOL_LEFT" Musica");
            lv_obj_set_style_text_color(bl,hx(C_NAV),0);
            lv_obj_set_style_text_font(bl,&lv_font_montserrat_16,0);lv_obj_center(bl);}}

    lv_obj_t*ttl=lv_label_create(top);
    if(ttl){lv_label_set_text(ttl,LV_SYMBOL_AUDIO"  Audio Console");
        lv_obj_set_style_text_color(ttl,hx(C_TITLE),0);
        lv_obj_set_style_text_font(ttl,&lv_font_montserrat_20,0);
        lv_obj_align(ttl,LV_ALIGN_CENTER,0,0);}

    ac.btn_mute=lv_btn_create(top);
    if(ac.btn_mute){lv_obj_set_size(ac.btn_mute,88,32);
        lv_obj_align(ac.btn_mute,LV_ALIGN_RIGHT_MID,-8,0);
        lv_obj_set_style_bg_color(ac.btn_mute,hx(0x151528),0);
        lv_obj_set_style_bg_opa(ac.btn_mute,LV_OPA_COVER,0);
        lv_obj_set_style_border_color(ac.btn_mute,hx(C_MUTE_BD),0);
        lv_obj_set_style_border_width(ac.btn_mute,1,0);
        lv_obj_set_style_shadow_width(ac.btn_mute,0,0);
        lv_obj_set_style_radius(ac.btn_mute,7,0);
        lv_obj_add_event_cb(ac.btn_mute,cb_mute,LV_EVENT_CLICKED,NULL);
        ac.lbl_mute=lv_label_create(ac.btn_mute);
        if(ac.lbl_mute){lv_label_set_text(ac.lbl_mute,"MUTE");
            lv_obj_set_style_text_color(ac.lbl_mute,hx(C_MUTE_TX),0);
            lv_obj_set_style_text_font(ac.lbl_mute,&lv_font_montserrat_16,0);
            lv_obj_center(ac.lbl_mute);}}

    plain(parent,0,TOP_H,SCR_W,1,C_LINE);
    plain(parent,LEFT_W,TOP_H+1,1,SCR_H-TOP_H-1,C_LINE);

    /* ── SLIDERS ── */
    static const char*SN[]={"Master","Bass","Treble"};
    static const int IDBV[]={4,3,8};
    int ct=TOP_H+1, ch=SCR_H-ct;
    int total3=3*SL_W+2*SL_GAP;
    int slx0=(LEFT_W-total3)/2;
    int sly=ct+(ch-SL_H)/2;                 /* slider Y — mismo para panel derecho */
    int zero_y=(int)(SL_H*(1.f-24.f/36.f));

    for(int i=0;i<3;i++){
        int sx=slx0+i*(SL_W+SL_GAP);
        lv_obj_t*lb=lv_label_create(parent);
        if(lb){lv_label_set_text(lb,SN[i]);
            lv_obj_set_style_text_font(lb,&lv_font_montserrat_16,0);
            lv_obj_set_style_text_color(lb,hx(C_LABEL),0);
            lv_obj_set_pos(lb,sx+SL_W/2-22,sly-22);}
        lv_obj_t*trk=lv_obj_create(parent);
        if(!trk)continue;
        lv_obj_set_size(trk,SL_W,SL_H);lv_obj_set_pos(trk,sx,sly);
        lv_obj_set_style_bg_color(trk,hx(C_TRACK),0);lv_obj_set_style_bg_opa(trk,LV_OPA_COVER,0);
        lv_obj_set_style_border_color(trk,hx(0x2A2A4A),0);lv_obj_set_style_border_width(trk,1,0);
        lv_obj_set_style_radius(trk,6,0);lv_obj_set_style_shadow_width(trk,0,0);
        lv_obj_set_style_pad_all(trk,0,0);
        lv_obj_clear_flag(trk,LV_OBJ_FLAG_SCROLLABLE);lv_obj_add_flag(trk,LV_OBJ_FLAG_CLICKABLE);
        lv_obj_add_event_cb(trk,cb_sl,LV_EVENT_PRESSING,(void*)(intptr_t)i);
        lv_obj_add_event_cb(trk,cb_sl,LV_EVENT_CLICKED,(void*)(intptr_t)i);
        ac.trk[i]=trk;
        static const char*MK[]={"+12","+6","0","-6","-12","-18","-24"};
        for(int m=0;m<7;m++){lv_obj_t*ml=lv_label_create(trk);if(!ml)continue;
            lv_label_set_text(ml,MK[m]);
            lv_obj_set_style_text_font(ml,&lv_font_montserrat_10,0);
            lv_obj_set_style_text_color(ml,hx(m<2?0x00B4CC:m==2?C_ZERO:0x2A4A7A),0);
            lv_obj_set_pos(ml,3,(int)((float)m/6.f*(SL_H-12)));
            lv_obj_clear_flag(ml,LV_OBJ_FLAG_CLICKABLE);}
        lv_obj_t*zl=lv_obj_create(trk);
        if(zl){lv_obj_set_size(zl,SL_W-2,1);lv_obj_set_pos(zl,0,zero_y);
            lv_obj_set_style_bg_color(zl,hx(C_ZERO),0);lv_obj_set_style_bg_opa(zl,LV_OPA_COVER,0);
            lv_obj_set_style_border_width(zl,0,0);lv_obj_set_style_shadow_width(zl,0,0);
            lv_obj_clear_flag(zl,LV_OBJ_FLAG_CLICKABLE|LV_OBJ_FLAG_SCROLLABLE);}
        lv_obj_t*fill=lv_obj_create(trk);
        if(fill){lv_obj_set_size(fill,SL_W-2,2);lv_obj_align(fill,LV_ALIGN_BOTTOM_MID,0,0);
            lv_obj_set_style_bg_color(fill,hx(C_FILL2),0);lv_obj_set_style_bg_opa(fill,LV_OPA_COVER,0);
            lv_obj_set_style_border_width(fill,0,0);lv_obj_set_style_radius(fill,3,0);
            lv_obj_set_style_shadow_width(fill,0,0);
            lv_obj_clear_flag(fill,LV_OBJ_FLAG_CLICKABLE|LV_OBJ_FLAG_SCROLLABLE);}
        ac.fill[i]=fill;
        lv_obj_t*knob=lv_obj_create(trk);
        if(knob){lv_obj_set_size(knob,SL_W-6,18);lv_obj_set_pos(knob,3,0);
            lv_obj_set_style_bg_color(knob,hx(C_KNOB),0);lv_obj_set_style_bg_opa(knob,LV_OPA_COVER,0);
            lv_obj_set_style_border_width(knob,0,0);lv_obj_set_style_radius(knob,4,0);
            lv_obj_set_style_shadow_width(knob,0,0);
            lv_obj_clear_flag(knob,LV_OBJ_FLAG_CLICKABLE|LV_OBJ_FLAG_SCROLLABLE);}
        ac.knob[i]=knob;
        lv_obj_t*vl=lv_label_create(parent);
        if(vl){lv_label_set_text(vl,"+0");
            lv_obj_set_style_text_font(vl,&lv_font_montserrat_18,0);
            lv_obj_set_style_text_color(vl,hx(C_VALUE),0);
            lv_obj_set_pos(vl,sx+SL_W/2-16,sly+SL_H+8);}
        ac.val_lbl[i]=vl;
        sl_apply(i,db_to_pct(IDBV[i]));}

    /* ── PANEL DERECHO — alineado con sly ── */
    int rx=LEFT_W+1+PAD;
    int rw=SCR_W-LEFT_W-1-PAD*2;
    /* VU box: altura fija 80px, anclado al fondo del slider */
    int sl_bottom = sly + SL_H;          /* Y donde terminan los sliders */
    int vu_box_h  = 80;
    int vu_y      = sl_bottom - vu_box_h; /* VU alineado al fondo */
    /* Spectrum box: desde sly hasta encima del VU */
    int spec_y    = sly;
    int spec_h    = vu_y - spec_y - 8;   /* gap 8px entre spectrum y VU */

    /* Spectrum box */
    lv_obj_t*sbox=lv_obj_create(parent);
    if(!sbox)goto done;
    lv_obj_set_size(sbox,rw,spec_h); lv_obj_set_pos(sbox,rx,spec_y);
    lv_obj_set_style_bg_color(sbox,hx(C_SPEC_BG),0);lv_obj_set_style_bg_opa(sbox,LV_OPA_COVER,0);
    lv_obj_set_style_border_color(sbox,hx(C_LINE),0);lv_obj_set_style_border_width(sbox,1,0);
    lv_obj_set_style_radius(sbox,7,0);lv_obj_set_style_shadow_width(sbox,0,0);
    lv_obj_set_style_pad_all(sbox,9,0);
    lv_obj_clear_flag(sbox,LV_OBJ_FLAG_SCROLLABLE|LV_OBJ_FLAG_CLICKABLE);

    {lv_obj_t*st=lv_label_create(sbox);
     if(st){lv_label_set_text(st,"SPECTRUM");
         lv_obj_set_style_text_font(st,&lv_font_montserrat_12,0);
         lv_obj_set_style_text_color(st,hx(C_DIM),0);
         lv_obj_align(st,LV_ALIGN_TOP_MID,0,0);}}

    /* Barras: content area = spec_h - 18px padding */
    {
        int sbox_content_h = spec_h - 18;         /* content area altura */
        int title_h  = 18;                         /* espacio para "SPECTRUM" */
        int freq_h   = 20;                         /* espacio para etiquetas freq */
        int bar_area = sbox_content_h - title_h - freq_h - 4;  /* altura maxima barras */
        if(bar_area < 50) bar_area = 50;

        /* base_y EN COORDENADAS DEL CONTENT AREA DE SBOX */
        int base_y = title_h + bar_area;           /* base de barras (fondo del area) */

        ac.bar_h_max   = bar_area;
        ac.bar_base_y  = base_y;                  /* guardado para anim_tick */

        int bpad=0;
        int bar_area_w = rw - 18;
        int bw = (bar_area_w - SPEC_N*2) / SPEC_N;
        if(bw<4)bw=4;
        ac.bar_w=bw;

        for(int i=0;i<SPEC_N;i++){
            int bx=bpad+i*(bw+2);
            lv_obj_t*bar=lv_obj_create(sbox);
            if(bar){
                lv_obj_set_size(bar,bw,2);
                lv_obj_set_pos(bar,bx,base_y-2);     /* posicion inicial: en la base */
                lv_obj_set_style_bg_color(bar,zbar(ZONE[i]),0);
                lv_obj_set_style_bg_opa(bar,LV_OPA_COVER,0);
                lv_obj_set_style_border_width(bar,0,0);lv_obj_set_style_radius(bar,2,0);
                lv_obj_set_style_shadow_width(bar,0,0);
                lv_obj_clear_flag(bar,LV_OBJ_FLAG_CLICKABLE|LV_OBJ_FLAG_SCROLLABLE);}
            ac.bar[i]=bar;
            lv_obj_t*pk=lv_obj_create(sbox);
            if(pk){lv_obj_set_size(pk,bw,2);lv_obj_set_pos(pk,bx,title_h);
                lv_obj_set_style_bg_color(pk,zpeak(ZONE[i]),0);
                lv_obj_set_style_bg_opa(pk,LV_OPA_COVER,0);
                lv_obj_set_style_border_width(pk,0,0);lv_obj_set_style_radius(pk,1,0);
                lv_obj_set_style_shadow_width(pk,0,0);
                lv_obj_clear_flag(pk,LV_OBJ_FLAG_CLICKABLE|LV_OBJ_FLAG_SCROLLABLE);}
            ac.peak[i]=pk;}

        /* Freq labels debajo de las barras */
        static const char*FL[]={"20","100","500","2k","8k","20k"};
        for(int i=0;i<6;i++){
            lv_obj_t*fl=lv_label_create(sbox);if(!fl)continue;
            lv_label_set_text(fl,FL[i]);
            lv_obj_set_style_text_font(fl,&lv_font_montserrat_10,0);
            lv_obj_set_style_text_color(fl,hx(C_FREQ),0);
            lv_obj_set_pos(fl,bpad+i*(bar_area_w/6),base_y+4);}
    }

    /* VU box */
    {
        lv_obj_t*vubox=lv_obj_create(parent);
        if(!vubox)goto done;
        lv_obj_set_size(vubox,rw,vu_box_h);lv_obj_set_pos(vubox,rx,vu_y);
        lv_obj_set_style_bg_color(vubox,hx(C_SPEC_BG),0);lv_obj_set_style_bg_opa(vubox,LV_OPA_COVER,0);
        lv_obj_set_style_border_color(vubox,hx(C_LINE),0);lv_obj_set_style_border_width(vubox,1,0);
        lv_obj_set_style_radius(vubox,7,0);lv_obj_set_style_shadow_width(vubox,0,0);
        lv_obj_set_style_pad_all(vubox,10,0);
        lv_obj_clear_flag(vubox,LV_OBJ_FLAG_SCROLLABLE|LV_OBJ_FLAG_CLICKABLE);

        int vu_mw=rw-20-22-10; ac.vu_w=vu_mw;
        static const char*VCH[]={"L","R"};
        for(int i=0;i<2;i++){
            int vy=2+i*(VU_H+12);
            lv_obj_t*ch=lv_label_create(vubox);
            if(ch){lv_label_set_text(ch,VCH[i]);
                lv_obj_set_style_text_font(ch,&lv_font_montserrat_16,0);
                lv_obj_set_style_text_color(ch,hx(C_DIM),0);lv_obj_set_pos(ch,0,vy);}
            lv_obj_t*vtrk=lv_obj_create(vubox);
            if(!vtrk)continue;
            lv_obj_set_size(vtrk,vu_mw,VU_H);lv_obj_set_pos(vtrk,20,vy+1);
            lv_obj_set_style_bg_color(vtrk,hx(C_VU_BG),0);lv_obj_set_style_bg_opa(vtrk,LV_OPA_COVER,0);
            lv_obj_set_style_border_width(vtrk,0,0);lv_obj_set_style_radius(vtrk,3,0);
            lv_obj_set_style_shadow_width(vtrk,0,0);lv_obj_set_style_pad_all(vtrk,0,0);
            lv_obj_clear_flag(vtrk,LV_OBJ_FLAG_SCROLLABLE|LV_OBJ_FLAG_CLICKABLE);
            lv_obj_t*vf=lv_obj_create(vtrk);
            if(vf){lv_obj_set_size(vf,0,VU_H);lv_obj_set_pos(vf,0,0);
                lv_obj_set_style_bg_color(vf,hx(0x22CC44),0);lv_obj_set_style_bg_opa(vf,LV_OPA_COVER,0);
                lv_obj_set_style_border_width(vf,0,0);lv_obj_set_style_radius(vf,3,0);
                lv_obj_set_style_shadow_width(vf,0,0);
                lv_obj_clear_flag(vf,LV_OBJ_FLAG_SCROLLABLE|LV_OBJ_FLAG_CLICKABLE);}
            ac.vu_fill[i]=vf;
            lv_obj_t*vv=lv_label_create(vubox);
            if(vv){lv_label_set_text(vv,"-3");
                lv_obj_set_style_text_font(vv,&lv_font_montserrat_12,0);
                lv_obj_set_style_text_color(vv,hx(C_DIM),0);
                lv_obj_set_pos(vv,20+vu_mw+6,vy+3);}
            ac.vu_val[i]=vv;}}

done:
    ac.tmr=lv_timer_create(anim_tick,80,NULL);
    ESP_LOGI(TAG,"Audio console lista");}

void audio_console_tick(void){}
