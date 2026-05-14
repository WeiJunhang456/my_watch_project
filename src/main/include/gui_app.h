#ifndef _GUI_APP_H
#define _GUI_APP_H

#include "lvgl.h"
#include "APP_Font_Awesome.h"
#include "main/include/app_function.h"
#include "main/include/time_sync.h"

#define WATCH_SCREEN_W 240  // 手表屏幕宽度
#define WATCH_SCREEN_H 320 // 手表屏幕高度
#define TAB_TOTAL_PAGE  2       //主菜单总页数
#define CELL_COUNT   9      //主菜单一页的图标上限
#define APP_NUM  4         //app总个数
#define PAGE_ONE  0
#define PAGE_TWO  1
#define ICON_SIZE  56   // 图标尺寸

//创建结构体对象，管理图标、图片、文本
struct  watch_window
{
    lv_obj_t *  scr_window_main;
    lv_obj_t *  scr_tile_page_main ;
    lv_obj_t *  scr_tile_page1 ;
    lv_obj_t *  scr_tile_page2 ;
    lv_obj_t *  scr_home_page;
    lv_obj_t *  scr_home_bottom_page;
};

struct  watch_function
{
    lv_obj_t *  wifi_list;
};

struct watch_battery_component
{
    lv_obj_t *battery_container;
    lv_obj_t *battery_bar;
    lv_obj_t *battery_label;
};

struct label_img
{
    lv_obj_t * img_weather;
    lv_obj_t * img_heart;
    lv_obj_t * img_run;
    lv_obj_t * label_weather_temp;   
};

// 时间同步状态
typedef enum {
    TIME_SYNC_NONE = 0,
    TIME_SYNC_IN_PROGRESS,
    TIME_SYNC_COMPLETED,
    TIME_SYNC_FAILED
} time_sync_status_t;

// 扩展watch_window结构体（或新建）
struct watch_time_component {
    lv_obj_t *label_top_time;       // 顶部时间标签
    lv_obj_t *label_home_time;      // 首页大时间（替换原来的静态"10:30"）
    lv_obj_t *label_home_date;      // 首页日期标签（新增）
    time_sync_status_t sync_status;
    bool time_valid;                // 时间是否有效
    char time_str[16];              // "HH:MM"
    char date_str[16];              // "YYYY.MM.D"
};

extern struct watch_window watch_window;
extern struct watch_function watch_function;
extern struct watch_battery_component watch_battery_component;
extern struct label_img label_img;
extern struct watch_time_component watch_time;


/*===================== 16个APP图标 ===================== */
// static const char *icon_list[16] = {
//     FA_HOME,          FA_MUSIC,          FA_COG,           FA_CLOCK,
//     FA_ENVELOPE,      FA_USER,          FA_BLUETOOTH,     FA_CALENDAR_DAYS,
//     FA_SUN,           FA_PHONE,         FA_VOLUME,  FA_HEART,
//     FA_HEARTBEAT,     FA_TINT,          FA_LUNGS,         FA_WALK
// };

static const char *icon_list[16] = {
    FA_CLOCK, FA_BLUETOOTH, FA_SUN, FA_HEART,
};

// 16套主流UI配色（宏定义，易懂、易改、易移植）
#define COLOR_HOME_TOP       LV_COLOR_MAKE(100,181,246)
#define COLOR_HOME_BOT       LV_COLOR_MAKE(33,150,243)
#define COLOR_MUSIC_TOP       LV_COLOR_MAKE(29,185,84)
#define COLOR_MUSIC_BOT       LV_COLOR_MAKE(18,18,18)
#define COLOR_COG_TOP        LV_COLOR_MAKE(149,117,205)
#define COLOR_COG_BOT        LV_COLOR_MAKE(103,58,183)
#define COLOR_CLOCK_TOP      LV_COLOR_MAKE(79,195,247)
#define COLOR_CLOCK_BOT      LV_COLOR_MAKE(3,169,244)
#define COLOR_ENVELOPE_TOP   LV_COLOR_MAKE(77,208,225)
#define COLOR_ENVELOPE_BOT   LV_COLOR_MAKE(0,172,193)
#define COLOR_USER_TOP       LV_COLOR_MAKE(240,98,146)
#define COLOR_USER_BOT       LV_COLOR_MAKE(216,27,96)
#define COLOR_BLUETOOTH_TOP  LV_COLOR_MAKE(100,255,218)
#define COLOR_BLUETOOTH_BOT  LV_COLOR_MAKE(0,191,165)
#define COLOR_BATTERY_TOP    LV_COLOR_MAKE(129,199,132)
#define COLOR_BATTERY_BOT    LV_COLOR_MAKE(67,160,71)
#define COLOR_SUN_TOP        LV_COLOR_MAKE(255,213,79)
#define COLOR_SUN_BOT        LV_COLOR_MAKE(255,160,0)
#define COLOR_PHONE_TOP      LV_COLOR_MAKE(129,199,132)
#define COLOR_PHONE_BOT      LV_COLOR_MAKE(39,174,96)
#define COLOR_PHONE_VOL_TOP  LV_COLOR_MAKE(66,165,245)
#define COLOR_PHONE_VOL_BOT  LV_COLOR_MAKE(25,118,210)
#define COLOR_HEART_TOP      LV_COLOR_MAKE(239,83,80)
#define COLOR_HEART_BOT      LV_COLOR_MAKE(198,40,40)
#define COLOR_HEARTBEAT_TOP  LV_COLOR_MAKE(255,107,107)
#define COLOR_HEARTBEAT_BOT  LV_COLOR_MAKE(185,28,28)
#define COLOR_TINT_TOP       LV_COLOR_MAKE(100,181,246)
#define COLOR_TINT_BOT       LV_COLOR_MAKE(21,101,192)
#define COLOR_LUNGS_TOP      LV_COLOR_MAKE(126,87,194)
#define COLOR_LUNGS_BOT      LV_COLOR_MAKE(69,39,160)
#define COLOR_WALK_TOP       LV_COLOR_MAKE(255,183,77)
#define COLOR_WALK_BOT       LV_COLOR_MAKE(245,124,0)

