#include "main/include/weather_iocn_function.h"
#include "esp_log.h"
#include "esp_http_client.h"
#include "cJSON.h"
#include "string.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#define WEATHER_TAG         "WEATHER"
#define API_URL_MAX         384
#define API_RESP_MAX        4096
#define SYNC_INTERVAL_S     3600    /* 1小时定时同步 */
#define UI_REFRESH_MS       1500    /* UI每1.5秒检查一次数据更新 */

/*======================= API 配置 =========================
 * 默认使用高德地图天气API（免费，需申请Key）。
 * 若使用阿里云云市场购买的第三方天气API，将
 * WEATHER_USE_ALIYUN_MARKET 设为 1 并填入 APPCODE。
 * ========================================================*/
#define WEATHER_USE_ALIYUN_MARKET   0

#if WEATHER_USE_ALIYUN_MARKET
    #define WEATHER_API_URL      "https://ali-weather.showapi.com/area-to-weather"
    #define WEATHER_API_APPCODE  "YOUR_APPCODE_HERE"
    #define WEATHER_API_PARAM    "?areaCode=%s&needMoreDay=1"
#else
    #define WEATHER_API_KEY      "04b875c07e6250168d0f1a496ee71fe2"
    // #define WEATHER_API_URL      "https://restapi.amap.com/v3/weather/weatherInfo"
    #define WEATHER_API_URL      "http://restapi.amap.com/v3/weather/weatherInfo"
    #define WEATHER_API_PARAM    "?city=%s&key=%s&extensions=all"
#endif

/*======================== 地区数据表 ========================
 * 高德天气API必须使用行政区划adcode查询。
 * 这里保留中文名称用于UI显示，adcode用于API请求。
 * =========================================================*/
typedef struct {
    const char *name;       /* 显示名称 */
    const char *adcode;     /* 高德adcode */
} region_item_t;

static const region_item_t province_tab[] = {
    {"广东省", "440000"}, {"浙江省", "330000"}, {"江苏省", "320000"},
    {"北京市", "110000"}, {"上海市", "310000"}
};
#define PROVINCE_CNT  (sizeof(province_tab)/sizeof(province_tab[0]))

static const region_item_t city_tab[][4] = {
    {{"广州市","440100"}, {"深圳市","440300"}, {"东莞市","441900"}, {"佛山市","440600"}},
    {{"杭州市","330100"}, {"宁波市","330200"}, {"温州市","330300"}},
    {{"南京市","320100"}, {"苏州市","320500"}, {"无锡市","320200"}},
    {{"北京市","110100"}},
    {{"上海市","310100"}}
};
static const uint8_t city_cnt[] = {4, 3, 3, 1, 1};

static const region_item_t district_tab[][5] = {
    {{"天河区","440106"}, {"越秀区","440104"}, {"海珠区","440105"}, {"白云区","440111"}, {"番禺区","440113"}},
    {{"南山区","440305"}, {"福田区","440304"}, {"罗湖区","440303"}, {"宝安区","440306"}},
    {{"南城街道","441900"}, {"莞城街道","441900"}, {"万江街道","441900"}},
    {{"禅城区","440604"}, {"南海区","440605"}, {"顺德区","440606"}},
    {{"上城区","330102"}, {"西湖区","330106"}, {"拱墅区","330105"}, {"滨江区","330108"}},
    {{"海曙区","330203"}, {"鄞州区","330212"}, {"江北区","330205"}},
    {{"鹿城区","330302"}, {"瓯海区","330304"}, {"龙湾区","330303"}},
    {{"玄武区","320102"}, {"秦淮区","320104"}, {"鼓楼区","320106"}, {"建邺区","320105"}},
    {{"姑苏区","320508"}, {"虎丘区","320505"}, {"吴中区","320506"}, {"相城区","320507"}},
    {{"梁溪区","320213"}, {"锡山区","320205"}, {"惠山区","320206"}},
    {{"东城区","110101"}, {"西城区","110102"}, {"朝阳区","110105"}, {"海淀区","110108"}, {"丰台区","110106"}},
    {{"黄浦区","310101"}, {"徐汇区","310104"}, {"长宁区","310105"}, {"静安区","310106"}, {"浦东新区","310115"}}
};
static const uint8_t district_cnt[] = {5, 4, 3, 3, 4, 3, 3, 4, 4, 3, 5, 5};
static const uint8_t city_off[] = {0, 4, 7, 10, 11};

