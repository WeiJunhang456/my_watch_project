#ifndef HEART_ICON_FUNCTION_H
#define HEART_ICON_FUNCTION_H

#include "lvgl.h"
#include "main/include/gui_app.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"

/*========== 心率判定阈值（可修改） ==========*/
#define HEART_NORMAL_MIN             60
#define HEART_NORMAL_MAX             100
#define HEART_CRITICAL_MIN           40
#define HEART_CRITICAL_MAX           140

/*========== 心率数值刷新周期 ==========*/
#define HEART_UI_UPDATE_INTERVAL_MS  200

typedef enum {
    HEART_STATUS_UNKNOWN = 0,
    HEART_STATUS_NORMAL,
    HEART_STATUS_LOW,
    HEART_STATUS_HIGH,
    HEART_STATUS_CRITICAL
} heart_status_t;

typedef struct {
    float value;
    bool valid;
    bool updated;
    heart_status_t status;
    bool is_normal;
} heart_data_t;

typedef struct {
    lv_obj_t *cont_upper;
    lv_obj_t *cont_lower;
    lv_obj_t *label_heart_icon;
    lv_obj_t *label_heart_value;
    lv_obj_t *label_heart_unit;
    lv_obj_t *label_status_title;
    lv_obj_t *label_status_detail;
    lv_obj_t *label_suggestion_title;
    lv_obj_t *label_suggestion_text;
    bool inited;
} heart_ui_t;

typedef struct {
    heart_ui_t ui;
    heart_data_t data;
    lv_timer_t *ui_timer;             /* LVGL定时器，用于线程安全的UI刷新 */
    /* 控制心率数值多久刷新一次，运行时可直接修改 */
    uint32_t ui_update_interval_ms;
    uint32_t last_ui_update_tick;
} heart_app_state_t;

void app_heart_window(lv_obj_t *parent);
void heart_icon_function_init(void);
void heart_data_change(float dat);
/* APP退出时调用，释放定时器、重置状态，防止野指针 */
void heart_icon_function_deinit(void);

#endif