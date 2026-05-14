#include "main/include/wifi_iocn_function.h"
#include "main/include/time_sync.h"
#include "esp_event.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/semphr.h"
#include "esp_log.h"
#include "nvs_flash.h"    
#include "nvs.h"            

#define WIFI_TAG "WIFI_ICON"

/* nvs保存WIFI模式、账号、密码 */
#define NVS_NAMESPACE "wifi_cfg"
#define NVS_KEY_SSID  "ssid"
#define NVS_KEY_PASS  "pass"

/* ========== WiFi扫描与连接相关 ========== */
static char saved_connected_ssid[33] = {0};   // 持久保存已连接WIFI的SSID
static char wifi_pending_ssid[33] = {0};      // 当前正在输入密码的目标SSID
#define WIFI_SCAN_LIST_SIZE  20
static wifi_ap_record_t wifi_scan_records[WIFI_SCAN_LIST_SIZE];
static uint16_t wifi_scan_count = 0;
static SemaphoreHandle_t wifi_data_mutex = NULL;

// 连接状态标志（由WiFi事件处理函数设置，LVGL timer读取）
static volatile bool wifi_scan_updated_flag = false;
static volatile bool wifi_connect_success_flag = false;
static volatile bool wifi_connect_failed_flag = false;
static volatile bool user_initiated_disconnect = false;
static char pending_connected_ssid[33] = {0};
static lv_timer_t *wifi_ui_refresh_timer = NULL;    //wifi列表刷新时间

/* 声明函数 */
static void rebuild_wifi_list(void);
static lv_obj_t *wifi_btn_find_label(lv_obj_t *btn);
static void wifi_ui_refresh_timer_cb(lv_timer_t *timer);
static void wifi_connect_real(const char *ssid, const char *password);
static void add_wifi_item(lv_obj_t *list, const char *ssid);
static void wifi_btn_event_cb(lv_event_t *e);
static void wifi_save_credentials(const char *ssid, const char *password);
static void wifi_clear_credentials(void);
static bool wifi_load_credentials(char *ssid, size_t ssid_len, char *pass, size_t pass_len);

/* ========== NVS工具函数 ========== */
/*将账号、密码保存到nvs中*/
static void wifi_save_credentials(const char *ssid, const char *password) {
    nvs_handle_t nvs_handle;
    esp_err_t err = nvs_open(NVS_NAMESPACE, NVS_READWRITE, &nvs_handle);
    if (err != ESP_OK) {
        ESP_LOGE(WIFI_TAG, "nvs_open failed: %s", esp_err_to_name(err));
        return;
    }
    nvs_set_str(nvs_handle, NVS_KEY_SSID, ssid);
    nvs_set_str(nvs_handle, NVS_KEY_PASS, password);
    nvs_commit(nvs_handle);     //nvs_commit做改写操作
    nvs_close(nvs_handle);
    ESP_LOGI(WIFI_TAG, "credentials saved to nvs");
}

/*将账号、密码从nvs中删除*/
static void wifi_clear_credentials(void) {
    nvs_handle_t nvs_handle;
    esp_err_t err = nvs_open(NVS_NAMESPACE, NVS_READWRITE, &nvs_handle);
    if (err != ESP_OK) return;
    nvs_erase_key(nvs_handle, NVS_KEY_SSID);
    nvs_erase_key(nvs_handle, NVS_KEY_PASS);
    nvs_commit(nvs_handle);     //nvs_commit做改写操作
    nvs_close(nvs_handle);
    ESP_LOGI(WIFI_TAG, "credentials cleared from nvs");
}

/*nvs中读取WIFI账号、密码*/
static bool wifi_load_credentials(char *ssid, size_t ssid_len, char *pass, size_t pass_len) {
    nvs_handle_t nvs_handle;
    esp_err_t err = nvs_open(NVS_NAMESPACE, NVS_READONLY, &nvs_handle);
    if (err != ESP_OK) return false;

    err = nvs_get_str(nvs_handle, NVS_KEY_SSID, ssid, &ssid_len);
    if (err != ESP_OK) {
        nvs_close(nvs_handle);
        return false;
    }
    err = nvs_get_str(nvs_handle, NVS_KEY_PASS, pass, &pass_len);
    nvs_close(nvs_handle);

    return (err == ESP_OK && ssid[0] != '\0');
}


