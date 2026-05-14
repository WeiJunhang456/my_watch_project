#include "main/include/timer_iocn_function.h"

// static const char *TAG = "TIMER_APP";

// ========== 全局状态实例（唯一变量定义）==========
static timer_app_state_t g_timer = {0};

// ========== 星期选项字符串 ==========
static const char *weekday_options_str = 
    "仅一次\n"
    "每天\n"
    "周一至周五\n"
    "周末";

// 文件顶部定义静态常量字符串
static const char *minute_options = 
    "00\n01\n02\n03\n04\n05\n06\n07\n08\n09\n"
    "10\n11\n12\n13\n14\n15\n16\n17\n18\n19\n"
    "20\n21\n22\n23\n24\n25\n26\n27\n28\n29\n"
    "30\n31\n32\n33\n34\n35\n36\n37\n38\n39\n"
    "40\n41\n42\n43\n44\n45\n46\n47\n48\n49\n"
    "50\n51\n52\n53\n54\n55\n56\n57\n58\n59";

// ========== 内部函数声明 ==========
static void alarm_save_to_nvs(void);
static void alarm_load_from_nvs(void);
static void alarm_show_notification(const char *time_str);
static void alarm_notify_close_cb(lv_timer_t *timer);
static void alarm_check_cb(lv_timer_t *timer);
static void alarm_switch_event_cb(lv_event_t *e);
static void alarm_refresh_list(void);
static void alarm_setup_save_cb(lv_event_t *e);
static void alarm_setup_cancel_cb(lv_event_t *e);
static void alarm_add_btn_cb(lv_event_t *e);
static void alarm_delete_btn_cb(lv_event_t *e);

static void stopwatch_update_cb(lv_timer_t *timer);
static void stopwatch_btn_event_cb(lv_event_t *e);

static void create_stopwatch_tab(lv_obj_t *parent);
static void create_alarm_tab(lv_obj_t *parent);

// ========== NVS存储 ==========
static void alarm_save_to_nvs(void)
{
    nvs_handle_t nvs_handle;
    esp_err_t err = nvs_open(ALARM_NVS_NAMESPACE, NVS_READWRITE, &nvs_handle);
    if (err != ESP_OK) {
        // ESP_LOGE(TAG, "NVS打开失败");
        return;
    }
    
    err = nvs_set_blob(nvs_handle, ALARM_NVS_KEY, g_timer.alarms, sizeof(g_timer.alarms));
    if (err == ESP_OK) {
        nvs_commit(nvs_handle);
        // ESP_LOGI(TAG, "闹钟数据已保存");
    }
    nvs_close(nvs_handle);
}

static void alarm_load_from_nvs(void)
{
    nvs_handle_t nvs_handle;
    esp_err_t err = nvs_open(ALARM_NVS_NAMESPACE, NVS_READONLY, &nvs_handle);
    if (err != ESP_OK) {
        memset(g_timer.alarms, 0, sizeof(g_timer.alarms));
        return;
    }
    
    //读取g_timer.alarms的总字节数
    size_t size = sizeof(g_timer.alarms);
    err = nvs_get_blob(nvs_handle, ALARM_NVS_KEY, g_timer.alarms, &size);
    if (err == ESP_OK) {
        g_timer.alarm_count = 0;
        for (int i = 0; i < MAX_ALARMS; i++) {
            if (g_timer.alarms[i].valid) g_timer.alarm_count++;
        }
        // ESP_LOGI(TAG, "从NVS加载 %d 个闹钟", g_timer.alarm_count);
    } else {
        memset(g_timer.alarms, 0, sizeof(g_timer.alarms));
        g_timer.alarm_count = 0;  // 同步清零，防止 alarm_count 和实际数据不一致
    }
    nvs_close(nvs_handle);
}

