#include "main/include/heart_iocn_function.h"
#include "esp_log.h"
#include "string.h"
#include "stdio.h"

#define HEART_TAG               "HEART"
#define UI_REFRESH_MS           150       /* LVGL定时器周期，保持流畅 */

static heart_app_state_t g_heart = {0};
static SemaphoreHandle_t g_heart_mutex = NULL;
static lv_obj_t *s_last_parent = NULL;   /* 记录上次绑定的父容器，防止跨窗口野指针 */

/*==================== 工具函数前向声明 ====================*/
static void heart_update_ui(void);
static void heart_ui_timer_cb(lv_timer_t *timer);
static void create_upper_half(lv_obj_t *parent);
static void create_lower_half(lv_obj_t *parent);

/*============================================================
 *                    本地建议文本
 *============================================================*/
static const char *suggest_normal =
    "心率正常，请继续保持健康的生活方式。";
static const char *suggest_low =
    "心率偏低，建议适当有氧运动，如散步、慢跑。如有不适请及时就医。";
static const char *suggest_high =
    "心率偏高，建议放松休息，避免咖啡因和剧烈运动。如持续偏高请就医。";
static const char *suggest_critical =
    "心率严重异常，请立即停止活动并寻求医疗帮助！";

/*============================================================
 *                      UI 构建
 *============================================================*/