/* ========== 工具函数 ========== */
/* 从按钮对象中查找真正的 lv_label 子对象 */
static lv_obj_t *wifi_btn_find_label(lv_obj_t *btn) {
    if (btn == NULL) return NULL;
    uint32_t cnt = lv_obj_get_child_cnt(btn);
    for (uint32_t i = 0; i < cnt; i++) {
        lv_obj_t *child = lv_obj_get_child(btn, i);
        if (lv_obj_check_type(child, &lv_label_class)) {
            return child;
        }
    }
    return NULL;
}

/* ========== WiFi事件处理（运行在系统事件任务，禁止直接操作LVGL） ========== */
static void wifi_event_handler(void* arg, esp_event_base_t event_base,
                               int32_t event_id, void* event_data) {
    if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_STA_DISCONNECTED) {
        wifi_event_sta_disconnected_t *disconn = (wifi_event_sta_disconnected_t *)event_data;
        /* reason=2 AUTH_FAIL(密码错), reason=201 NO_AP(找不到AP), reason=15 4WAY_TIMEOUT(密码错) */
        /* 主动断开不弹失败提示 */
        if (user_initiated_disconnect) {
            /*失败提示标记位置false*/
            user_initiated_disconnect = false; 
            wifi_connect_failed_flag = false;
        } else {
            wifi_connect_failed_flag = true;
        }
        wifi_connect_success_flag = false;
    }
    else if (event_base == IP_EVENT && event_id == IP_EVENT_STA_GOT_IP) {
        ip_event_got_ip_t *event = (ip_event_got_ip_t *)event_data;
        wifi_connect_success_flag = true;
        wifi_connect_failed_flag = false;
        wifi_ap_record_t ap_info;
        if (esp_wifi_sta_get_ap_info(&ap_info) == ESP_OK) {
            strncpy(pending_connected_ssid, (char *)ap_info.ssid, 32);
            pending_connected_ssid[32] = '\0';
        }
        // WiFi连接成功后调用,启用时间同步
        time_sync_init();
    }
}

/* ========== 主动断开WiFi ========== */
static void wifi_disconnect_real(void) {
    esp_err_t err = esp_wifi_disconnect();
    if (err != ESP_OK) {
        ESP_LOGW(WIFI_TAG, "disconnect failed: %s", esp_err_to_name(err));
    } else {
        ESP_LOGI(WIFI_TAG, "disconnect OK");
    }
    /* 清空本地状态，等事件回调确认后再重建列表 */
    saved_connected_ssid[0] = '\0';
    pending_connected_ssid[0] = '\0';
    wifi_clear_credentials();   // 主动断开时清除保存的凭证
}

/* ========== WiFi连接 ========== */
static void wifi_connect_real(const char *ssid, const char *password) {
    wifi_config_t wifi_config;
    /*整个清零，避免未初始化字段干扰*/
    memset(&wifi_config, 0, sizeof(wifi_config));  

    //结构体赋值
    strncpy((char *)wifi_config.sta.ssid, ssid, sizeof(wifi_config.sta.ssid) - 1);
    strncpy((char *)wifi_config.sta.password, password, sizeof(wifi_config.sta.password) - 1);
    /* 显式补 \0，防止极端情况 */
    wifi_config.sta.ssid[sizeof(wifi_config.sta.ssid) - 1] = '\0';
    wifi_config.sta.password[sizeof(wifi_config.sta.password) - 1] = '\0';

    /* 连接时扫描所有信道，不要FAST_SCAN（只扫部分信道） */
    wifi_config.sta.scan_method = WIFI_ALL_CHANNEL_SCAN;
    // /* 如果有多个同名AP，选信号最强的 */
    // wifi_config.sta.sort_method = WIFI_CONNECT_AP_BY_SIGNAL;
    /* 启用PMF兼容，关键！（现代路由器很多强制要求802.11w） */
    wifi_config.sta.pmf_cfg.capable = true;
    wifi_config.sta.pmf_cfg.required = false;
    /* 使用的是WPA2-Personal */
    wifi_config.sta.threshold.authmode = WIFI_AUTH_WPA2_PSK;

    esp_err_t err = esp_wifi_set_config(WIFI_IF_STA, &wifi_config);
    if (err != ESP_OK) {
        wifi_connect_failed_flag = true;
        return;
    }

    err = esp_wifi_connect();
    if (err != ESP_OK) {
        /* ESP_ERR_WIFI_CONN(0x3009) = 正在扫描中，稍后再试 */
        wifi_connect_failed_flag = true;
        return;
    }
}