// 颜色数组
static const lv_color_t color_top[16] = {
    COLOR_HOME_TOP,      COLOR_MUSIC_TOP,      COLOR_COG_TOP,       COLOR_CLOCK_TOP,
    COLOR_ENVELOPE_TOP,  COLOR_USER_TOP,      COLOR_BLUETOOTH_TOP, COLOR_BATTERY_TOP,
    COLOR_SUN_TOP,       COLOR_PHONE_TOP,     COLOR_PHONE_VOL_TOP, COLOR_HEART_TOP,
    COLOR_HEARTBEAT_TOP, COLOR_TINT_TOP,      COLOR_LUNGS_TOP,     COLOR_WALK_TOP
};

static const lv_color_t color_bottom[16] = {
    COLOR_HOME_BOT,      COLOR_MUSIC_BOT,      COLOR_COG_BOT,       COLOR_CLOCK_BOT,
    COLOR_ENVELOPE_BOT,  COLOR_USER_BOT,      COLOR_BLUETOOTH_BOT, COLOR_BATTERY_BOT,
    COLOR_SUN_BOT,       COLOR_PHONE_BOT,     COLOR_PHONE_VOL_BOT, COLOR_HEART_BOT,
    COLOR_HEARTBEAT_BOT, COLOR_TINT_BOT,      COLOR_LUNGS_BOT,     COLOR_WALK_BOT
};

//extern const lv_font_t lv_font_chinese_16;   // 声明字体变量
//中文字体
extern const lv_font_t lv_font_chinese_14;   // 声明字体变量
extern const lv_font_t lv_font_chinese_16;   // 声明字体变量
extern const lv_font_t lv_font_chinese_24;   // 声明字体变量
//图标用
extern const lv_font_t lv_font_fontawesome_28;
extern const lv_font_t lv_font_fontawesome_24;
extern const lv_font_t lv_font_fontawesome_32;

//声明仿宋字体
LV_FONT_DECLARE(lv_font_chinese_14);
LV_FONT_DECLARE(lv_font_chinese_16);
LV_FONT_DECLARE(lv_font_chinese_24);

//声明内部字体
LV_FONT_DECLARE(lv_font_montserrat_10);
LV_FONT_DECLARE(lv_font_montserrat_48);
LV_FONT_DECLARE(lv_font_montserrat_24);

//声明图标
LV_FONT_DECLARE(lv_font_fontawesome_28);
LV_FONT_DECLARE(lv_font_fontawesome_24);
LV_FONT_DECLARE(lv_font_fontawesome_32);

//声明天气图片
LV_IMG_DECLARE(sun);
LV_IMG_DECLARE(cloudey);
LV_IMG_DECLARE(thunder);
LV_IMG_DECLARE(rain);
LV_IMG_DECLARE(run);
LV_IMG_DECLARE(heatr);
LV_IMG_DECLARE(battery);

//声明lv_style_t样式，方便各个文件调用
extern lv_style_t style_app_icon_circle;

lv_obj_t *create_mainstream_icon(lv_obj_t *parent, const char *icon_str,const lv_font_t *font_style,
                                        lv_color_t color_top, lv_color_t color_bottom,uint8_t app_index);
/* GUI 初始化：创建所有页面，加载表盘为首页 */
void watch_gui_init(void);
//点击动画
void app_icon_click_anim(lv_event_t *e);


//void debug_chinese_font(void);

#endif //_GUI_APP_H
