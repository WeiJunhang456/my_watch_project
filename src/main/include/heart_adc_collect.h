#ifndef HEART_ADC_COLLECT_H
#define HEART_ADC_COLLECT_H

#include <stdint.h>
#include "driver/adc.h"

/* 
 * PulseSensor 接 ESP32 GPIO34 (ADC1_CHANNEL_6)
 * 参考 ESP-WROOM-32 手册：IO34 = ADC1_CH6, RTC_GPIO4
 */
#define HEART_ADC_GPIO          34
#define HEART_ADC_CHANNEL       ADC1_CHANNEL_6

/* ADC原始采样滤波批次：每10次去头去尾平均一次 */
#define HEART_ADC_RAW_BATCH     5

/* BPM平滑缓存：3次有效心跳取平均后上报(这个值大于2) */
#define HEART_ADC_BPM_BUF_SIZE  3

void heart_adc_init(void);
int heart_adc_get_latest_bpm(void);
/* 通知ADC任务当前APP是否在前台 */
/* active = true  → 心率APP已打开，恢复200Hz高频检测 */
/* active = false → 心率APP已退出，进入1分钟一次的后台检测 */
void heart_adc_set_active(bool active);


#endif