// ========== 秒表功能 ==========
static void stopwatch_update_cb(lv_timer_t *timer)
{
    if (!g_timer.sw.running) return;
    
    uint32_t now = lv_tick_get();
    g_timer.sw.elapsed_ms += (now - g_timer.sw.start_tick);
    g_timer.sw.start_tick = now;
    
    uint32_t min = g_timer.sw.elapsed_ms / 60000;
    uint32_t sec = (g_timer.sw.elapsed_ms % 60000) / 1000;
    uint32_t ms = (g_timer.sw.elapsed_ms % 1000) / 10;
    
    lv_label_set_text_fmt(g_timer.sw.label_time, "%02d:%02d.%02d", min, sec, ms);
}

static void stopwatch_btn_event_cb(lv_event_t *e)
{
    lv_obj_t *btn = lv_event_get_target(e);
    
    if (btn == g_timer.sw.btn_start) {
        if (!g_timer.sw.running) {
            g_timer.sw.running = true;
            g_timer.sw.start_tick = lv_tick_get();
            lv_label_set_text(lv_obj_get_child(btn, 0), "暂停");
            if (!g_timer.sw.timer) {
                g_timer.sw.timer = lv_timer_create(stopwatch_update_cb, 50, NULL);
            }
        } else {
            g_timer.sw.running = false;
            uint32_t now = lv_tick_get();
            g_timer.sw.elapsed_ms += (now - g_timer.sw.start_tick);
            lv_label_set_text(lv_obj_get_child(btn, 0), "继续");
        }
    } else if (btn == g_timer.sw.btn_reset) {
        g_timer.sw.running = false;
        g_timer.sw.elapsed_ms = 0;
        if (g_timer.sw.timer) {
            lv_timer_del(g_timer.sw.timer);
            g_timer.sw.timer = NULL;
        }
        lv_label_set_text(g_timer.sw.label_time, "00:00.00");
        lv_label_set_text(lv_obj_get_child(g_timer.sw.btn_start, 0), "开始");
    }
}