static void create_upper_half(lv_obj_t *parent)
{
    /* 上半部分容器：固定占父容器高度的45%，确保左右百分比高度能正常计算 */
    g_heart.ui.cont_upper = lv_obj_create(parent);
    lv_obj_set_width(g_heart.ui.cont_upper, LV_PCT(100));
    lv_obj_set_height(g_heart.ui.cont_upper, LV_PCT(45));
    lv_obj_set_style_bg_opa(g_heart.ui.cont_upper, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(g_heart.ui.cont_upper, 0, 0);
    lv_obj_set_style_pad_all(g_heart.ui.cont_upper, 4, 0);
    lv_obj_set_flex_flow(g_heart.ui.cont_upper, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(g_heart.ui.cont_upper, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_clear_flag(g_heart.ui.cont_upper, LV_OBJ_FLAG_SCROLLABLE);

    /* 左侧：心率状态说明 */
    lv_obj_t *left_cont = lv_obj_create(g_heart.ui.cont_upper);
    lv_obj_set_flex_grow(left_cont, 1);
    lv_obj_set_height(left_cont, LV_PCT(100));
    lv_obj_set_style_bg_opa(left_cont, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(left_cont, 0, 0);
    lv_obj_set_style_pad_all(left_cont, 4, 0);
    lv_obj_set_flex_flow(left_cont, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(left_cont, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_clear_flag(left_cont, LV_OBJ_FLAG_SCROLLABLE);

    g_heart.ui.label_status_title = lv_label_create(left_cont);
    lv_label_set_text(g_heart.ui.label_status_title, "等待数据...");
    lv_obj_set_style_text_font(g_heart.ui.label_status_title, &lv_font_chinese_16, 0);
    lv_obj_set_style_text_color(g_heart.ui.label_status_title, lv_color_hex(0x999999), 0);

    g_heart.ui.label_status_detail = lv_label_create(left_cont);
    lv_label_set_text(g_heart.ui.label_status_detail, "");
    lv_obj_set_style_text_font(g_heart.ui.label_status_detail, &lv_font_chinese_16, 0);
    lv_obj_set_style_text_color(g_heart.ui.label_status_detail, lv_color_hex(0xBBBBBB), 0);

    /* 右侧：爱心图标 + 心率数值 */
    lv_obj_t *right_cont = lv_obj_create(g_heart.ui.cont_upper);
    lv_obj_set_flex_grow(right_cont, 1);
    lv_obj_set_height(right_cont, LV_PCT(100));
    lv_obj_set_style_bg_opa(right_cont, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(right_cont, 0, 0);
    lv_obj_set_style_pad_all(right_cont, 4, 0);
    lv_obj_set_flex_flow(right_cont, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(right_cont, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_clear_flag(right_cont, LV_OBJ_FLAG_SCROLLABLE);

    g_heart.ui.label_heart_icon = lv_label_create(right_cont);
    lv_label_set_text(g_heart.ui.label_heart_icon, FA_HEART);
    lv_obj_set_style_text_font(g_heart.ui.label_heart_icon, &lv_font_fontawesome_32, 0);
    lv_obj_set_style_text_color(g_heart.ui.label_heart_icon, lv_color_hex(0xEF5350), 0);

    g_heart.ui.label_heart_value = lv_label_create(right_cont);
    lv_label_set_text(g_heart.ui.label_heart_value, "--");
    lv_obj_set_style_text_font(g_heart.ui.label_heart_value, &lv_font_montserrat_48, 0);
    lv_obj_set_style_text_color(g_heart.ui.label_heart_value, lv_color_hex(0x333333), 0);

    g_heart.ui.label_heart_unit = lv_label_create(right_cont);
    lv_label_set_text(g_heart.ui.label_heart_unit, "BPM");
    lv_obj_set_style_text_font(g_heart.ui.label_heart_unit, &lv_font_chinese_16, 0);
    lv_obj_set_style_text_color(g_heart.ui.label_heart_unit, lv_color_hex(0x999999), 0);
}

static void create_lower_half(lv_obj_t *parent)
{
    /* 下半部分容器：自动填充剩余空间 */
    g_heart.ui.cont_lower = lv_obj_create(parent);
    lv_obj_set_width(g_heart.ui.cont_lower, LV_PCT(100));
    lv_obj_set_flex_grow(g_heart.ui.cont_lower, 1);
    lv_obj_set_style_bg_opa(g_heart.ui.cont_lower, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(g_heart.ui.cont_lower, 0, 0);
    lv_obj_set_style_pad_all(g_heart.ui.cont_lower, 8, 0);
    lv_obj_set_flex_flow(g_heart.ui.cont_lower, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(g_heart.ui.cont_lower, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START);
    lv_obj_clear_flag(g_heart.ui.cont_lower, LV_OBJ_FLAG_SCROLLABLE);

    g_heart.ui.label_suggestion_title = lv_label_create(g_heart.ui.cont_lower);
    lv_label_set_text(g_heart.ui.label_suggestion_title, "健康建议");
    lv_obj_set_style_text_font(g_heart.ui.label_suggestion_title, &lv_font_chinese_16, 0);
    lv_obj_set_style_text_color(g_heart.ui.label_suggestion_title, lv_color_hex(0x999999), 0);
    lv_obj_set_style_pad_bottom(g_heart.ui.label_suggestion_title, 4, 0);

    g_heart.ui.label_suggestion_text = lv_label_create(g_heart.ui.cont_lower);
    lv_label_set_text(g_heart.ui.label_suggestion_text, "等待心率数据...");
    lv_obj_set_style_text_font(g_heart.ui.label_suggestion_text, &lv_font_chinese_16, 0);
    lv_obj_set_style_text_color(g_heart.ui.label_suggestion_text, lv_color_hex(0x555555), 0);
    lv_obj_set_width(g_heart.ui.label_suggestion_text, LV_PCT(100));
    lv_label_set_long_mode(g_heart.ui.label_suggestion_text, LV_LABEL_LONG_WRAP);
}

/*============================================================
 *                      资源清理（退出APP时调用）
 *============================================================*/
void heart_icon_function_deinit(void)
{
    if (g_heart.ui_timer) {
        lv_timer_del(g_heart.ui_timer);
        g_heart.ui_timer = NULL;
    }
    /* 清空UI结构体，防止下次进入时访问已销毁的LVGL对象 */
    memset(&g_heart.ui, 0, sizeof(g_heart.ui));
    g_heart.ui.inited = false;
    s_last_parent = NULL;

    /* 修复：清零初始化标志，下次进入能重新init */
    g_heart.ui_update_interval_ms = 0;

    // ESP_LOGI(HEART_TAG, "Heart UI deinit done, free_heap=%lu", esp_get_free_heap_size());
}

/*============================================================
 *                      UI 更新
 *============================================================*/

static void heart_update_ui(void)
{
    if (!g_heart.ui.inited) return;

    heart_data_t local_data = {0};

    if (g_heart_mutex) xSemaphoreTake(g_heart_mutex, portMAX_DELAY);
    local_data = g_heart.data;
    if (g_heart_mutex) xSemaphoreGive(g_heart_mutex);

    if (!local_data.valid) {
        lv_label_set_text(g_heart.ui.label_heart_value, "--");
        lv_label_set_text(g_heart.ui.label_status_title, "等待数据...");
        lv_label_set_text(g_heart.ui.label_status_detail, "");
        lv_label_set_text(g_heart.ui.label_suggestion_text, "等待心率数据...");
        return;
    }

    char buf[16];
    snprintf(buf, sizeof(buf), "%.0f", local_data.value);
    lv_label_set_text(g_heart.ui.label_heart_value, buf);

    const char *title = "";
    const char *detail = "";
    const char *suggestion = "";

    switch (local_data.status) {
        case HEART_STATUS_NORMAL:
            title = "心率正常";
            detail = "您的静息心率在健康范围内";
            suggestion = suggest_normal;
            lv_obj_set_style_text_color(g_heart.ui.label_status_title, lv_color_hex(0x4CAF50), 0);
            break;
        case HEART_STATUS_LOW:
            title = "心率偏低";
            detail = "低于正常范围";
            suggestion = suggest_low;
            lv_obj_set_style_text_color(g_heart.ui.label_status_title, lv_color_hex(0xFF9800), 0);
            break;
        case HEART_STATUS_HIGH:
            title = "心率偏高";
            detail = "高于正常范围";
            suggestion = suggest_high;
            lv_obj_set_style_text_color(g_heart.ui.label_status_title, lv_color_hex(0xFF9800), 0);
            break;
        case HEART_STATUS_CRITICAL:
            title = "心率严重异常";
            detail = "请立即关注";
            suggestion = suggest_critical;
            lv_obj_set_style_text_color(g_heart.ui.label_status_title, lv_color_hex(0xF44336), 0);
            break;
        default:
            title = "未知状态";
            break;
    }

    lv_label_set_text(g_heart.ui.label_status_title, title);
    lv_label_set_text(g_heart.ui.label_status_detail, detail);
    lv_label_set_text(g_heart.ui.label_suggestion_text, suggestion);
}

static void heart_ui_timer_cb(lv_timer_t *timer)
{
    /* 修复：防止定时器回调访问已销毁的UI */
    if (!g_heart.ui.inited) return; 
    
    if (g_heart.data.updated) {
        heart_update_ui();
        
        if (g_heart_mutex) xSemaphoreTake(g_heart_mutex, portMAX_DELAY);
        g_heart.data.updated = false;
        if (g_heart_mutex) xSemaphoreGive(g_heart_mutex);
    }
}

/*============================================================
 *                      对外接口
 *============================================================*/

void app_heart_window(lv_obj_t *parent)
{
    /* 修复：如果父容器变了，必须重建UI */
    if (g_heart.ui.inited && parent != s_last_parent) {
        heart_icon_function_deinit();
    }

    if (g_heart.ui.inited && parent == s_last_parent) {
        heart_update_ui();
        return;
    }

    s_last_parent = parent;  /* 记录当前父容器 */

    lv_obj_set_layout(parent, LV_LAYOUT_FLEX);
    lv_obj_set_flex_flow(parent, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(parent, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START);
    lv_obj_set_style_pad_all(parent, 0, 0);
    lv_obj_set_style_border_width(parent, 0, 0);
    lv_obj_clear_flag(parent, LV_OBJ_FLAG_SCROLLABLE);

    create_upper_half(parent);
    create_lower_half(parent);

    if (!g_heart.ui_timer) {
        g_heart.ui_timer = lv_timer_create(heart_ui_timer_cb, UI_REFRESH_MS, NULL);
    }

    g_heart.ui.inited = true;
    heart_update_ui();
}

void heart_icon_function_init(void)
{
    if (g_heart.ui_timer != NULL) return;
    
    if (g_heart_mutex == NULL) {
        g_heart_mutex = xSemaphoreCreateMutex();
    }
    
    /* 心率数值刷新周期赋值给 g_heart.ui_update_interval_ms */
    g_heart.ui_update_interval_ms = HEART_UI_UPDATE_INTERVAL_MS;
}

void heart_data_change(float dat)
{
    if (g_heart_mutex) xSemaphoreTake(g_heart_mutex, portMAX_DELAY);

    g_heart.data.value = dat;
    g_heart.data.valid = true;

    if (dat < HEART_CRITICAL_MIN || dat > HEART_CRITICAL_MAX) {
        g_heart.data.status = HEART_STATUS_CRITICAL;
        g_heart.data.is_normal = false;
    } else if (dat < HEART_NORMAL_MIN) {
        g_heart.data.status = HEART_STATUS_LOW;
        g_heart.data.is_normal = false;
    } else if (dat > HEART_NORMAL_MAX) {
        g_heart.data.status = HEART_STATUS_HIGH;
        g_heart.data.is_normal = false;
    } else {
        g_heart.data.status = HEART_STATUS_NORMAL;
        g_heart.data.is_normal = true;
    }

    uint32_t now = lv_tick_get();
    if (now - g_heart.last_ui_update_tick > g_heart.ui_update_interval_ms) {
        g_heart.data.updated = true;
        g_heart.last_ui_update_tick = now;
    }

    if (g_heart_mutex) xSemaphoreGive(g_heart_mutex);
}