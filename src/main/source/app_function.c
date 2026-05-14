#include "main/include/app_function.h"

// 2. 功能回调数组（与icon_list一一对应）
app_func_cb_t app_func_array[APP_NUM] = {
    app_clock_func, app_bluetooth_func, app_weather_func,  app_heart_func,
};

static lv_obj_t *app_screens[APP_NUM] = {NULL};
//为每个APP页面定义“顶部栏”和“内容区”的全局数组（或结构体）
static lv_obj_t *app_top_bar[APP_NUM] = {NULL};   // 顶部按钮栏
static lv_obj_t *app_content[APP_NUM] = {NULL};    // 底部内容区

extern struct watch_window watch_window;
extern struct watch_function watch_function;

//记录当前打开的APP索引，-1 = 没有打开任何APP
static uint8_t current_app_index = -1;

void app_clock_func(void)
{
    //初始化窗口和组件
    app_timer_window(app_content[0]);
    /* 时钟功能初始化 */
    timer_icon_function_init();
}

void app_bluetooth_func(void)
{
    //初始化窗口和组件
    app_bluetooth_window(app_content[1]);
    /* WiFi功能初始化（事件注册、互斥锁等） */
    wifi_icon_function_init();
}

void app_weather_func(void)
{
    ESP_LOGI("APP", ">>> app_weather_func called <<<");  // 加这行
    //初始化窗口和组件
    app_weather_window(app_content[2]);
    /* 天气功能初始化 */
    weather_icon_function_init();

}
void app_heart_func(void)
{
    //初始化窗口和组件
    app_heart_window(app_content[3]);
    /* 心率功能初始化 */
    heart_icon_function_init();
}


// 退出按钮点击事件
static void app_exit_btn_event_cb(lv_event_t *e) {
    if (lv_event_get_code(e) == LV_EVENT_CLICKED) {
        app_exit();  // 调用统一退出函数
    }
}

// 为单个APP页面创建退出按钮（工具函数）
static void create_app_exit_btn(lv_obj_t *app_screen) {
    //创建退出按钮（圆形小按钮，适配手表屏）
    lv_obj_t *exit_btn = lv_btn_create(app_screen);
    lv_obj_set_size(exit_btn, 32, 32);  // 按钮尺寸（适配手表屏）
    lv_obj_align(exit_btn,LV_ALIGN_LEFT_MID,10,0);  //按钮位置放左上角
    lv_obj_add_style(exit_btn, &style_app_icon_circle, 0);  // 复用现有圆形样式

    //设置按钮背景（浅蓝色）
    lv_obj_set_style_bg_color(exit_btn, lv_color_hex(0x179FFF), 0);     

    //添加退出图标
    lv_obj_t *exit_label = lv_label_create(exit_btn);
    lv_label_set_text(exit_label, "<");  // Font Awesome叉号图标
    lv_obj_set_style_text_font(exit_label, &lv_font_montserrat_28, 0);       // 适配小按钮的字体大小
    lv_obj_set_style_text_color(exit_label, lv_color_white(), 0);
    lv_obj_set_style_text_align(exit_label,LV_ALIGN_CENTER,0);      //文本居中
    lv_obj_center(exit_label);

    //绑定退出事件
    lv_obj_add_event_cb(exit_btn, app_exit_btn_event_cb, LV_EVENT_ALL, NULL);

    //优化交互：点击时的缩放动画
    lv_obj_add_event_cb(exit_btn, app_icon_click_anim, LV_EVENT_ALL, NULL);
}

// 执行指定索引的APP功能
void execute_app_function(uint8_t index) {
    // 检查索引范围（index为int，判断有效）
    if (index >= APP_NUM) return;
    // 检查函数指针非空后执行
    if (app_func_array[index] != NULL) {
        app_func_array[index]();  // 执行对应功能
    }
}

//初始化各APP窗口内容（给每个app_screens[index]添加控件）
void app_functions_init(void) {

    app_clock_func();
    //WIFI初始化
    app_bluetooth_func();
    app_weather_func();
    app_heart_func();

}

//app统一退出路径
void app_exit(void)
{
    //保护
    // if (current_app_index < 0 || current_app_index >= APP_NUM) return;
    if (current_app_index >= APP_NUM) return;
    /* 退出心率APP时，通知ADC进入后台低频模式 */
    if (current_app_index == 3) {
        heart_adc_set_active(false);
        heart_icon_function_deinit();   /* 清理LVGL定时器 */
    }
    // 关闭目标APP页面
    lv_obj_add_flag(app_screens[current_app_index], LV_OBJ_FLAG_HIDDEN);
    // 打开主菜单窗口
    lv_obj_clear_flag(watch_window.scr_tile_page_main, LV_OBJ_FLAG_HIDDEN);
    //关闭APP，更新索引
    current_app_index = -1;
}

