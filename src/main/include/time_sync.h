#ifndef TIME_SYNC_H
#define TIME_SYNC_H

#include "lvgl.h"
#include "main/include/gui_app.h"
#include "esp_log.h"
#include "time.h"           // 标准C时间库
#include "sys/time.h"       // gettimeofday
#include "esp_sntp.h"

// 初始化时间显示（创建标签 + 注册LVGL定时器）
void time_display_init(void);
// 启动SNTP时间同步
void time_sync_init(void);
// 阻塞等待首次同步完成
bool time_sync_wait_ready(uint32_t timeout_ms);
// 查询当前同步状态
bool time_is_synced(void);

#endif