/* ========== WiFi扫描任务 ========== */
void wifi_scan_task(void *pvParameters) {
    vTaskDelay(pdMS_TO_TICKS(3000)); // 等待系统WiFi初始化完成

    while (1) {
         /*安全保护：如果 init 还没执行，本次跳过*/ 
        /*	防后台扫描任务比 init 先执行。因为扫描任务优先级是 5，创建后哪怕有 3 秒延时，
        如果 wifi_icon_function_init() 因为 WiFi 事件注册阻塞、或者任务调度异常，扫描任务一旦醒来就 assert 崩溃。*/
        if (wifi_data_mutex == NULL) {
            vTaskDelay(pdMS_TO_TICKS(1000));
            continue;
        }

        wifi_scan_config_t scan_config = {
            .ssid = NULL,
            .bssid = NULL,
            .channel = 0,
            .show_hidden = false,
            .scan_type = WIFI_SCAN_TYPE_ACTIVE,
        };

        esp_err_t ret = esp_wifi_scan_start(&scan_config, true);
        if (ret == ESP_OK) {
            uint16_t number = WIFI_SCAN_LIST_SIZE;
            esp_wifi_scan_get_ap_records(&number, wifi_scan_records);

            xSemaphoreTake(wifi_data_mutex, portMAX_DELAY);
            wifi_scan_count = number;
            xSemaphoreGive(wifi_data_mutex);

            wifi_scan_updated_flag = true;
        }

        vTaskDelay(pdMS_TO_TICKS(10000)); // 每10秒扫描一次
    }
}

/* ========== UI刷新定时器回调（运行在LVGL主线程） ========== */
static void wifi_ui_refresh_timer_cb(lv_timer_t *timer) {
    bool need_rebuild = false;

    if (wifi_scan_updated_flag) {
        wifi_scan_updated_flag = false;
        need_rebuild = true;
    }

    if (wifi_connect_success_flag) {
        wifi_connect_success_flag = false;
        strncpy(saved_connected_ssid, pending_connected_ssid, 32);
        saved_connected_ssid[32] = '\0';
        need_rebuild = true;

        lv_obj_t *toast = lv_label_create(lv_scr_act());
        lv_label_set_text(toast, "连接成功");
        lv_obj_set_style_text_font(toast, &lv_font_chinese_16, 0);
        lv_obj_set_style_bg_color(toast, lv_color_hex(0x333333), 0);
        lv_obj_set_style_bg_opa(toast, LV_OPA_70, 0);
        lv_obj_set_style_text_color(toast, lv_color_white(), 0);
        lv_obj_set_style_pad_all(toast, 5, 0);
        lv_obj_center(toast);

        //延时删除toast界面
        lv_obj_del_delayed(toast, 2000);
    }

    if (wifi_connect_failed_flag && (!user_initiated_disconnect)) {
        wifi_connect_failed_flag = false;
        saved_connected_ssid[0] = '\0';
        need_rebuild = true;

        lv_obj_t *toast = lv_label_create(lv_scr_act());
        lv_label_set_text(toast, "连接失败");
        lv_obj_set_style_text_font(toast, &lv_font_chinese_16, 0);
        lv_obj_set_style_bg_color(toast, lv_color_hex(0xFF4444), 0);
        lv_obj_set_style_bg_opa(toast, LV_OPA_70, 0);
        lv_obj_set_style_text_color(toast, lv_color_white(), 0);
        lv_obj_set_style_pad_all(toast, 5, 0);
        lv_obj_center(toast);

        //延时删除toast界面
        lv_obj_del_delayed(toast, 2000);
    }

    if (need_rebuild) {
        rebuild_wifi_list();
    }
}