/*======================== 全局状态 ========================*/
static weather_app_state_t g_weather = {0};

/*==================== 工具函数前向声明 ====================*/
static void weather_do_http_request(void);
static void weather_parse_json(const char *json, weather_data_t *out);
static void weather_update_ui(void);
static void rebuild_dropdown_city(void);
static void rebuild_dropdown_district(void);
static void dropdown_event_cb(lv_event_t *e);
static void weather_ui_timer_cb(lv_timer_t *timer);
static void weather_fetch_task(void *pv);
static void create_upper_half(lv_obj_t *parent);
static void create_lower_half(lv_obj_t *parent);
static void build_api_url(char *buf, size_t size);
static const char* get_current_city_adcode(void);
static void update_home_weather(void);
static const lv_img_dsc_t* weather_condition_to_icon(const char *cond);

/*============================================================
 *                      网络与数据层
 *============================================================*/

static const char* get_current_city_adcode(void)
{
    uint8_t p = g_weather.sel_province;
    uint8_t c = g_weather.sel_city;
    if (p >= PROVINCE_CNT) p = 0;
    if (c >= city_cnt[p]) c = 0;
    return city_tab[p][c].adcode;
}

static void build_api_url(char *buf, size_t size)
{
    const char *adcode = get_current_city_adcode();
#if WEATHER_USE_ALIYUN_MARKET
    snprintf(buf, size, WEATHER_API_URL WEATHER_API_PARAM, adcode);
#else
    snprintf(buf, size, WEATHER_API_URL WEATHER_API_PARAM, adcode, WEATHER_API_KEY);
#endif
}

static void weather_do_http_request(void)
{
    char url[API_URL_MAX];
    build_api_url(url, sizeof(url));
    ESP_LOGI(WEATHER_TAG, "HTTP GET: %s", url);

    esp_http_client_config_t cfg = {
        .url = url,
        .timeout_ms = 15000,
        .method = HTTP_METHOD_GET,
    };
    esp_http_client_handle_t client = esp_http_client_init(&cfg);
    if (!client) {
        ESP_LOGE(WEATHER_TAG, "HTTP client init failed");
        return;
    }

#if WEATHER_USE_ALIYUN_MARKET
    char auth_header[96];
    snprintf(auth_header, sizeof(auth_header), "APPCODE %s", WEATHER_API_APPCODE);
    esp_http_client_set_header(client, "Authorization", auth_header);
#endif

    esp_err_t err = esp_http_client_open(client, 0);
    if (err != ESP_OK) {
        ESP_LOGE(WEATHER_TAG, "HTTP open failed: %s", esp_err_to_name(err));
        esp_http_client_cleanup(client);
        return;
    }

    int64_t content_length = esp_http_client_fetch_headers(client);
    int status = esp_http_client_get_status_code(client);
    ESP_LOGI(WEATHER_TAG, "HTTP status=%d, len=%lld", status, content_length);

    bool parsed = false;
    if (status == 200 && content_length > 0 && content_length < API_RESP_MAX) {
        char *resp = malloc(content_length + 1);
        if (resp) {
            int read = esp_http_client_read_response(client, resp, content_length);
            if (read > 0) {
                resp[read] = '\0';
                ESP_LOGD(WEATHER_TAG, "Resp: %s", resp);
                weather_parse_json(resp, &g_weather.data);
                parsed = true;
            }
            free(resp);
        }
    } else if (status == 200 && content_length == -1) {
        char *resp = malloc(API_RESP_MAX);
        if (resp) {
            int total = 0, r = 0;
            while (total < API_RESP_MAX - 1 && 
                   (r = esp_http_client_read(client, resp + total, API_RESP_MAX - 1 - total)) > 0) {
                total += r;
            }
            if (total > 0) {
                resp[total] = '\0';
                weather_parse_json(resp, &g_weather.data);
                parsed = true;
            }
            free(resp);
        }
    } else {
        ESP_LOGW(WEATHER_TAG, "HTTP error or empty, status=%d", status);
    }

    esp_http_client_close(client);
    esp_http_client_cleanup(client);

    if (!parsed) {
        ESP_LOGW(WEATHER_TAG, "Parse failed or network error");
    }
}

