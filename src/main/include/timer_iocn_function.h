#ifndef TIMER_ICON_FUNCTION_H
#define TIMER_ICON_FUNCTION_H

#include "lvgl.h"
#include "main/include/gui_app.h"
#include "esp_log.h"
#include "nvs_flash.h"
#include "nvs.h"
#include "driver/ledc.h"
#include "driver/gpio.h"

// ========== 常量定义 ==========
#define MAX_ALARMS          5
#define ALARM_NVS_NAMESPACE "alarm_storage"
#define ALARM_NVS_KEY       "alarm_data"

// ========== 闹钟数据结构 ==========
typedef struct {
    uint8_t hour;
    uint8_t minute;
    uint8_t weekday_mask;
    bool enabled;
    bool valid;
} alarm_setting_t;

// ========== 秒表状态结构体 ==========
typedef struct {
    lv_obj_t *label_time;
    lv_obj_t *btn_start;
    lv_obj_t *btn_reset;
    bool running;
    uint32_t elapsed_ms;
    uint32_t start_tick;
    lv_timer_t *timer;
} stopwatch_state_t;

// ========== 闹钟UI结构体 ==========
typedef struct {
    lv_obj_t *list_container;
    lv_obj_t *add_btn;
    lv_obj_t *setup_win;
    lv_obj_t *roller_hour;
    lv_obj_t *roller_minute;
    lv_obj_t *roller_weekday;
    lv_obj_t *switch_enable;
    lv_obj_t *notify_win;
    lv_timer_t *notify_timer;
} alarm_ui_t;

// ========== 定时器APP全局状态结构体 ==========
typedef struct {
    // Tabview
    lv_obj_t *tabview;
    lv_obj_t *tab_stopwatch;
    lv_obj_t *tab_alarm;
    
    // 秒表
    stopwatch_state_t sw;
    
    // 闹钟
    alarm_ui_t alarm;
    alarm_setting_t alarms[MAX_ALARMS];
    uint8_t alarm_count;
    
    // 星期选项字符串
    const char *weekday_options;
} timer_app_state_t;

// ========== 对外接口 ==========
void timer_icon_function_init(void);
void app_timer_window(lv_obj_t *parent);

#endif