static void create_stopwatch_tab(lv_obj_t *parent)
{
    g_timer.sw.label_time = lv_label_create(parent);
    lv_label_set_text(g_timer.sw.label_time, "00:00.00");
    lv_obj_set_style_text_font(g_timer.sw.label_time, &lv_font_montserrat_48, 0);
    lv_obj_set_style_text_color(g_timer.sw.label_time, lv_color_black(), 0);
    lv_obj_align(g_timer.sw.label_time, LV_ALIGN_TOP_MID, 0, 40);
    
    lv_obj_t *btn_cont = lv_obj_create(parent);
    lv_obj_set_size(btn_cont, 200, 60);
    lv_obj_align(btn_cont, LV_ALIGN_BOTTOM_MID, 0, -40);
    lv_obj_set_style_bg_opa(btn_cont, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(btn_cont, 0, 0);
    lv_obj_set_flex_flow(btn_cont, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(btn_cont, LV_FLEX_ALIGN_SPACE_EVENLY, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_clear_flag(btn_cont, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_clear_flag(btn_cont, LV_OBJ_FLAG_SCROLL_MOMENTUM);
    lv_obj_clear_flag(btn_cont, LV_OBJ_FLAG_SCROLL_ELASTIC);
    
    g_timer.sw.btn_start = lv_btn_create(btn_cont);
    lv_obj_set_size(g_timer.sw.btn_start, 80, 45);
    lv_obj_set_style_radius(g_timer.sw.btn_start, 22, 0);
    lv_obj_set_style_bg_color(g_timer.sw.btn_start, lv_color_hex(0x4CAF50), 0);
    lv_obj_t *label = lv_label_create(g_timer.sw.btn_start);
    lv_label_set_text(label, "开始");
    lv_obj_center(label);
    lv_obj_set_style_text_font(label,&lv_font_chinese_16,0);
    lv_obj_add_event_cb(g_timer.sw.btn_start, stopwatch_btn_event_cb, LV_EVENT_CLICKED, NULL);
    
    g_timer.sw.btn_reset = lv_btn_create(btn_cont);
    lv_obj_set_size(g_timer.sw.btn_reset, 80, 45);
    lv_obj_set_style_radius(g_timer.sw.btn_reset, 22, 0);
    lv_obj_set_style_bg_color(g_timer.sw.btn_reset, lv_color_hex(0xFF5722), 0);
    label = lv_label_create(g_timer.sw.btn_reset);
    lv_label_set_text(label, "复位");
    lv_obj_center(label);
    lv_obj_set_style_text_font(label,&lv_font_chinese_16,0);
    lv_obj_add_event_cb(g_timer.sw.btn_reset, stopwatch_btn_event_cb, LV_EVENT_CLICKED, NULL);
}

// ========== 闹钟通知功能 ==========
static void alarm_notify_close_cb(lv_timer_t *timer)
{  
    if (g_timer.alarm.notify_win) {
        lv_obj_del(g_timer.alarm.notify_win);
        g_timer.alarm.notify_win = NULL;
    }
    lv_timer_del(timer);
    g_timer.alarm.notify_timer = NULL;
}

static void alarm_show_notification(const char *time_str)
{   
    g_timer.alarm.notify_win = lv_obj_create(lv_scr_act());
    lv_obj_set_size(g_timer.alarm.notify_win, WATCH_SCREEN_W, WATCH_SCREEN_H);
    lv_obj_center(g_timer.alarm.notify_win);
    lv_obj_set_style_bg_color(g_timer.alarm.notify_win, lv_color_black(), 0);
    lv_obj_set_style_bg_opa(g_timer.alarm.notify_win, LV_OPA_70, 0);
    lv_obj_set_style_radius(g_timer.alarm.notify_win, 0, 0);
    lv_obj_set_style_border_width(g_timer.alarm.notify_win, 0, 0);
    
    lv_obj_t *cont = lv_obj_create(g_timer.alarm.notify_win);
    lv_obj_set_size(cont, 200, 140);
    lv_obj_center(cont);
    lv_obj_set_style_radius(cont, 15, 0);
    lv_obj_set_style_bg_color(cont, lv_color_white(), 0);
    
    lv_obj_t *icon = lv_label_create(cont);
    lv_label_set_text(icon, LV_SYMBOL_BELL);
    lv_obj_set_style_text_font(icon, &lv_font_montserrat_24, 0);
    lv_obj_set_style_text_color(icon, lv_color_hex(0xFF5722), 0);
    lv_obj_align(icon, LV_ALIGN_TOP_MID, 0, 10);
    
    lv_obj_t *label = lv_label_create(cont);
    lv_label_set_text_fmt(label, "闹钟时间到！\n%s", time_str);
    lv_obj_set_style_text_font(label, &lv_font_chinese_16, 0);
    lv_obj_set_style_text_align(label, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_set_style_text_color(label, lv_color_black(), 0);
    lv_obj_align(label, LV_ALIGN_CENTER, 0, 5);
       
    g_timer.alarm.notify_timer = lv_timer_create(alarm_notify_close_cb, 20000, NULL);
}

// ========== 闹钟检查 ==========
static void alarm_check_cb(lv_timer_t *timer)
{
    if (!time_is_synced()) return;
    
    time_t now;
    struct tm timeinfo;
    time(&now);
    localtime_r(&now, &timeinfo);
    
    uint8_t current_wday = timeinfo.tm_wday;
    uint8_t current_hour = timeinfo.tm_hour;
    uint8_t current_min = timeinfo.tm_min;
    uint8_t current_sec = timeinfo.tm_sec;
    
    if (current_sec != 0) return;
    
    for (int i = 0; i < MAX_ALARMS; i++) {
        if (!g_timer.alarms[i].valid || !g_timer.alarms[i].enabled) continue;
        
        bool weekday_match = false;
        if (g_timer.alarms[i].weekday_mask == 0) {
            weekday_match = true;
        } else {
            weekday_match = (g_timer.alarms[i].weekday_mask & (1 << current_wday)) != 0;
        }
        
        if (weekday_match && g_timer.alarms[i].hour == current_hour && g_timer.alarms[i].minute == current_min) {
            char time_str[16];
            snprintf(time_str, sizeof(time_str), "%02d:%02d", current_hour, current_min);
            alarm_show_notification(time_str);
            
            if (g_timer.alarms[i].weekday_mask == 0) {
                g_timer.alarms[i].enabled = false;
                alarm_save_to_nvs();
            }
            break;
        }
    }
}

// ========== 闹钟标签删除 ==========
static void alarm_delete_btn_cb(lv_event_t *e)
{
    lv_obj_t *btn = lv_event_get_target(e);
    int idx = (int)(intptr_t)lv_obj_get_user_data(btn);
    
    if (idx >= 0 && idx < MAX_ALARMS && g_timer.alarms[idx].valid) {
        g_timer.alarms[idx].valid = false;
        g_timer.alarms[idx].enabled = false;
        if (g_timer.alarm_count > 0) g_timer.alarm_count--;
        
        alarm_save_to_nvs();
        alarm_refresh_list();
        // ESP_LOGI(TAG, "删除闹钟%d", idx);
    }
}


// ========== 闹钟列表管理 ==========
static void alarm_switch_event_cb(lv_event_t *e)
{
    lv_obj_t *sw = lv_event_get_target(e);
    int idx = (int)(intptr_t)lv_obj_get_user_data(sw);
    
    g_timer.alarms[idx].enabled = lv_obj_has_state(sw, LV_STATE_CHECKED);
    alarm_save_to_nvs();
    // ESP_LOGI(TAG, "闹钟%d %s", idx, g_timer.alarms[idx].enabled ? "开启" : "关闭");
}

static void alarm_refresh_list(void)
{
    // lv_obj_clean(g_timer.alarm.list_container);
    // 保留 add_btn，删除其他子对象
    // lv_obj_t *child;
    // while ((child = lv_obj_get_child(g_timer.alarm.list_container, 0)) != NULL) {
    //     if (child == g_timer.alarm.add_btn) break;  // 保留添加按钮
            // 碰到 add_btn 就跳出
    //     // add_btn 是第0个，直接跳出，旧item全留着
    //     lv_obj_del(child);
    // }

    // 从后往前遍历，删除所有非 add_btn 的子对象
    uint32_t child_cnt = lv_obj_get_child_cnt(g_timer.alarm.list_container);
    for (int32_t i = child_cnt - 1; i >= 0; i--) {
        lv_obj_t *child = lv_obj_get_child(g_timer.alarm.list_container, i);
        if (child == g_timer.alarm.add_btn) continue;  // 保留 add_btn
        lv_obj_del(child);
    }
    
    int y_pos = 0;
    for (int i = 0; i < MAX_ALARMS; i++) {
        if (!g_timer.alarms[i].valid) continue;
        
        lv_obj_t *item = lv_obj_create(g_timer.alarm.list_container);
        lv_obj_set_size(item, 195, 80);
        lv_obj_set_pos(item, 10, y_pos);
        lv_obj_set_style_bg_color(item, lv_color_hex(0x333333), 0);
        lv_obj_set_style_radius(item, 10, 0);
        lv_obj_clear_flag(item, LV_OBJ_FLAG_SCROLLABLE);
        
        lv_obj_t *label = lv_label_create(item);
        lv_label_set_text_fmt(label, "%02d:%02d", g_timer.alarms[i].hour, g_timer.alarms[i].minute);
        lv_obj_set_style_text_font(label, &lv_font_montserrat_24, 0);
        lv_obj_set_style_text_color(label, lv_color_white(), 0);
        lv_obj_align(label, LV_ALIGN_LEFT_MID, 10, 0);
        
        const char *week_str = "单次";
        if (g_timer.alarms[i].weekday_mask == 0x7F) week_str = "每天";
        else if (g_timer.alarms[i].weekday_mask == 0x3E) week_str = "工作日";
        else if (g_timer.alarms[i].weekday_mask == 0x41) week_str = "周末";
        
        lv_obj_t *wlabel = lv_label_create(item);
        lv_label_set_text(wlabel, week_str);
        lv_obj_set_style_text_font(wlabel, &lv_font_chinese_16, 0);
        lv_obj_set_style_text_color(wlabel, lv_color_hex(0xAAAAAA), 0);
        lv_obj_align_to(wlabel, label, LV_ALIGN_OUT_BOTTOM_LEFT, 0, 2);

        // ========== 新增：删除按钮 ==========
        lv_obj_t *btn_del = lv_btn_create(item);
        lv_obj_set_size(btn_del, 28, 28);
        lv_obj_set_style_radius(btn_del, LV_RADIUS_CIRCLE, 0);
        // lv_obj_set_style_bg_color(btn_del, lv_color_hex(0xFF4444), 0);  // 红色
        //lv_obj_set_style_bg_color(btn_del, lv_color_hex(0x1E1E1E), 0); 
        lv_obj_set_style_bg_opa(btn_del, LV_OPA_TRANSP, 0);   // 背景透明
        lv_obj_set_style_border_width(btn_del, 0, 0);
        lv_obj_set_style_pad_all(btn_del, 0, 0);
        lv_obj_align(btn_del, LV_ALIGN_RIGHT_MID, 10, 0);  // 放在开关左侧
        lv_obj_t *del_label = lv_label_create(btn_del);
        lv_label_set_text(del_label, "x");  // 乘号作为删除图标
        lv_obj_set_style_text_font(del_label, &lv_font_montserrat_20, 0);
        lv_obj_set_style_text_color(del_label, lv_color_white(), 0);
        lv_obj_center(del_label);

        lv_obj_clear_flag(del_label, LV_OBJ_FLAG_CLICKABLE);  // 新增：禁用标签点击

        //识别用户点击的是第几个闹钟
        lv_obj_set_user_data(btn_del, (void *)(intptr_t)i);
        //注册删除按钮回调
        lv_obj_add_event_cb(btn_del, alarm_delete_btn_cb, LV_EVENT_CLICKED, NULL);
        
        lv_obj_t *sw = lv_switch_create(item);
        lv_obj_align(sw, LV_ALIGN_RIGHT_MID, -25, 0);
        if (g_timer.alarms[i].enabled) {
            lv_obj_add_state(sw, LV_STATE_CHECKED);
        }
        lv_obj_set_user_data(sw, (void *)(intptr_t)i);
        lv_obj_add_event_cb(sw, alarm_switch_event_cb, LV_EVENT_VALUE_CHANGED, NULL);
        
        y_pos += 85; //item之间的间距
    }
    
    if (g_timer.alarm.add_btn) {
        // lv_obj_set_pos(g_timer.alarm.add_btn, 200, g_timer.alarm_count * 55 + 10);
        // 计算居中 x 坐标：假设屏幕宽度 240，按钮宽 40，则 x = (240 - 40) / 2 = 100
        // 但为了安全，使用 align 到父对象底部中央
        lv_obj_align(g_timer.alarm.add_btn, LV_ALIGN_BOTTOM_MID, 0, -10);
    }
}

// ========== 闹钟设置弹窗 ==========
static void alarm_setup_save_cb(lv_event_t *e)
{
    int idx = -1;
    for (int i = 0; i < MAX_ALARMS; i++) {
        if (!g_timer.alarms[i].valid) {
            idx = i;
            break;
        }
    }
    if (idx < 0) {
        // ESP_LOGW(TAG, "闹钟已满");
        lv_obj_del(g_timer.alarm.setup_win);
        g_timer.alarm.setup_win = NULL;
        return;
    }
    
    uint16_t hour = lv_roller_get_selected(g_timer.alarm.roller_hour);
    uint16_t minute = lv_roller_get_selected(g_timer.alarm.roller_minute);
    uint16_t wtype = lv_roller_get_selected(g_timer.alarm.roller_weekday);
    
    uint8_t wmask = 0;
    switch (wtype) {
        case 0: wmask = 0; break;
        case 1: wmask = 0x7F; break;
        case 2: wmask = 0x3E; break;
        case 3: wmask = 0x41; break;
        case 4: wmask = 0; break;   // 增加默认保护
    }
    
    g_timer.alarms[idx].hour = hour;
    g_timer.alarms[idx].minute = minute;
    g_timer.alarms[idx].weekday_mask = wmask;
    // g_timer.alarms[idx].enabled = lv_obj_has_state(g_timer.alarm.switch_enable, LV_STATE_CHECKED);
    g_timer.alarms[idx].enabled = true;
    g_timer.alarms[idx].valid = true;
    g_timer.alarm_count++;
    
    alarm_save_to_nvs();
    alarm_refresh_list();
    
    lv_obj_del(g_timer.alarm.setup_win);
    g_timer.alarm.setup_win = NULL;
    
    // ESP_LOGI(TAG, "新建闹钟 %02d:%02d", hour, minute);
}

static void alarm_setup_cancel_cb(lv_event_t *e)
{
    lv_obj_del(g_timer.alarm.setup_win);
    g_timer.alarm.setup_win = NULL;
}

static void alarm_add_btn_cb(lv_event_t *e)
{
    if (g_timer.alarm_count >= MAX_ALARMS) {
        // ESP_LOGW(TAG, "闹钟数量已达上限");
        return;
    }
    if (g_timer.alarm.setup_win) return;
    
    g_timer.alarm.setup_win = lv_obj_create(lv_scr_act());
    lv_obj_set_size(g_timer.alarm.setup_win, 220, 280);
    lv_obj_center(g_timer.alarm.setup_win);
    lv_obj_set_style_radius(g_timer.alarm.setup_win, 15, 0);
    lv_obj_set_style_bg_color(g_timer.alarm.setup_win, lv_color_hex(0x2C2C2C), 0);
    
    lv_obj_t *title = lv_label_create(g_timer.alarm.setup_win);
    lv_label_set_text(title, "新建闹钟");
    lv_obj_set_style_text_font(title, &lv_font_chinese_16, 0);
    lv_obj_set_style_text_color(title, lv_color_white(), 0);
    lv_obj_align(title, LV_ALIGN_TOP_MID, 0, 5);
    
    g_timer.alarm.roller_hour = lv_roller_create(g_timer.alarm.setup_win);
    lv_roller_set_options(g_timer.alarm.roller_hour,
        "00\n01\n02\n03\n04\n05\n06\n07\n08\n09\n"
        "10\n11\n12\n13\n14\n15\n16\n17\n18\n19\n"
        "20\n21\n22\n23", LV_ROLLER_MODE_NORMAL);
    lv_roller_set_visible_row_count(g_timer.alarm.roller_hour, 3);
    lv_obj_set_size(g_timer.alarm.roller_hour, 60, 100);
    lv_obj_align(g_timer.alarm.roller_hour, LV_ALIGN_TOP_LEFT, 20, 30);
    lv_roller_set_selected(g_timer.alarm.roller_hour, 7, LV_ANIM_OFF);
    
    g_timer.alarm.roller_minute = lv_roller_create(g_timer.alarm.setup_win);
    // char min_opts[256] = "";
    // for (int i = 0; i < 60; i++) {
    //     char buf[8];
    //     snprintf(buf, sizeof(buf), "%02d\n", i);
    //     strcat(min_opts, buf);
    // }
    lv_roller_set_options(g_timer.alarm.roller_minute, minute_options, LV_ROLLER_MODE_NORMAL);
    lv_roller_set_visible_row_count(g_timer.alarm.roller_minute, 3);
    lv_obj_set_size(g_timer.alarm.roller_minute, 60, 100);
    lv_obj_align(g_timer.alarm.roller_minute, LV_ALIGN_TOP_RIGHT, -20, 30);
    
    lv_obj_t *colon = lv_label_create(g_timer.alarm.setup_win);
    lv_label_set_text(colon, ":");
    lv_obj_set_style_text_font(colon, &lv_font_montserrat_24, 0);
    lv_obj_set_style_text_color(colon, lv_color_white(), 0);
    lv_obj_align(colon, LV_ALIGN_TOP_MID, 0, 70);
    
    g_timer.alarm.roller_weekday = lv_roller_create(g_timer.alarm.setup_win);
    lv_roller_set_options(g_timer.alarm.roller_weekday, g_timer.weekday_options, LV_ROLLER_MODE_NORMAL);
    lv_roller_set_visible_row_count(g_timer.alarm.roller_weekday, 3);
    lv_obj_set_style_text_font(g_timer.alarm.roller_weekday,&lv_font_chinese_16,0);
    lv_obj_set_size(g_timer.alarm.roller_weekday, 180, 80);
    lv_obj_align(g_timer.alarm.roller_weekday, LV_ALIGN_TOP_MID, 0, 140);
    
    lv_obj_t *sw_label = lv_label_create(g_timer.alarm.setup_win);
    lv_label_set_text(sw_label, "启用");
    lv_obj_set_style_text_font(sw_label,&lv_font_chinese_16,0);
    lv_obj_set_style_text_color(sw_label, lv_color_white(), 0);
    lv_obj_align(sw_label, LV_ALIGN_BOTTOM_LEFT, 20, -55);
    
    // g_timer.alarm.switch_enable = lv_switch_create(g_timer.alarm.setup_win);
    // lv_obj_align(g_timer.alarm.switch_enable, LV_ALIGN_BOTTOM_LEFT, 80, -50);
    // lv_obj_add_state(g_timer.alarm.switch_enable, LV_STATE_CHECKED);
    
    lv_obj_t *btn_save = lv_btn_create(g_timer.alarm.setup_win);
    lv_obj_set_size(btn_save, 65, 35);
    lv_obj_align(btn_save, LV_ALIGN_BOTTOM_LEFT, 10, 10);
    lv_obj_set_style_bg_color(btn_save, lv_color_hex(0x4CAF50), 0);
    lv_obj_t *bl = lv_label_create(btn_save);
    lv_label_set_text(bl, "保存");
    lv_obj_center(bl);
    lv_obj_set_style_text_font(bl,&lv_font_chinese_16,0);
    lv_obj_add_event_cb(btn_save, alarm_setup_save_cb, LV_EVENT_CLICKED, NULL);
    
    lv_obj_t *btn_cancel = lv_btn_create(g_timer.alarm.setup_win);
    lv_obj_set_size(btn_cancel, 65, 35);
    lv_obj_align(btn_cancel, LV_ALIGN_BOTTOM_RIGHT, -10, 10);
    lv_obj_set_style_bg_color(btn_cancel, lv_color_hex(0x757575), 0);
    bl = lv_label_create(btn_cancel);
    lv_label_set_text(bl, "取消");
    lv_obj_center(bl);
    lv_obj_set_style_text_font(bl,&lv_font_chinese_16,0);
    lv_obj_add_event_cb(btn_cancel, alarm_setup_cancel_cb, LV_EVENT_CLICKED, NULL);
}

// ========== 闹钟Tab创建 ==========
static void create_alarm_tab(lv_obj_t *parent)
{
    alarm_load_from_nvs();
    
    g_timer.alarm.list_container = lv_obj_create(parent);
    lv_obj_set_size(g_timer.alarm.list_container, LV_PCT(100), LV_PCT(100));
    lv_obj_align(g_timer.alarm.list_container, LV_ALIGN_TOP_MID, 0, 10);
    lv_obj_set_style_bg_opa(g_timer.alarm.list_container, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(g_timer.alarm.list_container, 0, 0);
    lv_obj_set_style_pad_all(g_timer.alarm.list_container, 0, 0);
    lv_obj_clear_flag(g_timer.alarm.list_container, LV_OBJ_FLAG_SCROLLABLE);
    
    g_timer.alarm.add_btn = lv_btn_create(g_timer.alarm.list_container);
    lv_obj_set_size(g_timer.alarm.add_btn, 40, 40);
    lv_obj_set_style_radius(g_timer.alarm.add_btn, LV_RADIUS_CIRCLE, 0);
    lv_obj_set_style_bg_color(g_timer.alarm.add_btn, lv_color_hex(0x2196F3), 0);
    lv_obj_t *plus = lv_label_create(g_timer.alarm.add_btn);
    lv_label_set_text(plus, "+");
    lv_obj_set_style_text_font(plus, &lv_font_montserrat_24, 0);
    lv_obj_set_style_text_color(plus, lv_color_black(), 0);
    lv_obj_center(plus);
    lv_obj_align(g_timer.alarm.add_btn, LV_ALIGN_BOTTOM_RIGHT, -10, -10);
    lv_obj_add_event_cb(g_timer.alarm.add_btn, alarm_add_btn_cb, LV_EVENT_CLICKED, NULL);
    
    alarm_refresh_list();
    lv_timer_create(alarm_check_cb, 1000, NULL);
}

// ========== 对外接口 ==========
/*创建闹钟APP窗口*/
void app_timer_window(lv_obj_t *parent)
{
    g_timer.weekday_options = weekday_options_str;
    
    g_timer.tabview = lv_tabview_create(parent, LV_DIR_TOP, 40);
    lv_obj_set_size(g_timer.tabview, LV_PCT(100), LV_PCT(100));
    lv_obj_center(g_timer.tabview);
    
    lv_obj_t *tab_btns = lv_tabview_get_tab_btns(g_timer.tabview);
    lv_obj_set_style_bg_color(tab_btns, lv_color_hex(0x1E1E1E), 0);
    lv_obj_set_style_text_color(tab_btns, lv_color_white(), 0);
    
    g_timer.tab_stopwatch = lv_tabview_add_tab(g_timer.tabview, "秒表");
    g_timer.tab_alarm = lv_tabview_add_tab(g_timer.tabview, "闹钟");
    lv_obj_t* btn_text = lv_tabview_get_tab_btns(g_timer.tabview);
    lv_obj_set_style_text_font(btn_text,&lv_font_chinese_16,0);
    lv_obj_set_style_text_color(btn_text, lv_color_hex(0xAAAAAA), 0);
    lv_obj_clear_flag(g_timer.tab_stopwatch, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_clear_flag(g_timer.tab_stopwatch, LV_OBJ_FLAG_SCROLL_MOMENTUM);
    lv_obj_clear_flag(g_timer.tab_stopwatch, LV_OBJ_FLAG_SCROLL_ELASTIC);
    //闹钟标签改成垂直滑动
    lv_obj_set_scroll_dir(g_timer.tab_alarm, LV_DIR_VER);
     
    lv_obj_t *content = lv_tabview_get_content(g_timer.tabview);
    lv_obj_set_style_bg_color(content, lv_color_hex(0x121212), 0);
}

/*添加秒表功能/闹钟功能*/
void timer_icon_function_init(void)
{
    create_stopwatch_tab(g_timer.tab_stopwatch);
    create_alarm_tab(g_timer.tab_alarm);
}