static void weather_parse_json(const char *json, weather_data_t *out)
{
    if (!json || !out) return;
    cJSON *root = cJSON_Parse(json);
    if (!root) {
        ESP_LOGW(WEATHER_TAG, "JSON parse failed");
        return;
    }

    cJSON *status = cJSON_GetObjectItem(root, "status");
    if (!status || strcmp(status->valuestring, "1") != 0) {
        cJSON *info = cJSON_GetObjectItem(root, "info");
        ESP_LOGW(WEATHER_TAG, "API status invalid, info=%s", info ? info->valuestring : "null");
        cJSON_Delete(root);
        return;
    }

    cJSON *forecasts = cJSON_GetObjectItem(root, "forecasts");
    if (forecasts && cJSON_IsArray(forecasts) && cJSON_GetArraySize(forecasts) > 0) {
        cJSON *city_item = cJSON_GetArrayItem(forecasts, 0);
        cJSON *casts = cJSON_GetObjectItem(city_item, "casts");
        if (casts && cJSON_IsArray(casts)) {
            int n = cJSON_GetArraySize(casts);
            if (n > FORECAST_DAYS) n = FORECAST_DAYS;
            for (int i = 0; i < n; i++) {
                cJSON *cast = cJSON_GetArrayItem(casts, i);
                cJSON *date = cJSON_GetObjectItem(cast, "date");
                cJSON *dayweather = cJSON_GetObjectItem(cast, "dayweather");
                cJSON *nightweather = cJSON_GetObjectItem(cast, "nightweather");
                cJSON *daytemp = cJSON_GetObjectItem(cast, "daytemp");
                cJSON *nighttemp = cJSON_GetObjectItem(cast, "nighttemp");

                if (date) {
                    snprintf(out->forecast[i].date, sizeof(out->forecast[i].date), "%s", date->valuestring);
                }
                const char *cond = dayweather ? dayweather->valuestring : 
                                   (nightweather ? nightweather->valuestring : "未知");
                snprintf(out->forecast[i].condition, sizeof(out->forecast[i].condition), "%s", cond);

                int16_t hi = daytemp ? (int16_t)atoi(daytemp->valuestring) : 0;
                int16_t lo = nighttemp ? (int16_t)atoi(nighttemp->valuestring) : 0;
                out->forecast[i].temp_high = hi;
                out->forecast[i].temp_low = lo;
                out->forecast[i].temp_avg = (hi + lo) / 2;

                if (i == 0) {
                    out->current.temperature = out->forecast[i].temp_avg;
                    snprintf(out->current.condition, sizeof(out->current.condition), "%s", cond);
                }
            }
            out->valid = true;
            out->updated = true;

            /* 新增：同步刷新首页 */
            update_home_weather();

            ESP_LOGI(WEATHER_TAG, "Weather updated: %d°C %s", 
                     out->current.temperature, out->current.condition);
        } else {
            ESP_LOGW(WEATHER_TAG, "casts array missing");
        }
    } else {
        ESP_LOGW(WEATHER_TAG, "forecasts missing");
    }
    cJSON_Delete(root);
}

static void weather_fetch_task(void *pv)
{
    vTaskDelay(pdMS_TO_TICKS(5000));  /* 等待WiFi稳定 */

    uint32_t sync_counter = 0;
    while (1) {
        if (g_weather.need_sync || sync_counter >= SYNC_INTERVAL_S) {
            g_weather.need_sync = false;
            sync_counter = 0;
            weather_do_http_request();
        }
        vTaskDelay(pdMS_TO_TICKS(1000));
        sync_counter++;
    }
}

/*============================================================
 *                      UI 构建与事件
 *============================================================*/

static void rebuild_dropdown_city(void)
{
    if (!g_weather.ui.dd_city || !g_weather.ui.inited) return;
    char opts[256] = {0};
    size_t pos = 0;
    uint8_t p = g_weather.sel_province;
    if (p >= PROVINCE_CNT) p = 0;
    for (uint8_t i = 0; i < city_cnt[p] && pos < sizeof(opts) - 4; i++) {
        pos += snprintf(opts + pos, sizeof(opts) - pos, "%s\n", city_tab[p][i].name);
    }
    if (pos > 0 && opts[pos - 1] == '\n') opts[pos - 1] = '\0';
    lv_dropdown_set_options(g_weather.ui.dd_city, opts);
    lv_dropdown_set_selected(g_weather.ui.dd_city, 0);
    g_weather.sel_city = 0;
}