/* ========== 重建WIFI列表：文本分割，已连接置顶 ========== */
static void rebuild_wifi_list(void) {
    if (watch_function.wifi_list == NULL) return;

    lv_obj_clear_flag(watch_function.wifi_list, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_clean(watch_function.wifi_list);

    /* --- 已连接区域 --- */
    lv_obj_t *txt_conn = lv_list_add_text(watch_function.wifi_list, "已连接");
    lv_obj_set_style_text_font(txt_conn, &lv_font_chinese_16, 0);

    if (saved_connected_ssid[0] != '\0') {
        lv_obj_t *btn = lv_list_add_btn(watch_function.wifi_list, LV_SYMBOL_WIFI, saved_connected_ssid);
        lv_obj_t *label = wifi_btn_find_label(btn);
        if (label) lv_obj_set_style_text_font(label, &lv_font_chinese_16, 0);
        /* 给已连接项也注册点击事件 */
        lv_obj_add_event_cb(btn, wifi_btn_event_cb, LV_EVENT_CLICKED, NULL);
    } else {
        lv_obj_t *btn = lv_list_add_btn(watch_function.wifi_list, NULL, "未连接");
        lv_obj_t *label = wifi_btn_find_label(btn);
        if (label) lv_obj_set_style_text_font(label, &lv_font_chinese_16, 0);
        lv_obj_clear_flag(btn, LV_OBJ_FLAG_CLICKABLE);
    }

    /* --- 可用网络区域 --- */
    lv_obj_t *txt_avail = lv_list_add_text(watch_function.wifi_list, "可用网络");
    lv_obj_set_style_text_font(txt_avail, &lv_font_chinese_16, 0);

    xSemaphoreTake(wifi_data_mutex, portMAX_DELAY);
    uint16_t count = wifi_scan_count; 
    wifi_ap_record_t records[WIFI_SCAN_LIST_SIZE];
    memcpy(records, wifi_scan_records, sizeof(wifi_ap_record_t) * count);
    xSemaphoreGive(wifi_data_mutex);

    for (int i = 0; i < count; i++) {
        char *ssid = (char *)records[i].ssid;
        if (strlen(ssid) == 0) continue;
        /* 跳过已连接的SSID，避免在可用网络中重复显示 */
        if (saved_connected_ssid[0] != '\0' && strcmp(ssid, saved_connected_ssid) == 0) {
            continue;
        }
        add_wifi_item(watch_function.wifi_list, ssid);
    }
}

/* ========== 密码对话框及连接逻辑（修复：先复制密码，再删对话框） ========== */
static void wifi_ok_btn_event_cb(lv_event_t *e) {
    lv_obj_t *dlg = (lv_obj_t *)lv_event_get_user_data(e);
    lv_obj_t *mask = lv_obj_get_parent(dlg);

    /* 先找到输入框 */
    lv_obj_t *ta = NULL;
    uint32_t child_cnt = lv_obj_get_child_cnt(dlg);
    for (uint32_t i = 0; i < child_cnt; i++) {
        lv_obj_t *child = lv_obj_get_child(dlg, i);
        if (lv_obj_check_type(child, &lv_textarea_class)) {
            ta = child;
            break;
        }
    }
    /* 关键修复：先把密码复制到局部缓冲区，再删除对话框 */
    char password_buf[64] = {0};
    if (ta) {
        strncpy(password_buf, lv_textarea_get_text(ta), sizeof(password_buf) - 1);
    }

    lv_obj_del(mask);   // 现在可以安全删除对话框

    if (wifi_pending_ssid[0] != '\0' && password_buf[0] != '\0') {

        /* 点击可用网络 → 先断开旧的（如果有），再弹密码框连新的(必须在放这里，如果放在列表按钮点击事件wifi_btn_event_cb处，会出现误触导致断连) */
        if (saved_connected_ssid[0] != '\0') {
             wifi_disconnect_real();
        }

        /* 先清空旧连接状态，列表立即显示"未连接"，等真实连接成功后再更新 */
        saved_connected_ssid[0] = '\0';
        rebuild_wifi_list();

        lv_obj_t *toast = lv_label_create(lv_scr_act());
        lv_label_set_text(toast, "连接中...");
        lv_obj_set_style_text_font(toast, &lv_font_chinese_16, 0);
        lv_obj_center(toast);
        lv_obj_del_delayed(toast, 1500);

        wifi_connect_real(wifi_pending_ssid, password_buf);

        /* 立即保存凭证到 NVS（即使后续连接失败，下次也能重试） */
        wifi_save_credentials(wifi_pending_ssid, password_buf);
    }

    wifi_pending_ssid[0] = '\0';
}

/* ========== 断开确认对话框回调 ========== */
static void wifi_disconnect_ok_cb(lv_event_t *e) {
    user_initiated_disconnect = true;   //标记这是用户主动断开
    lv_obj_t *dlg = (lv_obj_t *)lv_event_get_user_data(e);
    lv_obj_t *mask = lv_obj_get_parent(dlg);
    lv_obj_del(mask);
    wifi_disconnect_real();
    rebuild_wifi_list();
}

static void wifi_disconnect_cancel_cb(lv_event_t *e) {
    lv_obj_t *dlg = (lv_obj_t *)lv_event_get_user_data(e);
    lv_obj_t *mask = lv_obj_get_parent(dlg);
    lv_obj_del(mask);
}

//密码框取消按钮回调 
static void wifi_cancel_btn_event_cb(lv_event_t *e) {
    lv_obj_t *dlg = (lv_obj_t *)lv_event_get_user_data(e);
    lv_obj_t *mask = lv_obj_get_parent(dlg);
    lv_obj_del(mask);   // 取消时也需删除遮罩
    wifi_pending_ssid[0] = '\0';
}

/* ========== 创建断开确认对话框 ========== */
static lv_obj_t *create_disconnect_dialog(const char *ssid) {
    lv_obj_t *mask = lv_obj_create(lv_scr_act());
    lv_obj_set_size(mask, LV_PCT(100), LV_PCT(100));
    lv_obj_set_style_bg_color(mask, lv_color_black(), 0);
    lv_obj_set_style_bg_opa(mask, LV_OPA_50, 0);
    lv_obj_clear_flag(mask, LV_OBJ_FLAG_SCROLLABLE);

    lv_obj_t *dlg = lv_obj_create(mask);
    lv_obj_set_size(dlg, 220, 100);
    lv_obj_center(dlg);
    lv_obj_set_style_bg_color(dlg, lv_color_white(), 0);
    lv_obj_set_style_radius(dlg, 10, 0);
    lv_obj_set_style_pad_all(dlg, 10, 0);
    lv_obj_clear_flag(dlg, LV_OBJ_FLAG_SCROLLABLE);

    lv_obj_t *title = lv_label_create(dlg);
    lv_label_set_text_fmt(title, "断开 %s?", ssid);
    lv_obj_set_style_text_font(title, &lv_font_chinese_16, 0);
    lv_obj_align(title, LV_ALIGN_TOP_MID, 0, 5);

    lv_obj_t *btn_ok = lv_btn_create(dlg);
    lv_obj_set_size(btn_ok, 80, 30);
    lv_obj_align(btn_ok, LV_ALIGN_BOTTOM_LEFT, 15, -5);
    lv_obj_t *label_ok = lv_label_create(btn_ok);
    lv_label_set_text(label_ok, "断开");
    lv_obj_set_style_text_font(label_ok, &lv_font_chinese_16, 0);
    lv_obj_center(label_ok);

    lv_obj_t *btn_cancel = lv_btn_create(dlg);
    lv_obj_set_size(btn_cancel, 80, 30);
    lv_obj_align(btn_cancel, LV_ALIGN_BOTTOM_RIGHT, -15, -5);
    lv_obj_t *label_cancel = lv_label_create(btn_cancel);
    lv_label_set_text(label_cancel, "取消");
    lv_obj_set_style_text_font(label_cancel, &lv_font_chinese_16, 0);
    lv_obj_center(label_cancel);

    lv_obj_add_event_cb(btn_ok, wifi_disconnect_ok_cb, LV_EVENT_CLICKED, dlg);
    lv_obj_add_event_cb(btn_cancel, wifi_disconnect_cancel_cb, LV_EVENT_CLICKED, dlg);

    return dlg;
}

/* ========== 创建自定义密码对话框 ========== */
static lv_obj_t *create_password_dialog(const char *ssid) {
    // 创建半透明遮罩（可选）
    lv_obj_t *mask = lv_obj_create(lv_scr_act());
    lv_obj_set_size(mask, LV_PCT(100), LV_PCT(100));
    lv_obj_set_style_bg_color(mask, lv_color_black(), 0);
    lv_obj_set_style_bg_opa(mask, LV_OPA_50, 0);
    lv_obj_clear_flag(mask, LV_OBJ_FLAG_SCROLLABLE);

    // 创建对话框主体
    lv_obj_t *dlg = lv_obj_create(mask);
    lv_obj_set_size(dlg, 200, 140);         //宽度200px，高度140px
    lv_obj_align(dlg, LV_ALIGN_TOP_MID, 0, 5); // 顶部居中，向下偏移 5px
    // lv_obj_center(dlg);
    lv_obj_set_style_bg_color(dlg, lv_color_white(), 0);
    lv_obj_set_style_radius(dlg, 10, 0);
    lv_obj_set_style_pad_all(dlg, 10, 0);
    lv_obj_clear_flag(dlg, LV_OBJ_FLAG_SCROLLABLE);
    

    // 标题：显示 SSID
    lv_obj_t *title = lv_label_create(dlg);
    lv_label_set_text_fmt(title, "连接: %s", ssid);
    lv_obj_set_style_text_font(title, &lv_font_chinese_16, 0);
    lv_obj_align(title, LV_ALIGN_TOP_MID, 0, 5);

    // 提示文字
    lv_obj_t *hint = lv_label_create(dlg);
    lv_label_set_text(hint, "请输入密码");
    lv_obj_set_style_text_font(hint, &lv_font_chinese_16, 0);
    lv_obj_align(hint, LV_ALIGN_TOP_MID, 0, 30);

    //密码输入框
    lv_obj_t *ta = lv_textarea_create(dlg);
    lv_textarea_set_one_line(ta, true);
    lv_textarea_set_password_mode(ta, true);
    lv_textarea_set_placeholder_text(ta, "密码");
    lv_obj_set_width(ta, 160);
    lv_obj_set_style_text_font(ta, &lv_font_chinese_16, 0);
    lv_obj_align(ta, LV_ALIGN_TOP_MID, 0, 55);

    //按钮行（用两个小按钮）
    lv_obj_t *btn_ok = lv_btn_create(dlg);
    lv_obj_set_size(btn_ok, 70, 30);
    lv_obj_align(btn_ok, LV_ALIGN_BOTTOM_LEFT, 20, 10);
    lv_obj_t *label_ok = lv_label_create(btn_ok);
    lv_label_set_text(label_ok, "确定");
    lv_obj_set_style_text_font(label_ok, &lv_font_chinese_16, 0);
    lv_obj_center(label_ok);

    lv_obj_t *btn_cancel = lv_btn_create(dlg);
    lv_obj_set_size(btn_cancel, 70, 30);
    lv_obj_align(btn_cancel, LV_ALIGN_BOTTOM_RIGHT, -20, 10);
    lv_obj_t *label_cancel = lv_label_create(btn_cancel);
    lv_label_set_text(label_cancel, "取消");
    lv_obj_set_style_text_font(label_cancel, &lv_font_chinese_16, 0);
    lv_obj_center(label_cancel);

    // 创建键盘，绑定到输入框，放在遮罩底部
    lv_obj_t *kb = lv_keyboard_create(mask);
    lv_obj_set_size(kb, 240, 150);          // 满宽，高度 150（适配 5 行按键）
    lv_obj_align(kb, LV_ALIGN_BOTTOM_MID, 0, 14);
    lv_keyboard_set_textarea(kb, ta);       // 关联输入框
    // 默认文本模式（含大小写/数字/符号切换），也可改成 LV_KEYBOARD_MODE_NUMBER 纯数字
    lv_keyboard_set_mode(kb, LV_KEYBOARD_MODE_TEXT_LOWER);

    // 绑定按钮事件，传递 dlg 作为 user_data（方便回调关闭对话框）
    lv_obj_add_event_cb(btn_ok, wifi_ok_btn_event_cb, LV_EVENT_CLICKED, dlg);
    lv_obj_add_event_cb(btn_cancel, wifi_cancel_btn_event_cb, LV_EVENT_CLICKED, dlg);

    return dlg;  // 返回对话框对象（实际上遮罩是父对象，但对话框是主对象）
}

/* WiFi 列表按钮点击事件 */
static void wifi_btn_event_cb(lv_event_t *e)
{
    /* 获取注册回调的按钮 */
    lv_obj_t *btn = lv_event_get_target(e);
    const char *clicked_ssid = lv_list_get_btn_text(watch_function.wifi_list, btn);

    if (!clicked_ssid || strlen(clicked_ssid) == 0) return;

    /* 点击已连接项 → 弹出断开确认框 */
    if (saved_connected_ssid[0] != '\0' && strcmp(clicked_ssid, saved_connected_ssid) == 0) {
        create_disconnect_dialog(clicked_ssid);
        return;
    }

    strncpy(wifi_pending_ssid, clicked_ssid, 32);
    wifi_pending_ssid[32] = '\0';

    create_password_dialog(clicked_ssid);
}

// 添加一个WiFi项到列表，并绑定点击事件
// 工具函数：创建按钮 + 绑定回调
static void add_wifi_item(lv_obj_t *list, const char *ssid) {
    lv_obj_t *btn = lv_list_add_btn(list, LV_SYMBOL_WIFI, ssid);
    lv_obj_add_event_cb(btn, wifi_btn_event_cb, LV_EVENT_CLICKED, NULL);
    lv_obj_t *label = wifi_btn_find_label(btn);
    if (label) lv_obj_set_style_text_font(label, &lv_font_chinese_16, 0);
}

void app_bluetooth_window(lv_obj_t *parent)
{
    /* 安全保护：如果 init 还没执行，先创建 mutex */
    /*防 GUI 初始化时 wifi_icon_function_init() 还没被调用。
    比如 main.c 里有人把 watch_gui_init() 写到了 wifi_icon_function_init() 前面，或者未来改代码调整了顺序*/
    if (wifi_data_mutex == NULL) {
        wifi_data_mutex = xSemaphoreCreateMutex();
    }
    // 防止重复创建：如果列表已存在，仅恢复连接状态显示
    if (watch_function.wifi_list != NULL) {
        rebuild_wifi_list();
        return;
    }

    /* 蓝牙/WIFI窗口页面,在内容区写 */
    watch_function.wifi_list = lv_list_create(parent);
    //铺满父对象全屏;100%，父对象是app_content[i]
    lv_obj_set_size(watch_function.wifi_list, LV_PCT(100), LV_PCT(100));
    // 配置滚动（你的WiFi列表必需）
    lv_obj_set_scrollbar_mode(watch_function.wifi_list, LV_SCROLLBAR_MODE_AUTO);    //显示滚动条
    lv_obj_set_scroll_dir(watch_function.wifi_list, LV_DIR_VER);    //上下滚动

    /* 尝试获取当前已连接AP的信息（若开机已连接则直接显示） */
    wifi_ap_record_t ap_info;
    if (esp_wifi_sta_get_ap_info(&ap_info) == ESP_OK) {
        strncpy(saved_connected_ssid, (char *)ap_info.ssid, 32);
        saved_connected_ssid[32] = '\0';
    }

    rebuild_wifi_list();
}

/* 外部传入扫描结果更新列表 */
void update_wifi_list(wifi_ap_record_t *ap_records, int count) {
    if (ap_records == NULL || count == 0) return;
    if (wifi_data_mutex == NULL) return;

    xSemaphoreTake(wifi_data_mutex, portMAX_DELAY);
    wifi_scan_count = (count > WIFI_SCAN_LIST_SIZE) ? WIFI_SCAN_LIST_SIZE : count;
    memcpy(wifi_scan_records, ap_records, sizeof(wifi_ap_record_t) * wifi_scan_count);
    xSemaphoreGive(wifi_data_mutex);

    wifi_scan_updated_flag = true;
}

/* WiFi STA 事件注册初始化（在main的WiFi初始化完成后调用一次） */
void wifi_icon_function_init(void) {
    if (wifi_data_mutex == NULL) {
        wifi_data_mutex = xSemaphoreCreateMutex();
    }

    if (wifi_ui_refresh_timer == NULL) {
        wifi_ui_refresh_timer = lv_timer_create(wifi_ui_refresh_timer_cb, 500, NULL);
    }

    ESP_ERROR_CHECK(esp_event_handler_register(WIFI_EVENT, ESP_EVENT_ANY_ID, &wifi_event_handler, NULL));
    ESP_ERROR_CHECK(esp_event_handler_register(IP_EVENT, IP_EVENT_STA_GOT_IP, &wifi_event_handler, NULL));

    /* 开机从 NVS 读取凭证并自动连接 */
    char saved_ssid[33] = {0};
    char saved_pass[65] = {0};
    if (wifi_load_credentials(saved_ssid, sizeof(saved_ssid), saved_pass, sizeof(saved_pass))) {
        //保存已连接WIFI的账号
        strncpy(saved_connected_ssid, saved_ssid, 32);
        saved_connected_ssid[32] = '\0';
        //账号、密码传入，开始连接WIFI
        wifi_connect_real(saved_ssid, saved_pass);
    }
}