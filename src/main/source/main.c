#include <stdio.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_log.h"
#include "esp_task_wdt.h"   // 用于关闭看门狗
#include "lvgl.h"
#include "main/include/disp_driver.h"
#include "main/include/touch_driver.h"  // 稍后打开
#include "main/include/gui_app.h"
#include "main/include/wifi_iocn_function.h"
#include "main/include/heart_adc_collect.h" 
#include "main/include/time_sync.h"
#include "nvs_flash.h"      // nvs_flash_init()
#include "esp_netif.h"      // esp_netif_init(), esp_netif_create_default_wifi_sta()
#include "esp_event.h"      // esp_event_loop_create_default(), ESP_EVENT_ANY_ID
#include "esp_wifi.h"       // esp_wifi_init(), esp_wifi_set_mode(), esp_wifi_start(), WIFI_MODE_STA


// #define TAG "MAIN"

//声明并初始化句柄
static TaskHandle_t flush_finish_task_handle = NULL;
static TaskHandle_t lvgl_task_handle = NULL;

// 传输完成监控任务
static void lvgl_flush_finish_task(void *arg) {
    spi_transaction_t *rtrans;
    while (1) {
        esp_err_t ret = spi_device_get_trans_result(spi_dev, &rtrans, portMAX_DELAY);
        if (ret == ESP_OK) {
            free(rtrans);   // 记得释放（或用固定池）
            if (g_trans_pending > 0) g_trans_pending--;
            if (g_trans_pending == 0 && g_disp_drv != NULL) {
                lv_disp_flush_ready(g_disp_drv);
            }
        }
    }
}

static void lvgl_handler_task(void *arg) {
    while (1) {
        // 更聪明的调用方式：让 LVGL 自己决定是否需要延迟
        uint32_t delay = lv_timer_handler();
        if (delay > 0 && delay < 1000) {
            vTaskDelay(pdMS_TO_TICKS(delay));
        } else {
            vTaskDelay(pdMS_TO_TICKS(5));
        }
    }
}


void app_main(void) {
    // 彻底关闭任务看门狗，避免 IDLE 饿死报警
    esp_task_wdt_deinit();

    /*ESP-IDF 的推荐初始顺序是：
    初始化基础服务（NVS、日志）。
    初始化所有硬件外设（SPI、I²C、GPIO）。
    初始化图形库（LVGL port）。
    创建 FreeRTOS 任务（LVGL tick、handler）。
    创建 UI。
    主任务进入无限循环等待*/

    /*硬件初始化放在前面，在任务调度之前，避免任务调度频繁中断干扰初始化
      同时也可以避免不同步启动产生的电流尖峰（可能造成瞬间压降）、信号干扰等硬件问题 */
    //LCD+触摸屏硬件初始化
    lcd_init();
    app_touch_init();
    //LVGL驱动初始化（包含LVGL核心）
    lvgl_disp_init();
    //注册硬件设备，告诉LVGL输入设备是触摸屏
    touch_lvgl_register();

     /* WiFi 硬件初始化 */
    // 初始化NVS（非易失性存储）：WiFi驱动需要从Flash读取射频校准数据和MAC地址
    // 必要性：不执行这行，esp_wifi_init()会返回ESP_ERR_NVS_NOT_INITIALIZED错误
    // WiFi 硬件初始化（放在 LVGL 显示 ready 之后，避免射频校准打断 SPI）
    esp_err_t ret = nvs_flash_init();
    if (ret == ESP_ERR_NVS_NO_FREE_PAGES || ret == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_ERROR_CHECK(nvs_flash_erase());
        ret = nvs_flash_init();
    }
    ESP_ERROR_CHECK(ret);
    // 初始化网络接口抽象层（Netif）：作为WiFi驱动和LwIP TCP/IP协议栈之间的桥梁
    // 必要性：后续的esp_netif_create_default_wifi_sta()依赖它，否则无法获取IP地址
    ESP_ERROR_CHECK(esp_netif_init());
    // 创建默认事件循环：用于接收WiFi驱动产生的连接/断开/获取IP等事件通知
    // 必要性：wifi_icon_function_init()里注册的事件回调必须依附于此循环，否则收不到状态变化
    ESP_ERROR_CHECK(esp_event_loop_create_default());
    // 创建STA模式的默认网络接口实例：将WiFi驱动与LwIP的DHCP客户端绑定
    // 必要性：连接WiFi后自动通过DHCP获取IP地址，没有它只能连上但无法上网
    esp_netif_create_default_wifi_sta();
    // 生成WiFi驱动的默认初始化配置：包含射频校准模式、最大连接数、缓存大小等参数
    // 必要性：esp_wifi_init()需要这个结构体，宏填充官方推荐默认值避免手动配置出错
    wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
    // 真正初始化WiFi驱动：分配内存、启动WiFi任务、加载射频固件到硬件
    // 必要性：前面5行都是铺垫，这行才是WiFi硬件的"开机"，之后才能扫描/连接
    ESP_ERROR_CHECK(esp_wifi_init(&cfg));
    // 设置WiFi工作模式为STA（客户端模式）：让ESP32作为设备去连接外部路由器
    // 必要性：必须显式指定模式，默认可能是NULL模式，无法执行扫描和连接操作
    ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_STA));
    // 启动WiFi射频硬件：让底层任务进入工作状态，射频开始收发信号
    // 必要性：esp_wifi_init()只是软件初始化，这行才真正打开硬件，之后才能扫描和连接
    ESP_ERROR_CHECK(esp_wifi_start());

    //自定义样式、图标等初始化
    watch_gui_init();
    
    // 心率传感器ADC采集初始化（后台独立任务，持续采样）
    heart_adc_init();
    
    // 创建必需的任务
    //SPI+DMA任务
    xTaskCreatePinnedToCore(lvgl_flush_finish_task, "flush_finish", 2048, NULL, 4, &flush_finish_task_handle,0);
    //将LVGL任务强制指定核心1去运行
    xTaskCreatePinnedToCore(lvgl_handler_task, "handler", 4096, NULL, 3, &lvgl_task_handle,1);
    xTaskCreatePinnedToCore(wifi_scan_task, "wifi_scan",4096, NULL, 2, NULL,  0);

    
    // 防止主任务退出
    while (1) {
        vTaskDelay(pdMS_TO_TICKS(1000));
    }
}