static void rebuild_dropdown_district(void)
{
    if (!g_weather.ui.dd_district || !g_weather.ui.inited) return;
    uint8_t p = g_weather.sel_province;
    uint8_t c = g_weather.sel_city;
    if (p >= PROVINCE_CNT) p = 0;
    if (c >= city_cnt[p]) c = 0;
    int off = city_off[p] + c;
    char opts[256] = {0};
    size_t pos = 0;
    uint8_t dcnt = district_cnt[off];
    for (uint8_t i = 0; i < dcnt && pos < sizeof(opts) - 4; i++) {
        pos += snprintf(opts + pos, sizeof(opts) - pos, "%s\n", district_tab[off][i].name);
    }
    if (pos > 0 && opts[pos - 1] == '\n') opts[pos - 1] = '\0';
    lv_dropdown_set_options(g_weather.ui.dd_district, opts);
    lv_dropdown_set_selected(g_weather.ui.dd_district, 0);
    g_weather.sel_district = 0;
}

static void dropdown_event_cb(lv_event_t *e)
{
    lv_obj_t *target = lv_event_get_target(e);
    if (target == g_weather.ui.dd_province) {
        g_weather.sel_province = lv_dropdown_get_selected(target);
        rebuild_dropdown_city();
        rebuild_dropdown_district();
        g_weather.need_sync = true;
        ESP_LOGI(WEATHER_TAG, "Province changed, trigger sync");
    } else if (target == g_weather.ui.dd_city) {
        g_weather.sel_city = lv_dropdown_get_selected(target);
        rebuild_dropdown_district();
        g_weather.need_sync = true;
        ESP_LOGI(WEATHER_TAG, "City changed, trigger sync");
    } else if (target == g_weather.ui.dd_district) {
        g_weather.sel_district = lv_dropdown_get_selected(target);
        g_weather.need_sync = true;
    }
}

/**
 * @brief 上半部分：地区选择 + 当前温度/天气
 * 高度由内容自适应（LV_SIZE_CONTENT），不挤占下半部分
 */
