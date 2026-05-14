#ifndef WEATHER_ICON_FUNCTION_H
#define WEATHER_ICON_FUNCTION_H

#include "lvgl.h"
#include "main/include/gui_app.h"

#define FORECAST_DAYS           3
#define WEATHER_CONDITION_LEN   16
#define WEATHER_DATE_LEN        12
#define WEATHER_REGION_LEN      24

/*======================== 数据结构 ========================*/

typedef struct {
    int16_t temperature;
    char condition[WEATHER_CONDITION_LEN];
} current_weather_t;

typedef struct {
    char date[WEATHER_DATE_LEN];
    char condition[WEATHER_CONDITION_LEN];
    int16_t temp_high;
    int16_t temp_low;
    int16_t temp_avg;
} forecast_day_t;

typedef struct {
    current_weather_t current;
    forecast_day_t forecast[FORECAST_DAYS];
    bool valid;      // 数据是否有效
    bool updated;    // 标记有新数据待UI刷新
} weather_data_t;

typedef struct {
    lv_obj_t *cont_upper;
    lv_obj_t *cont_lower;

    lv_obj_t *dd_province;
    lv_obj_t *dd_city;
    lv_obj_t *dd_district;

    lv_obj_t *label_temp;
    lv_obj_t *label_condition;

    lv_obj_t *cont_forecast;
    lv_obj_t *day_cont[FORECAST_DAYS];
    lv_obj_t *label_date[FORECAST_DAYS];
    lv_obj_t *label_fc_cond[FORECAST_DAYS];
    lv_obj_t *label_temp_avg[FORECAST_DAYS];
    lv_obj_t *line_split[FORECAST_DAYS - 1];

    bool inited;     // UI是否已创建
} weather_ui_t;

typedef struct {
    weather_ui_t ui;
    weather_data_t data;

    uint8_t sel_province;
    uint8_t sel_city;
    uint8_t sel_district;

    bool need_sync;      // 触发立即同步
    bool net_inited;     // 网络任务是否已创建
    lv_timer_t *ui_timer;
} weather_app_state_t;

/*======================== 对外接口 ========================*/
void app_weather_window(lv_obj_t *parent);
void weather_icon_function_init(void);
void weather_trigger_sync(void);

#endif
