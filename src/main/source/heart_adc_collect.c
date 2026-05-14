#include "main/include/heart_adc_collect.h"
#include "main/include/heart_iocn_function.h"
#include "esp_log.h"
#include "esp_adc_cal.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#define HEART_ADC_TAG           "HEART_ADC"

#define REFRACTORY_MS           200
#define BPM_VALID_MIN           30
#define BPM_VALID_MAX           220

/* 包络线自适应参数：加快跟踪速度，提升PulseSensor响应 */
#define ENVELOPE_TRACK_STEP     3
#define ENVELOPE_DEAD_ZONE      10

/* 后台模式参数 */
#define BACKGROUND_INTERVAL_MS  60000   /* 后台休眠间隔：60秒 */
#define BACKGROUND_WINDOW_MS    5000    /* 后台快速采样窗口：5秒 */
#define BACKGROUND_INIT_SAMPLES 30      /* 后台窗口初始化采样数 */

static const char *TAG = "HEART_ADC";

/*==================== 模式控制（跨任务访问） ====================*/
static volatile bool g_adc_active = false;      /* true=前台正常模式, false=后台低频模式 */
static esp_timer_handle_t g_sample_timer = NULL;

/*==================== 算法状态（文件级，方便模式切换时重置） ====================*/
static int latest_bpm = 0;
static int bpm_buffer[HEART_ADC_BPM_BUF_SIZE];
static int bpm_idx = 0;
static int bpm_count = 0;

static int baseline = 2048;       /* 信号直流基线（PulseSensor静止时约1.65V） */
static int envelope_min = 4095;
static int envelope_max = 0;
static int prev_smoothed = 0;
static uint32_t last_beat_ms = 0;
static TaskHandle_t adc_task_handle = NULL;

static int raw_buf[HEART_ADC_RAW_BATCH];
static int raw_idx = 0;
static bool foreground_init_done = false;

/*==================== 工具函数 ====================*/

/*批量滤波：去除最高值和最低值后计算平均值*/
static int heart_filter_batch(const int *samples, int count)
{
    int sum = 0, min_val = 4095, max_val = 0;
    for (int i = 0; i < count; i++) {
        int v = samples[i];
        if (v < min_val) min_val = v;
        if (v > max_val) max_val = v;
        sum += v;
    }
    return (sum - min_val - max_val) / (count - 2);
}

/* BPM缓存滤波：去除最高最低BPM后平均，输出平滑心率*/
static int heart_filter_bpm(void)
{
    if (bpm_count < HEART_ADC_BPM_BUF_SIZE) return 0;
    int sum = 0, min_bpm = 999, max_bpm = 0;
    for (int i = 0; i < HEART_ADC_BPM_BUF_SIZE; i++) {
        int v = bpm_buffer[i];
        if (v < min_bpm) min_bpm = v;
        if (v > max_bpm) max_bpm = v;
        sum += v;
    }
    return (sum - min_bpm - max_bpm) / (HEART_ADC_BPM_BUF_SIZE - 2);
}

/*==================== ADC 中断与任务 ====================*/

static void heart_adc_timer_cb(void *arg)
{
    BaseType_t xHigherPriorityTaskWoken = pdFALSE;
    if (adc_task_handle) {
        vTaskNotifyGiveFromISR(adc_task_handle, &xHigherPriorityTaskWoken);
    }
    if (xHigherPriorityTaskWoken) {
        portYIELD_FROM_ISR();
    }
}

/* 重置所有算法状态，用于后台模式每次新窗口开始时清零旧数据*/
static void heart_adc_reset_algorithm(void)
{
    bpm_idx = 0;
    bpm_count = 0;
    envelope_min = 4095;
    envelope_max = 0;
    prev_smoothed = 0;
    last_beat_ms = 0;
    baseline = 2048;
    raw_idx = 0;
}