//原有页面切换函数
void load_app_page(int index) {
    //保护
    if (index < 0 || index >= APP_NUM) return;

    // 隐藏所有APP页面
    for (int i = 0; i < APP_NUM; i++) {
        if (app_screens[i]) lv_obj_add_flag(app_screens[i], LV_OBJ_FLAG_HIDDEN);
    }

    // 主菜单窗口
    if(watch_window.scr_tile_page_main) {
        lv_obj_add_flag(watch_window.scr_tile_page_main, LV_OBJ_FLAG_HIDDEN);
    }

    // 显示目标APP页面（父对象是屏幕根，不能被隐藏）
    if(app_screens[index]) {
        lv_obj_clear_flag(app_screens[index], LV_OBJ_FLAG_HIDDEN);
        // 确保APP页面在最上层
        lv_obj_move_foreground(app_screens[index]);
    }

    /* 打开心率APP时才启动高频ADC */
    if (index == 3) {
        heart_adc_set_active(true);
    }

     //更新当前打开的APP索引
    current_app_index = index;
}

// 原有APP窗口创建函数（保留，建议在初始化时调用）
void create_app_window(void) {
    for (int i = 0; i < APP_NUM; i++) 
    {
        app_screens[i] = lv_obj_create(watch_window.scr_window_main);
        lv_obj_set_size(app_screens[i], WATCH_SCREEN_W, WATCH_SCREEN_H);
        lv_obj_align(app_screens[i], LV_ALIGN_CENTER, 0, 0);
        lv_obj_set_style_bg_color(app_screens[i], lv_color_white(), 0);
        lv_obj_add_flag(app_screens[i], LV_OBJ_FLAG_HIDDEN);  // 默认隐藏

        // 给APP主窗口设置垂直Flex布局
        lv_obj_set_layout(app_screens[i], LV_LAYOUT_FLEX);
        lv_obj_set_flex_flow(app_screens[i], LV_FLEX_FLOW_COLUMN); // 垂直排列
        lv_obj_set_style_pad_all(app_screens[i], 0, 0);             // 去掉内边距

        // 创建顶部按钮栏（高度固定，专门放退出按钮）
        app_top_bar[i] = lv_obj_create(app_screens[i]);
        lv_obj_set_size(app_top_bar[i], LV_PCT(100), 36); // 宽度100%，高度36px（专门空出的一行）
        lv_obj_set_style_bg_opa(app_top_bar[i], LV_OPA_TRANSP, 0); // 背景透明（或设为浅灰）
        lv_obj_set_style_border_width(app_top_bar[i], 0, 0);
        lv_obj_set_style_pad_all(app_top_bar[i], 0, 0);
        create_app_exit_btn(app_top_bar[i]);    //创建退出键

        // 顶部栏内部用Flex布局，让按钮靠右
        // lv_obj_set_layout(app_top_bar[i], LV_LAYOUT_FLEX);
        // lv_obj_set_flex_flow(app_top_bar[i], LV_FLEX_FLOW_ROW);
        // lv_obj_set_flex_align(app_top_bar[i], LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER); // 左对齐
        // 内容区禁止滚动
        lv_obj_clear_flag(app_top_bar[i], LV_OBJ_FLAG_SCROLLABLE);
        lv_obj_set_scroll_dir(app_top_bar[i], LV_DIR_NONE);

        // 3. 创建底部内容区（自动填充剩余空间，放APP核心内容）
        app_content[i] = lv_obj_create(app_screens[i]);
        //在分配完所有固定尺寸的子项后，把剩余空间按 grow 比例分配给各个子项
        lv_obj_set_flex_grow(app_content[i], 1);   
        lv_obj_set_size(app_content[i], LV_PCT(100), LV_SIZE_CONTENT); // 宽度100%，高度自动填充
        lv_obj_set_style_bg_opa(app_content[i], LV_OPA_TRANSP, 0);
        lv_obj_set_style_border_width(app_content[i], 0, 0);
        lv_obj_set_style_pad_all(app_content[i], 0, 0);
        // ========== 修复点：仅对非WIFI页面禁用滚动 ==========
        if (i != 6) {  // 6是WIFI/蓝牙页面，保留滚动能力
            lv_obj_clear_flag(app_content[i], LV_OBJ_FLAG_SCROLLABLE);
            lv_obj_clear_flag(app_content[i], LV_OBJ_FLAG_SCROLL_MOMENTUM);
            lv_obj_clear_flag(app_content[i], LV_OBJ_FLAG_SCROLL_ELASTIC);
            lv_obj_clear_flag(app_content[i], LV_OBJ_FLAG_SCROLL_ON_FOCUS);
            lv_obj_set_scroll_dir(app_content[i], LV_DIR_NONE);
        } else {
            // WIFI页面：启用垂直滚动
            lv_obj_set_scrollbar_mode(app_content[i], LV_SCROLLBAR_MODE_AUTO);
            lv_obj_set_scroll_dir(app_content[i], LV_DIR_VER);
        }

        // 同时禁止父容器（app_screens[i]）的滚动（防止父容器可滚动）
        lv_obj_clear_flag(app_screens[i], LV_OBJ_FLAG_SCROLLABLE);
        lv_obj_clear_flag(app_screens[i], LV_OBJ_FLAG_SCROLL_MOMENTUM);
        lv_obj_clear_flag(app_screens[i], LV_OBJ_FLAG_SCROLL_ELASTIC);
        lv_obj_set_scrollbar_mode(app_screens[i], LV_SCROLLBAR_MODE_OFF);
        lv_obj_set_scroll_dir(app_screens[i], LV_DIR_NONE);
    }
    // 创建窗口后初始化各APP内容
    app_functions_init();
}


