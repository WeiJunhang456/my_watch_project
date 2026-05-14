#ifndef WIFI_ICON_FUNCTION_H
#define WIFI_ICON_FUNCTION_H

#include "lvgl.h"
#include "main/include/gui_app.h"
#include "esp_wifi.h"

void app_bluetooth_window(lv_obj_t *parent);
void update_wifi_list(wifi_ap_record_t *ap_records, int count);
void wifi_icon_function_init(void);
void wifi_scan_task(void *pvParameters);   /* 新增：供main.c统一创建任务 */

#endif