/* 处理单个ADC采样点（正常模式与后台窗口共用）*/
static void heart_process_one_sample(int raw)
{
    raw_buf[raw_idx++] = raw;
    if (raw_idx < HEART_ADC_RAW_BATCH) return;
    raw_idx = 0;

    int smoothed = heart_filter_batch(raw_buf, HEART_ADC_RAW_BATCH);

    if (smoothed >= envelope_max) {
        envelope_max = smoothed;
    } else if (envelope_max > smoothed + ENVELOPE_DEAD_ZONE) {
        envelope_max -= ENVELOPE_TRACK_STEP;
    }

    if (smoothed <= envelope_min) {
        envelope_min = smoothed;
    } else if (envelope_min < smoothed - ENVELOPE_DEAD_ZONE) {
        envelope_min += ENVELOPE_TRACK_STEP;
    }

    if (envelope_max < baseline + 5) envelope_max = baseline + 5;
    if (envelope_min > baseline - 5) envelope_min = baseline - 5;

    baseline = (envelope_min + envelope_max) / 2;

    int amplitude = envelope_max - envelope_min;
    int threshold = baseline + amplitude / 3;

    uint32_t now = esp_timer_get_time() / 1000;

    if (prev_smoothed <= threshold && smoothed > threshold) {
        if (now - last_beat_ms > REFRACTORY_MS) {
            uint32_t interval = now - last_beat_ms;
            if (interval >= (60000 / BPM_VALID_MAX) && interval <= (60000 / BPM_VALID_MIN)) {
                int bpm = 60000 / interval;

                bpm_buffer[bpm_idx++] = bpm;
                if (bpm_idx >= HEART_ADC_BPM_BUF_SIZE) bpm_idx = 0;
                if (bpm_count < HEART_ADC_BPM_BUF_SIZE) bpm_count++;

                if (bpm_count >= HEART_ADC_BPM_BUF_SIZE) {
                    latest_bpm = heart_filter_bpm();
                    heart_data_change((float)latest_bpm);
                }
            }
            last_beat_ms = now;
        }
    }

    prev_smoothed = smoothed;
}


/*==================== 对外接口 ====================*/
void heart_adc_set_active(bool active)
{
    bool old = g_adc_active;
    g_adc_active = active;

    if (old == active) return;  /* 状态无变化，不操作定时器 */

    if (!g_sample_timer) return;

    /* 切换到前台时，唤醒正在休眠的任务 */
    if (active && adc_task_handle) {
        // xTaskNotifyGive(adc_task_handle);
        xTaskNotify(adc_task_handle, 1, eSetValueWithOverwrite);
    }

    if (active) {
        /* 切换到前台：启动高频定时器 5ms */
        esp_timer_stop(g_sample_timer);
        /* 重置算法状态，立即适应新信号 */
        heart_adc_reset_algorithm();

        esp_err_t err = esp_timer_start_periodic(g_sample_timer, 5000);
        if (err == ESP_OK) {
            // ESP_LOGI(TAG, "Switch to FOREGROUND mode (200Hz)");
        }
    } 
    // else {
    //     /* 切换到后台：停止高频定时器，任务将进入vTaskDelay休眠 */
    //     esp_timer_stop(g_sample_timer);
    //     ESP_LOGI(TAG, "Switch to BACKGROUND mode (1min interval)");
    // }
}

