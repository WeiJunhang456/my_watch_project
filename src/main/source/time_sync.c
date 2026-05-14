#include "main/include/time_sync.h"

static const char *TAG = "TIME_SYNC";
static bool time_synced = false;
static bool sntp_initialized = false;   //防止重复注册SNTP导致内存泄漏

//声明时间同步相关的结构体
struct watch_time_component watch_time;

// ========== 内部工具函数：格式化时间 ==========
static void get_formatted_time(char *buf, size_t size)
{
    time_t now;     // 时间戳（长整型，秒数）
    struct tm timeinfo;     // 分解后的时间结构体
    
    time(&now);     // 获取当前系统时间（从1970年1月1日00:00:00 UTC开始的秒数）
    
    if (time_synced && localtime_r(&now, &timeinfo) != NULL) {
        // 同步完成：格式化为本地时间
        strftime(buf, size, "%H:%M", &timeinfo);  // "14:30"
    } else {
        strncpy(buf, "--:--", size);  // 未同步时显示占位符
    }
}

// ========== 内部工具函数：格式化日期 ==========
static void get_formatted_date(char *buf, size_t size)
{
    time_t now;
    struct tm timeinfo;
    
    time(&now);
    
    if (time_synced && localtime_r(&now, &timeinfo) != NULL) {
        strftime(buf, size, "%Y.%m.%d", &timeinfo);  // "2026.05.11"
    } else {
        strncpy(buf, "----.--.--", size);  // 未同步时显示占位符
    }
}

// ========== LVGL定时器回调（在主线程执行，安全）==========
static void lv_time_update_cb(lv_timer_t *timer)
{
    char buf[16];
    
    // 更新时间
    // 获取格式化后的时间字符串
    get_formatted_time(buf, sizeof(buf));
    // 更新顶部小时间（主菜单页面）
    if (watch_time.label_top_time) {
        lv_label_set_text(watch_time.label_top_time, buf);
    }
    // 更新顶部小时间（主菜单页面）
    if (watch_time.label_home_time) {
        lv_label_set_text(watch_time.label_home_time, buf);
    }
    
    // 更新日期
    // 获取并更新日期
    get_formatted_date(buf, sizeof(buf));
    if (watch_time.label_home_date) {
        lv_label_set_text(watch_time.label_home_date, buf);
    }
}

// ========== 时间同步完成回调（SNTP调用）==========
static void time_sync_notification_cb(struct timeval *tv)
{
    time_synced = true;
    watch_time.sync_status = TIME_SYNC_COMPLETED;
    watch_time.time_valid = true;
    
    // 设置中国时区 (UTC+8)
    setenv("TZ", "CST-8", 1);
    tzset();
    
    ESP_LOGI(TAG, "时间同步完成，UTC时间戳: %ld", tv->tv_sec);
}

// ========== 对外接口：初始化时间显示 ==========
void time_display_init(void)
{  
    //注册LVGL定时器：每秒更新一次时间显示
    lv_timer_create(lv_time_update_cb, 1000, NULL);
    
    watch_time.sync_status = TIME_SYNC_NONE;
    watch_time.time_valid = false;
}

// ========== 对外接口：启动SNTP时间同步 ==========
void time_sync_init(void)
{
    if (sntp_initialized)return;    //通电注册过后就不再注册（一次注册）

    watch_time.sync_status = TIME_SYNC_IN_PROGRESS;
    
    // 配置SNTP,必须手动指定服务器
    sntp_setoperatingmode(SNTP_OPMODE_POLL);
    sntp_setservername(0, "ntp.aliyun.com");      // 国内优先阿里云
    sntp_setservername(1, "pool.ntp.org");        // 备用
    sntp_setservername(2, "time.cloudflare.com"); // 备用2

    // 使用ESP-IDF默认SNTP配置，不手动指定服务器
    // ESP-IDF会自动使用内置默认服务器或DHCP获取
    // sntp_setoperatingmode(SNTP_OPMODE_POLL);
    
    // 设置同步间隔：1小时
    sntp_set_sync_interval(3600000);  // 3600秒 = 1小时
    
    // 注册同步回调
    sntp_set_time_sync_notification_cb(time_sync_notification_cb);
    
    sntp_init();
    ESP_LOGI(TAG, "SNTP已启动，等待同步...");
    sntp_initialized = true;
}

// ========== 对外接口：阻塞等待首次同步 ==========
bool time_sync_wait_ready(uint32_t timeout_ms)
{
    uint32_t start = xTaskGetTickCount() * portTICK_PERIOD_MS;
    
    while (!time_synced) {
        if ((xTaskGetTickCount() * portTICK_PERIOD_MS - start) > timeout_ms) {
            ESP_LOGW(TAG, "时间同步超时");
            watch_time.sync_status = TIME_SYNC_FAILED;
            return false;
        }
        vTaskDelay(500 / portTICK_PERIOD_MS);
    }
    return true;
}

// ========== 对外接口：查询同步状态 ==========
bool time_is_synced(void)
{
    return time_synced;
}