static void create_upper_half(lv_obj_t *parent)
{
    g_weather.ui.cont_upper = lv_obj_create(parent);
    lv_obj_set_width(g_weather.ui.cont_upper, LV_PCT(100));
    lv_obj_set_height(g_weather.ui.cont_upper, LV_SIZE_CONTENT);  /* 关键：高度由内容决定 */
    lv_obj_set_style_bg_opa(g_weather.ui.cont_upper, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(g_weather.ui.cont_upper, 0, 0);
    lv_obj_set_style_pad_all(g_weather.ui.cont_upper, 4, 0);
    lv_obj_set_flex_flow(g_weather.ui.cont_upper, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(g_weather.ui.cont_upper, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_clear_flag(g_weather.ui.cont_upper, LV_OBJ_FLAG_SCROLLABLE);

    /* 地区选择行：三个 lv_dropdown 横向紧凑排列 */
    lv_obj_t *dd_row = lv_obj_create(g_weather.ui.cont_upper);
    lv_obj_set_width(dd_row, LV_PCT(100));
    lv_obj_set_height(dd_row, LV_SIZE_CONTENT);
    lv_obj_set_style_bg_opa(dd_row, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(dd_row, 0, 0);
    lv_obj_set_style_pad_all(dd_row, 2, 0);
    lv_obj_set_flex_flow(dd_row, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(dd_row, LV_FLEX_ALIGN_SPACE_EVENLY, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_clear_flag(dd_row, LV_OBJ_FLAG_SCROLLABLE);

    /* 省下拉框 */
    g_weather.ui.dd_province = lv_dropdown_create(dd_row);
    lv_obj_set_size(g_weather.ui.dd_province, LV_PCT(30), 32);
    lv_obj_set_style_text_font(g_weather.ui.dd_province, &lv_font_chinese_16, 0);
    lv_obj_set_style_pad_all(g_weather.ui.dd_province, 2, 0);
    char opts[256] = {0};
    size_t pos = 0;
    for (uint8_t i = 0; i < PROVINCE_CNT && pos < sizeof(opts) - 4; i++)
        pos += snprintf(opts + pos, sizeof(opts) - pos, "%s\n", province_tab[i].name);
    if (pos > 0 && opts[pos - 1] == '\n') opts[pos - 1] = '\0';
    lv_dropdown_set_options(g_weather.ui.dd_province, opts);
    lv_obj_add_event_cb(g_weather.ui.dd_province, dropdown_event_cb, LV_EVENT_VALUE_CHANGED, NULL);

    /* 市下拉框 */
    g_weather.ui.dd_city = lv_dropdown_create(dd_row);
    lv_obj_set_size(g_weather.ui.dd_city, LV_PCT(30), 32);
    lv_obj_set_style_text_font(g_weather.ui.dd_city, &lv_font_chinese_16, 0);
    lv_obj_set_style_pad_all(g_weather.ui.dd_city, 2, 0);
    rebuild_dropdown_city();
    lv_obj_add_event_cb(g_weather.ui.dd_city, dropdown_event_cb, LV_EVENT_VALUE_CHANGED, NULL);

    /* 区下拉框 */
    g_weather.ui.dd_district = lv_dropdown_create(dd_row);
    lv_obj_set_size(g_weather.ui.dd_district, LV_PCT(30), 32);
    lv_obj_set_style_text_font(g_weather.ui.dd_district, &lv_font_chinese_16, 0);
    lv_obj_set_style_pad_all(g_weather.ui.dd_district, 2, 0);
    rebuild_dropdown_district();
    lv_obj_add_event_cb(g_weather.ui.dd_district, dropdown_event_cb, LV_EVENT_VALUE_CHANGED, NULL);

    /* 当前温度：26px 大字 */
    g_weather.ui.label_temp = lv_label_create(g_weather.ui.cont_upper);
    lv_label_set_text(g_weather.ui.label_temp, "--°C");
    lv_obj_set_style_text_font(g_weather.ui.label_temp, &lv_font_montserrat_26, 0);
    lv_obj_set_style_text_color(g_weather.ui.label_temp, lv_color_hex(0x333333), 0);
    lv_obj_set_style_pad_top(g_weather.ui.label_temp, 6, 0);

    /* 当前天气状况 */
    g_weather.ui.label_condition = lv_label_create(g_weather.ui.cont_upper);
    lv_label_set_text(g_weather.ui.label_condition, "加载中...");
    lv_obj_set_style_text_font(g_weather.ui.label_condition, &lv_font_chinese_16, 0);
    lv_obj_set_style_text_color(g_weather.ui.label_condition, lv_color_hex(0x666666), 0);
    lv_obj_set_style_pad_bottom(g_weather.ui.label_condition, 4, 0);
}

/**
 * @brief 下半部分：未来三天预报
 * 使用 flex_grow:1 填满父容器剩余全部空间，绝对不会被遮挡
 */
static void create_lower_half(lv_obj_t *parent)
{
    g_weather.ui.cont_lower = lv_obj_create(parent);
    lv_obj_set_width(g_weather.ui.cont_lower, LV_PCT(100));
    lv_obj_set_flex_grow(g_weather.ui.cont_lower, 1);   /* 占据剩余全部高度 */
    lv_obj_set_style_bg_opa(g_weather.ui.cont_lower, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(g_weather.ui.cont_lower, 0, 0);
    lv_obj_set_style_pad_all(g_weather.ui.cont_lower, 2, 0);
    lv_obj_set_flex_flow(g_weather.ui.cont_lower, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(g_weather.ui.cont_lower, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_clear_flag(g_weather.ui.cont_lower, LV_OBJ_FLAG_SCROLLABLE);

    /* 标题 */
    lv_obj_t *title = lv_label_create(g_weather.ui.cont_lower);
    lv_label_set_text(title, "未来三天预报");
    lv_obj_set_style_text_font(title, &lv_font_chinese_16, 0);
    lv_obj_set_style_text_color(title, lv_color_hex(0x999999), 0);
    lv_obj_set_style_pad_top(title, 2, 0);
    lv_obj_set_style_pad_bottom(title, 2, 0);

    /* 预报容器：Flex row，三等分 */
    g_weather.ui.cont_forecast = lv_obj_create(g_weather.ui.cont_lower);
    lv_obj_set_width(g_weather.ui.cont_forecast, LV_PCT(100));
    lv_obj_set_flex_grow(g_weather.ui.cont_forecast, 1);  /* 填满lower剩余空间 */
    lv_obj_set_style_bg_opa(g_weather.ui.cont_forecast, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(g_weather.ui.cont_forecast, 0, 0);
    lv_obj_set_style_pad_all(g_weather.ui.cont_forecast, 0, 0);
    lv_obj_set_flex_flow(g_weather.ui.cont_forecast, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(g_weather.ui.cont_forecast, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_clear_flag(g_weather.ui.cont_forecast, LV_OBJ_FLAG_SCROLLABLE);

    for (int i = 0; i < FORECAST_DAYS; i++) {
        /* 每天的卡片 */
        g_weather.ui.day_cont[i] = lv_obj_create(g_weather.ui.cont_forecast);
        lv_obj_set_flex_grow(g_weather.ui.day_cont[i], 1);
        lv_obj_set_height(g_weather.ui.day_cont[i], LV_PCT(100));
        lv_obj_set_style_bg_opa(g_weather.ui.day_cont[i], LV_OPA_TRANSP, 0);
        lv_obj_set_style_border_width(g_weather.ui.day_cont[i], 0, 0);
        lv_obj_set_style_pad_all(g_weather.ui.day_cont[i], 2, 0);
        lv_obj_set_flex_flow(g_weather.ui.day_cont[i], LV_FLEX_FLOW_COLUMN);
        lv_obj_set_flex_align(g_weather.ui.day_cont[i], LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
        lv_obj_clear_flag(g_weather.ui.day_cont[i], LV_OBJ_FLAG_SCROLLABLE);

        /* 日期 */
        g_weather.ui.label_date[i] = lv_label_create(g_weather.ui.day_cont[i]);
        lv_label_set_text(g_weather.ui.label_date[i], "--.--");
        lv_obj_set_style_text_font(g_weather.ui.label_date[i], &lv_font_chinese_16, 0);
        lv_obj_set_style_text_color(g_weather.ui.label_date[i], lv_color_hex(0x555555), 0);

        /* 天气 */
        g_weather.ui.label_fc_cond[i] = lv_label_create(g_weather.ui.day_cont[i]);
        lv_label_set_text(g_weather.ui.label_fc_cond[i], "--");
        lv_obj_set_style_text_font(g_weather.ui.label_fc_cond[i], &lv_font_chinese_16, 0);
        lv_obj_set_style_text_color(g_weather.ui.label_fc_cond[i], lv_color_hex(0x333333), 0);

        /* 平均气温 */
        g_weather.ui.label_temp_avg[i] = lv_label_create(g_weather.ui.day_cont[i]);
        lv_label_set_text(g_weather.ui.label_temp_avg[i], "--°C");
        lv_obj_set_style_text_font(g_weather.ui.label_temp_avg[i], &lv_font_montserrat_20, 0);
        lv_obj_set_style_text_color(g_weather.ui.label_temp_avg[i], lv_color_hex(0x333333), 0);

        /* 竖线分隔（1px宽灰色条） */
        if (i < FORECAST_DAYS - 1) {
            g_weather.ui.line_split[i] = lv_obj_create(g_weather.ui.cont_forecast);
            lv_obj_set_size(g_weather.ui.line_split[i], 1, LV_PCT(60));
            lv_obj_set_style_bg_color(g_weather.ui.line_split[i], lv_color_hex(0xCCCCCC), 0);
            lv_obj_set_style_bg_opa(g_weather.ui.line_split[i], LV_OPA_COVER, 0);
            lv_obj_set_style_border_width(g_weather.ui.line_split[i], 0, 0);
            lv_obj_set_style_pad_all(g_weather.ui.line_split[i], 0, 0);
        }
    }
}

static void weather_ui_timer_cb(lv_timer_t *timer)
{
    if (g_weather.data.updated) {
        weather_update_ui();
    }
}

static void weather_update_ui(void)
{
    if (!g_weather.ui.inited) return;

    if (!g_weather.data.valid) {
        if (g_weather.ui.label_temp)
            lv_label_set_text(g_weather.ui.label_temp, "--°C");
        if (g_weather.ui.label_condition)
            lv_label_set_text(g_weather.ui.label_condition, "加载中...");
        for (int i = 0; i < FORECAST_DAYS; i++) {
            if (g_weather.ui.label_date[i])
                lv_label_set_text(g_weather.ui.label_date[i], "--.--");
            if (g_weather.ui.label_fc_cond[i])
                lv_label_set_text(g_weather.ui.label_fc_cond[i], "--");
            if (g_weather.ui.label_temp_avg[i])
                lv_label_set_text(g_weather.ui.label_temp_avg[i], "--°C");
        }
        return;
    }

    char buf[32];
    snprintf(buf, sizeof(buf), "%d°C", g_weather.data.current.temperature);
    if (g_weather.ui.label_temp) lv_label_set_text(g_weather.ui.label_temp, buf);
    if (g_weather.ui.label_condition) lv_label_set_text(g_weather.ui.label_condition, g_weather.data.current.condition);

    for (int i = 0; i < FORECAST_DAYS; i++) {
        const char *d = g_weather.data.forecast[i].date;
        if (strlen(d) >= 10)
            lv_label_set_text(g_weather.ui.label_date[i], d + 5);  /* MM-DD */
        else
            lv_label_set_text(g_weather.ui.label_date[i], d);

        lv_label_set_text(g_weather.ui.label_fc_cond[i], g_weather.data.forecast[i].condition);

        snprintf(buf, sizeof(buf), "%d°C", g_weather.data.forecast[i].temp_avg);
        lv_label_set_text(g_weather.ui.label_temp_avg[i], buf);
    }

    g_weather.data.updated = false;
}

/* 天气条件 → 图标映射 */
static const lv_img_dsc_t* weather_condition_to_icon(const char *cond)
{
    if (!cond) return &cloudey;
    if (strstr(cond, "晴"))       return &sun;
    if (strstr(cond, "雷"))       return &thunder;
    if (strstr(cond, "雨"))       return &rain;
    if (strstr(cond, "云") || strstr(cond, "阴")) return &cloudey;
    return &cloudey;  /* 默认多云 */
}

/* 刷新首页天气（温度 + 图标） */
static void update_home_weather(void)
{
    if (!g_weather.data.valid) return;

    /* 温度 */
    if (label_img.label_weather_temp) {
        char buf[16];
        snprintf(buf, sizeof(buf), "%d°C", g_weather.data.current.temperature);
        lv_label_set_text(label_img.label_weather_temp, buf);
    }

    /* 图标 */
    if (label_img.img_weather) {
        const lv_img_dsc_t *icon = weather_condition_to_icon(g_weather.data.current.condition);
        lv_img_set_src(label_img.img_weather, icon);
    }
}

/*============================================================
 *                      对外接口
 *============================================================*/

void app_weather_window(lv_obj_t *parent)
{
    if (g_weather.ui.inited) {
        weather_trigger_sync();
        weather_update_ui();
        return;
    }

    /* 确保parent为Flex纵向布局 */
    lv_obj_set_layout(parent, LV_LAYOUT_FLEX);
    lv_obj_set_flex_flow(parent, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(parent, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START);
    lv_obj_set_style_pad_all(parent, 0, 0);
    lv_obj_set_style_border_width(parent, 0, 0);
    lv_obj_clear_flag(parent, LV_OBJ_FLAG_SCROLLABLE);

    create_upper_half(parent);   /* 高度自适应，放地区+当前天气 */
    create_lower_half(parent);   /* flex_grow填满剩余空间，放三天预报 */

    if (!g_weather.ui_timer) {
        g_weather.ui_timer = lv_timer_create(weather_ui_timer_cb, UI_REFRESH_MS, NULL);
    }

    g_weather.ui.inited = true;
    weather_trigger_sync();
}

void weather_icon_function_init(void)
{
    if (g_weather.net_inited) return;
    g_weather.net_inited = true;
    xTaskCreate(weather_fetch_task, "weather_fetch", 16384, NULL, 5, NULL);
    ESP_LOGI(WEATHER_TAG, "Weather task started");
}

void weather_trigger_sync(void)
{
    g_weather.need_sync = true;
    ESP_LOGI(WEATHER_TAG, "Sync triggered (app open or city changed)");
}