static void heart_adc_task(void *pv)
{
    adc1_config_width(ADC_WIDTH_BIT_12);
    adc1_config_channel_atten(HEART_ADC_CHANNEL, ADC_ATTEN_DB_11);

    esp_adc_cal_characteristics_t adc_chars;
    esp_adc_cal_characterize(ADC_UNIT_1, ADC_ATTEN_DB_11, ADC_WIDTH_BIT_12, 1100, &adc_chars);

    adc_task_handle = xTaskGetCurrentTaskHandle();

    esp_timer_create_args_t timer_args = {
        .callback = heart_adc_timer_cb,
        .arg = NULL,
        .name = "adc_sample_timer",
        .skip_unhandled_events = true
    };
    /* 创建定时器并保存到全局变量 g_sample_timer */
    ESP_ERROR_CHECK(esp_timer_create(&timer_args, &g_sample_timer));
   
    while (1) {
        if (!g_adc_active) {
            /*==================================================
             * 后台模式：休眠60秒，然后5秒快速采样窗口
             * 此期间CPU占用接近0%，不影响其他APP
             *==================================================*/
            /* 切到后台时，重置前台初始化标志，下次进前台重新初始化 */
            foreground_init_done = false;

            ulTaskNotifyTake(pdTRUE, 0);  /* 清空可能残留的定时器通知 */

            /* 用 ulTaskNotifyTake 代替 vTaskDelay，收到通知立即醒来 */
            uint32_t notify_val = ulTaskNotifyTake(pdTRUE, pdMS_TO_TICKS(BACKGROUND_INTERVAL_MS));

            /* 如果收到通知（notify_val > 0）且已切前台，直接跳过 */
            if (notify_val > 0 && g_adc_active) {
                continue;
            }

            /* 休眠结束后，如果APP仍未打开，执行一次快速检测 */
            if (!g_adc_active) {
                heart_adc_reset_algorithm();

                /* 初始化心率设置 */
                for (int i = 0; i < BACKGROUND_INIT_SAMPLES; i++) {
                    int v = adc1_get_raw(HEART_ADC_CHANNEL);
                    if (v < envelope_min) envelope_min = v;
                    if (v > envelope_max) envelope_max = v;
                    vTaskDelay(pdMS_TO_TICKS(5));
                }
                uint32_t offset = (envelope_max - envelope_min) / 4;
                envelope_min = (envelope_min > (int)offset) ? envelope_min - offset : 0;
                envelope_max = envelope_max + offset;
                if (envelope_max > 4095) envelope_max = 4095;
                baseline = (envelope_min + envelope_max) / 2;
                prev_smoothed = baseline;

                ESP_LOGI(TAG, "Background window start, baseline=%d", baseline);

                /* 5秒快速采样窗口（1000次@5ms），期间检测心跳 */
                for (int i = 0; i < 1000 && !g_adc_active; i++) {
                    int raw = adc1_get_raw(HEART_ADC_CHANNEL);
                    heart_process_one_sample(raw);
                    vTaskDelay(pdMS_TO_TICKS(5));
                }

                /* 窗口结束，如有有效BPM则上报 */
                if (bpm_count >= HEART_ADC_BPM_BUF_SIZE) {
                    latest_bpm = heart_filter_bpm();
                    heart_data_change((float)latest_bpm);
                    ESP_LOGI(TAG, "Background window done, BPM=%d", latest_bpm);
                } else {
                    ESP_LOGW(TAG, "Background window done, no valid BPM");
                }
            }
        } else {
            /*==================================================
             * 前台正常模式：200Hz持续采样，等待定时器通知
             *==================================================*/
            /* 前台模式首次进入时，初始化心率设置 */
            if (!foreground_init_done) {
                foreground_init_done = true;
                for (int i = 0; i < 30; i++) {
                    ulTaskNotifyTake(pdTRUE, portMAX_DELAY);
                    int v = adc1_get_raw(HEART_ADC_CHANNEL);
                    if (v < envelope_min) envelope_min = v;
                    if (v > envelope_max) envelope_max = v;
                }
                baseline = (envelope_min + envelope_max) / 2;
                prev_smoothed = baseline;
                // ESP_LOGI(TAG, "Foreground init done, baseline=%d", baseline);
            }

            ulTaskNotifyTake(pdTRUE, portMAX_DELAY);

            int raw = adc1_get_raw(HEART_ADC_CHANNEL);
            heart_process_one_sample(raw);
        }
    }
}

void heart_adc_init(void)
{
    static bool initialized = false;
    if (initialized) return;
    initialized = true;

    xTaskCreatePinnedToCore(heart_adc_task, "heart_adc", 4096, NULL, 5, NULL,0);
    ESP_LOGI(TAG, "Heart ADC task created (GPIO%d, 100Hz, PulseSensor)", HEART_ADC_GPIO);
}

int heart_adc_get_latest_bpm(void)
{
    return latest_bpm;
}