#include "main/include/gui_app.h"

// 全局变量：圆点指示器(APP页面)
lv_obj_t *dot[2];

// 全局格子数组：2页 × 9格
static lv_obj_t * g_cell_objs[TAB_TOTAL_PAGE][CELL_COUNT] = {NULL}; // 存储9个格子对象

//声明结构体
struct watch_window watch_window;
struct watch_function watch_function;
struct watch_battery_component watch_battery_component;
struct label_img label_img;

lv_style_t style_app_icon_circle;


//页面用
// ====================== 创建3×3网格 ======================
static void create_3x3_grid(lv_obj_t *parent,uint8_t page_idx)
{
    lv_obj_set_layout(parent, LV_LAYOUT_GRID);  // 启用网格布局

    // 3列3行 自适应布局
    static lv_coord_t col_dsc[] = {LV_GRID_FR(1), LV_GRID_FR(1), LV_GRID_FR(1), LV_GRID_TEMPLATE_LAST};
    static lv_coord_t row_dsc[] = {LV_GRID_FR(1), LV_GRID_FR(1), LV_GRID_FR(1), LV_GRID_TEMPLATE_LAST};
    //描述父对象上的网格大小
    lv_obj_set_grid_dsc_array(parent, col_dsc, row_dsc);

        // 循环创建9个“格子容器”，并存入全局数组
        for(int i = 0; i < 9; i++)
        {
            int col = i % 3;
            int row = i / 3;

            // 创建格子对象
            g_cell_objs[page_idx][i] = lv_obj_create(parent);
            // 设置格子位置
            lv_obj_set_grid_cell(g_cell_objs[page_idx][i], LV_GRID_ALIGN_STRETCH , col, 1, LV_GRID_ALIGN_STRETCH , row, 1);
            // 让对象完全透明、无背景、无边框
            lv_obj_set_style_bg_opa(g_cell_objs[page_idx][i], LV_OPA_TRANSP, 0);  // 背景透明度=0
            lv_obj_set_style_border_width(g_cell_objs[page_idx][i], 0, 0);        // 边框宽度=0
            // 禁止格子容器本身有滚动条
            lv_obj_clear_flag(g_cell_objs[page_idx][i], LV_OBJ_FLAG_SCROLLABLE);
            lv_obj_clear_flag(g_cell_objs[page_idx][i], LV_OBJ_FLAG_SCROLL_MOMENTUM);
            lv_obj_clear_flag(g_cell_objs[page_idx][i], LV_OBJ_FLAG_SCROLL_ELASTIC);
        }
}

// 定时器回调函数
static void hide_led_timer_cb(lv_timer_t * timer)
{
    // 隐藏两个 LED，直接使用全局变量dot数组
    lv_obj_add_flag(dot[0], LV_OBJ_FLAG_HIDDEN);
    lv_obj_add_flag(dot[1], LV_OBJ_FLAG_HIDDEN);

    // 删除一次性定时器，避免重复触发
    lv_timer_del(timer);
}

// ====================== 圆点隐藏 ======================
void led_show_and_auto_hide(void)
{
    // 确保 LED 可见（万一之前被隐藏了）
    lv_obj_clear_flag(dot[0], LV_OBJ_FLAG_HIDDEN);
    lv_obj_clear_flag(dot[1], LV_OBJ_FLAG_HIDDEN);

    // 创建一次性定时器，3000ms 后调用 hide_led_timer_cb
    lv_timer_create(hide_led_timer_cb, 3000, NULL);// 无需传递 user_data
}

// ====================== 底部圆点 ======================
static void create_page_indicator(lv_obj_t *parent)
{
    dot[0] = lv_led_create(parent);
    dot[1] = lv_led_create(parent);
    lv_obj_set_size(dot[0],5,5);
    lv_obj_set_size(dot[1],5,5);
    lv_obj_align(dot[0], LV_ALIGN_CENTER, -5, 116);
    lv_obj_align(dot[1], LV_ALIGN_CENTER, 5, 116);
    lv_led_set_color(dot[0],lv_color_white());
    lv_led_set_color(dot[1],lv_color_white());
    // 彻底不让 LED 接收触摸
    lv_obj_clear_flag(dot[0], LV_OBJ_FLAG_CLICKABLE);
    lv_obj_clear_flag(dot[1], LV_OBJ_FLAG_CLICKABLE);

    lv_obj_set_style_bg_color(dot[0], lv_color_white(), 0);
    lv_obj_set_style_bg_color(dot[1], lv_color_white(), 0);

    lv_led_on(dot[0]);
    lv_led_off(dot[1]);
}

// ====================== 切换圆点 ======================
void tileview_event_cb(lv_event_t *e)
{
    lv_obj_t *tv = lv_event_get_target(e);
    lv_obj_t * pos = lv_tileview_get_tile_act(tv);  // 获取当前页面对象

    // 2. 获取瓦片在tileview网格中的行列坐标
    if (pos == watch_window.scr_tile_page1)
    {
        lv_led_set_brightness(dot[0], 255);
        lv_led_set_brightness(dot[1], 0);
    }
    else if(pos == watch_window.scr_tile_page2)
    {
        lv_led_set_brightness(dot[0], 0);
        lv_led_set_brightness(dot[1], 255);
    }

    //调用亮 3 秒后隐藏的功能
    led_show_and_auto_hide();

    /* lv_obj_get_style_grid_cell_column_pos 只能读取通过 lv_obj_set_grid_cell 显式设置了 grid 位置的子对象的列坐标。
        tileview 内部的 tile 排列是 tileview 自己管理的，并不会给每个 tile 设置 grid cell column pos 样式，所以返回值永远是 0（默认值）。
        因此，条件 (col == 1) 永不为真，dot[1] 永远不可能变亮。*/
}

// ====================== 顶部时间显示======================
void create_top_time_label(lv_obj_t *parent)
{
    watch_time.label_top_time = lv_label_create(parent);
    lv_obj_set_style_text_font(watch_time.label_top_time, &lv_font_montserrat_14, 0);
    lv_obj_set_style_text_color(watch_time.label_top_time, lv_color_black(), 0);
    lv_obj_align(watch_time.label_top_time, LV_ALIGN_TOP_MID, 0, 12);  // 顶部居中，下移12px
    
    // 初始显示"--:--"
    lv_label_set_text(watch_time.label_top_time, "--:--");
}

// ====================== 主菜单显示 ======================
void home_page_event_cb(lv_event_t *e)
{
    lv_obj_t *home = lv_event_get_target(e);
    if(lv_event_get_code(e) == LV_EVENT_PRESSED)
    {
        lv_obj_add_flag(home, LV_OBJ_FLAG_HIDDEN);                // 隐藏主页
        // 显示主菜单
        lv_obj_clear_flag(watch_window.scr_window_main, LV_OBJ_FLAG_HIDDEN);
    }


//    // 创建一次性定时器，30s 后调用 hide_led_timer_cb
//    lv_timer_create(hide_led_timer_cb, 3000, NULL);// 无需传递 user_data
}

// ====================== 电池电量显示 ======================
static void create_battery_icon(lv_obj_t *parent, lv_coord_t x, lv_coord_t y)
{
    // 创建容器（用来统一管理进度条和图片）
    watch_battery_component.battery_container = lv_obj_create(parent);
    lv_obj_set_size(watch_battery_component.battery_container, 56, 20); // 和电池图片尺寸一致
    lv_obj_center(watch_battery_component.battery_container);
    lv_obj_set_pos(watch_battery_component.battery_container, x, y);
    lv_obj_set_style_bg_opa(watch_battery_component.battery_container, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(watch_battery_component.battery_container, 0, 0);
    lv_obj_set_style_pad_all(watch_battery_component.battery_container, 0, 0);
    //消除边框滑动条
    lv_obj_clear_flag(watch_battery_component.battery_container, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_clear_flag(watch_battery_component.battery_container, LV_OBJ_FLAG_SCROLL_MOMENTUM);
    lv_obj_clear_flag(watch_battery_component.battery_container, LV_OBJ_FLAG_SCROLL_ELASTIC);

    //创建电量进度条（底层）
    watch_battery_component.battery_bar = lv_bar_create(watch_battery_component.battery_container);
    lv_obj_set_size(watch_battery_component.battery_bar, 16, 10); // 电池内部填充区域的大小（比外壳小一点）
    lv_obj_align(watch_battery_component.battery_bar, LV_ALIGN_CENTER, -2, 0);
    lv_obj_set_style_bg_color(watch_battery_component.battery_bar,lv_color_hex(0x00FF88),LV_PART_INDICATOR);
    lv_obj_set_style_radius(watch_battery_component.battery_bar, 0, 0);                    // 进度条主体方形
    lv_obj_set_style_radius(watch_battery_component.battery_bar, 0, LV_PART_INDICATOR);   // 填充部分方形

    // 去掉进度条的所有默认样式，只保留填充部分
    lv_obj_set_style_bg_opa(watch_battery_component.battery_bar, LV_OPA_TRANSP, 0); // 背景透明
    lv_obj_set_style_border_width(watch_battery_component.battery_bar, 0, 0);       // 去掉边框

    // 设置进度条范围0-100
    lv_bar_set_range(watch_battery_component.battery_bar, 0, 100);
    lv_bar_set_value(watch_battery_component.battery_bar, 100, LV_ANIM_ON); // 默认满电

    //创建电池外壳图片（顶层，覆盖在进度条上面）
    lv_obj_t *battery_img = lv_img_create(watch_battery_component.battery_container);
    lv_img_set_src(battery_img, &battery);
    lv_obj_align(battery_img, LV_ALIGN_CENTER, 0, 0);

    //添加电量数字显示
    watch_battery_component.battery_label = lv_label_create(watch_battery_component.battery_container);
    lv_obj_set_style_text_font(watch_battery_component.battery_label, &lv_font_montserrat_10, 0);
    lv_obj_align_to(watch_battery_component.battery_label, watch_battery_component.battery_container, LV_ALIGN_OUT_RIGHT_MID, -12, 0);
    static int par666 = 95;
    lv_label_set_text_fmt(watch_battery_component.battery_label, "%d", par666);
}

// ====================== 电池电量更新 ======================
static void update_battery_level(int dat)
{
    if (watch_battery_component.battery_bar == NULL) return;

    // 限制电量在0-100之间
    if (dat < 0) dat = 0;
    if (dat > 100) dat = 100;

    // 更新进度条值
    lv_bar_set_value(watch_battery_component.battery_bar, dat, LV_ANIM_ON); // 带动画更新

    // 进阶：根据电量改变填充颜色
    if (dat <= 20) {
        // 低电量：红色
        lv_obj_set_style_bg_color(watch_battery_component.battery_bar, lv_color_hex(0xFF4444), LV_PART_INDICATOR);
    } else if (dat <= 50) {
        // 中电量：黄色
        lv_obj_set_style_bg_color(watch_battery_component.battery_bar, lv_color_hex(0xFFCC00), LV_PART_INDICATOR);
    } else {
        // 高电量：绿色
        lv_obj_set_style_bg_color(watch_battery_component.battery_bar, lv_color_hex(0x00FFCC), LV_PART_INDICATOR);
    }
}

// ====================== 首页创建 ======================
void create_home_page(void)
{
        /*********************************主窗口****************************/
        watch_window.scr_home_page = lv_obj_create(lv_scr_act());
        lv_obj_set_size(watch_window.scr_home_page,WATCH_SCREEN_W,WATCH_SCREEN_H);
        lv_obj_center(watch_window.scr_home_page);
        //设置基础色（深蓝色）
        lv_obj_set_style_bg_color(watch_window.scr_home_page, lv_color_hex(0xA9D8FD), 0);
        //设置渐变色（白色）
        lv_obj_set_style_bg_grad_color(watch_window.scr_home_page, lv_color_hex(0xFFFFFF), 0);
        //设置渐变方向：从上到下（垂直）
        lv_obj_set_style_bg_grad_dir(watch_window.scr_home_page, LV_GRAD_DIR_VER, 0);

        lv_obj_set_style_radius(watch_window.scr_home_page,15,0);            // 圆角半径15
        lv_obj_set_style_border_width(watch_window.scr_home_page,0,0);  // 边框设0
        lv_obj_set_style_pad_all(watch_window.scr_home_page,0,0);            // 内边距设0

        // 让整个首页可点击
        lv_obj_add_flag(watch_window.scr_home_page, LV_OBJ_FLAG_CLICKABLE);
        //隐藏主菜单
         lv_obj_add_flag(watch_window.scr_window_main, LV_OBJ_FLAG_HIDDEN);
         // 绑定页面切换事件
        lv_obj_add_event_cb(watch_window.scr_home_page, home_page_event_cb, LV_EVENT_PRESSED , NULL);

        //时间标签
        watch_time.label_home_time = lv_label_create(watch_window.scr_home_page);
        lv_label_set_text(watch_time.label_home_time, "--:--");  // 初始占位
        lv_obj_set_style_text_font(watch_time.label_home_time, &lv_font_montserrat_48, 0);
        lv_obj_set_style_text_align(watch_time.label_home_time, LV_TEXT_ALIGN_CENTER, 0);
        lv_obj_set_style_text_color(watch_time.label_home_time, lv_color_black(), 0);
        lv_obj_center(watch_time.label_home_time);
        lv_obj_set_pos(watch_time.label_home_time,-30,-60);

        //日期标签
        watch_time.label_home_date = lv_label_create(watch_window.scr_home_page);
        lv_label_set_text(watch_time.label_home_date,"----.--.--");
        lv_obj_set_style_text_font(watch_time.label_home_date, &lv_font_montserrat_24, 0);
        lv_obj_set_style_text_align(watch_time.label_home_date, LV_TEXT_ALIGN_CENTER, 0);
        lv_obj_set_style_text_color(watch_time.label_home_date, lv_color_black(), 0);
        lv_obj_center(watch_time.label_home_date);
        lv_obj_set_pos(watch_time.label_home_date,-40,-20);

        //温度标签
        label_img.label_weather_temp = lv_label_create(watch_window.scr_home_page);
        lv_label_set_text(label_img.label_weather_temp,"25°C");
        lv_obj_set_style_text_font(label_img.label_weather_temp, &lv_font_montserrat_24, 0);
        lv_obj_set_style_text_align(label_img.label_weather_temp, LV_TEXT_ALIGN_CENTER, 0);
        lv_obj_set_style_text_color(label_img.label_weather_temp, lv_color_black(), 0);
        lv_obj_center(label_img.label_weather_temp);
        lv_obj_set_pos(label_img.label_weather_temp,0,28);

        //引入天气图标
        label_img.img_weather = lv_img_create(watch_window.scr_home_page);
        lv_img_set_src(label_img.img_weather,&cloudey);
        lv_obj_center(label_img.img_weather);
        lv_obj_set_pos(label_img.img_weather,-70,20);

        //电量图标创建
        create_battery_icon(lv_scr_act(),WATCH_SCREEN_W/2-40,-WATCH_SCREEN_H/2+18);

        /*********************************主窗口****************************/

        /*******************************底部窗口****************************/
        watch_window.scr_home_bottom_page = lv_obj_create(watch_window.scr_home_page);
        lv_obj_set_size(watch_window.scr_home_bottom_page,WATCH_SCREEN_W/4*3,WATCH_SCREEN_H/10*3);
        lv_obj_align(watch_window.scr_home_bottom_page,LV_ALIGN_BOTTOM_MID,0,0);
         //设置基础色（浅蓝色）
        lv_obj_set_style_bg_color(watch_window.scr_home_bottom_page, lv_color_hex(0xA9D8FD), 0);
        //设置渐变色（白色）
        lv_obj_set_style_bg_grad_color(watch_window.scr_home_bottom_page, lv_color_hex(0xFFFFFF), 0);
        //设置渐变方向：从上到下（垂直）
        lv_obj_set_style_bg_grad_dir(watch_window.scr_home_bottom_page, LV_GRAD_DIR_VER, 0);

        lv_obj_set_style_radius(watch_window.scr_home_bottom_page,20,0);            // 圆角半径20
        lv_obj_set_style_border_width(watch_window.scr_home_bottom_page,0,0);  // 边框设0
        lv_obj_set_style_pad_all(watch_window.scr_home_bottom_page,0,0);            // 内边距设0

        // 定义线的两个点（起点和终点，画线用）
        static lv_point_t line_points[] = {
            {(WATCH_SCREEN_W/4*3)/2, WATCH_SCREEN_H/10*3-60},  // 上点：屏幕中间x=，y=
            {(WATCH_SCREEN_W/4*3)/2, WATCH_SCREEN_H/10*3-15}  // 下点：屏幕中间x=，y=
        };

        // 创建线条对象,分窗口用
        lv_obj_t *bottom_line = lv_line_create(watch_window.scr_home_bottom_page);
        lv_line_set_points(bottom_line, line_points, 2);  // 设置两个点
        lv_obj_set_style_line_color(bottom_line, lv_color_white(), 0); // 白色
        lv_obj_set_style_line_width(bottom_line, 2, 0);   // 线宽2像素
        lv_obj_set_style_line_opa(bottom_line, LV_OPA_100, 0); // 不透明

        //创建两个标签，分别放置心率和步数
        lv_obj_t *bottom_label_heart1 = lv_label_create(watch_window.scr_home_bottom_page);
        lv_label_set_text(bottom_label_heart1,"80");
        lv_obj_set_style_text_font(bottom_label_heart1, &lv_font_montserrat_24, 0);
        lv_obj_set_style_text_color(bottom_label_heart1, lv_color_black(), 0);
        lv_obj_center(bottom_label_heart1);
        lv_obj_set_pos(bottom_label_heart1,-50,0);
        lv_obj_t *bottom_label_heart2 = lv_label_create(watch_window.scr_home_bottom_page);
        lv_label_set_text(bottom_label_heart2,"心率");
        lv_obj_set_style_text_font(bottom_label_heart2, &lv_font_chinese_16, 0);
        lv_obj_set_style_text_color(bottom_label_heart2, lv_color_black(), 0);
        lv_obj_center(bottom_label_heart2);
        lv_obj_set_pos(bottom_label_heart2,-42,-20);

         //引入图标
        label_img.img_heart = lv_img_create(watch_window.scr_home_bottom_page);
        lv_img_set_src(label_img.img_heart,&heatr);
        lv_obj_center(label_img.img_heart);
        lv_obj_set_pos(label_img.img_heart,-70,-20);

        lv_obj_t *bottom_label_step1 = lv_label_create(watch_window.scr_home_bottom_page);
        lv_label_set_text(bottom_label_step1,"8650");
        lv_obj_set_style_text_font(bottom_label_step1, &lv_font_montserrat_24, 0);
        lv_obj_set_style_text_color(bottom_label_step1, lv_color_black(), 0);
        lv_obj_center(bottom_label_step1);
        lv_obj_set_pos(bottom_label_step1,50,0);
        lv_obj_t *bottom_label_step2 = lv_label_create(watch_window.scr_home_bottom_page);
        lv_label_set_text(bottom_label_step2,"步数");
        lv_obj_set_style_text_font(bottom_label_step2, &lv_font_chinese_16, 0);
        lv_obj_set_style_text_color(bottom_label_step2, lv_color_black(), 0);
        lv_obj_center(bottom_label_step2);
        lv_obj_set_pos(bottom_label_step2,54,-20);

        //引入图标
        label_img.img_run = lv_img_create(watch_window.scr_home_bottom_page);
        lv_img_set_src(label_img.img_run,&run);
        lv_obj_center(label_img.img_run);
        lv_obj_set_pos(label_img.img_run,30,-20);
        /*******************************底部窗口****************************/
}
// ====================== 主菜单创建 ======================
void create_watch_menu(void)
{
        //底层主视图
        watch_window.scr_window_main = lv_obj_create(lv_scr_act());
        lv_obj_set_size(watch_window.scr_window_main,WATCH_SCREEN_W,WATCH_SCREEN_H);
        lv_obj_center(watch_window.scr_window_main);
        lv_obj_set_style_bg_opa(watch_window.scr_window_main,LV_OPA_0,0);
        lv_obj_set_style_border_width(watch_window.scr_window_main,0,0);  // 边框设0
        lv_obj_set_style_pad_all(watch_window.scr_window_main,0,0);            // 内边距设0
        lv_obj_clear_flag( watch_window.scr_window_main, LV_OBJ_FLAG_SCROLLABLE);
        lv_obj_clear_flag( watch_window.scr_window_main, LV_OBJ_FLAG_SCROLL_MOMENTUM);
        lv_obj_clear_flag( watch_window.scr_window_main, LV_OBJ_FLAG_SCROLL_ELASTIC);
        lv_obj_set_style_radius(watch_window.scr_window_main,0,0);            // 圆角半径15

        //滑动主视图
        watch_window.scr_tile_page_main = lv_tileview_create(watch_window.scr_window_main);
        lv_obj_set_size(watch_window.scr_tile_page_main,WATCH_SCREEN_W,WATCH_SCREEN_H);
        lv_obj_center(watch_window.scr_tile_page_main);
        // ===== 关键：默认禁止一切滑动/拖动 =====
        lv_obj_set_scroll_dir(watch_window.scr_tile_page_main, LV_DIR_HOR);  // 显式开启水平滚动
        lv_obj_set_style_radius(watch_window.scr_tile_page_main,0,0);            // 圆角半径15

        //设置基础色（浅橙色）
        lv_obj_set_style_bg_color(watch_window.scr_tile_page_main, lv_color_hex(0x4A90E2), 0);
        // //设置渐变色（褐色）
        // lv_obj_set_style_bg_grad_color(watch_window.scr_tile_page_main, lv_color_hex(0x16213E), 0);
        // //设置渐变方向：从上到下（垂直）
        // lv_obj_set_style_bg_grad_dir(watch_window.scr_tile_page_main, LV_GRAD_DIR_VER, 0);

        // 修改样式，顶部留25像素空白（给电量图标留空间）
        lv_obj_set_style_pad_top(watch_window.scr_tile_page_main, 25, 0);
        // 底部留5像素空白
        lv_obj_set_style_pad_bottom(watch_window.scr_tile_page_main, 5, 0);

        //电量图标创建（以最底层为基准），建立在Tileview视图以上
        create_battery_icon(watch_window.scr_window_main,WATCH_SCREEN_W/2-40,-WATCH_SCREEN_H/2+18);

        //顶部添加时间
        create_top_time_label(watch_window.scr_window_main);

        // 阻止触摸、手势事件冒泡到父对象，防止滑动时触发主页切换
        lv_obj_clear_flag(watch_window.scr_tile_page_main, LV_OBJ_FLAG_EVENT_BUBBLE);
        lv_obj_clear_flag(watch_window.scr_tile_page_main, LV_OBJ_FLAG_GESTURE_BUBBLE);
        //延长滑动动画时间，降低CPU瞬时负载
        lv_obj_set_style_anim_time(watch_window.scr_tile_page_main, 3000, LV_PART_MAIN | LV_STATE_DEFAULT);

        // 滑动视图第1页 (0,0)
        watch_window.scr_tile_page1 = lv_tileview_add_tile(watch_window.scr_tile_page_main, 0, 0, LV_DIR_LEFT | LV_DIR_RIGHT);
        // 禁止 tile 本身滚动
        lv_obj_clear_flag( watch_window.scr_tile_page1, LV_OBJ_FLAG_SCROLLABLE);
        lv_obj_clear_flag( watch_window.scr_tile_page1, LV_OBJ_FLAG_SCROLL_MOMENTUM);
        lv_obj_clear_flag( watch_window.scr_tile_page1, LV_OBJ_FLAG_SCROLL_ELASTIC);
        lv_obj_set_style_pad_all( watch_window.scr_tile_page1, 0, 0);

        create_3x3_grid(watch_window.scr_tile_page1,PAGE_ONE);//第一页
        for(uint8_t i=0; i< (((APP_NUM - CELL_COUNT * PAGE_ONE) > CELL_COUNT) ? CELL_COUNT:(APP_NUM - CELL_COUNT * PAGE_ONE)); i++)
        {
            // 确保格子对象已创建
            if(g_cell_objs[PAGE_ONE][i] == NULL) continue;
            uint8_t app_index = CELL_COUNT * PAGE_ONE + i;  // 计算当前APP索引
            //创建图标
            create_mainstream_icon(g_cell_objs[PAGE_ONE][i],icon_list[CELL_COUNT * PAGE_ONE + i],
                                   &lv_font_fontawesome_28,color_top[CELL_COUNT * PAGE_ONE + i],color_bottom[CELL_COUNT * PAGE_ONE + i],
                                   app_index);                     
        }

        // // 滑动视图第2页 (1,0)
        // watch_window.scr_tile_page2 = lv_tileview_add_tile(watch_window.scr_tile_page_main, 1, 0, LV_DIR_LEFT | LV_DIR_RIGHT);
        // // 禁止 tile 本身滚动
        // lv_obj_clear_flag(watch_window.scr_tile_page2, LV_OBJ_FLAG_SCROLLABLE);
        // lv_obj_clear_flag(watch_window.scr_tile_page2, LV_OBJ_FLAG_SCROLL_MOMENTUM);
        // lv_obj_clear_flag(watch_window.scr_tile_page2, LV_OBJ_FLAG_SCROLL_ELASTIC);
        // lv_obj_set_style_pad_all(watch_window.scr_tile_page2, 0, 0);

        // create_3x3_grid(watch_window.scr_tile_page2,PAGE_TWO);//第二页
        // //第2页只有7个图标
        // for(uint8_t i=0; i <  (((APP_NUM - CELL_COUNT * PAGE_TWO) > CELL_COUNT) ? CELL_COUNT:(APP_NUM - CELL_COUNT * PAGE_TWO)); i++)
        // {
        //      // 确保格子对象已创建.
        //     if(g_cell_objs[PAGE_TWO][i] == NULL) continue;
        //     uint8_t app_index = CELL_COUNT * PAGE_TWO + i;  // 计算当前APP索引
        //     //创建图标
        //     create_mainstream_icon(g_cell_objs[PAGE_TWO][i],icon_list[CELL_COUNT * PAGE_TWO + i],
        //                            &lv_font_fontawesome_28,color_top[CELL_COUNT * PAGE_TWO + i],color_bottom[CELL_COUNT * PAGE_TWO + i],
        //                             app_index);
        // }

    // 底部圆点
    // 创建底部圆点（这时 dot[0]、dot[1] 已生成）
    create_page_indicator(watch_window.scr_window_main);

    //首次启动时立即隐藏，后续由页面切换事件控制
    //LED的创建与隐藏最好分开，不要写在同一个函数下，不然函数一旦跳出后又返回，发现LED隐藏就会重新创建；会出现LED可见->闪烁的效果
    lv_obj_add_flag(dot[0], LV_OBJ_FLAG_HIDDEN);
    lv_obj_add_flag(dot[1], LV_OBJ_FLAG_HIDDEN);

    // 绑定页面切换事件
    lv_obj_add_event_cb(watch_window.scr_tile_page_main, tileview_event_cb, LV_EVENT_VALUE_CHANGED, NULL);
}

void style_init(void) {
    lv_style_init(&style_app_icon_circle);
    lv_style_set_radius(&style_app_icon_circle, LV_RADIUS_CIRCLE);
    lv_style_set_bg_opa(&style_app_icon_circle, LV_OPA_COVER);
    // lv_style_set_shadow_width(&style_app_icon_circle, 4);
    // lv_style_set_shadow_color(&style_app_icon_circle, lv_color_black());
    // lv_style_set_shadow_opa(&style_app_icon_circle, LV_OPA_30);
    // lv_style_set_shadow_ofs_y(&style_app_icon_circle, 2);
    lv_style_set_pad_all(&style_app_icon_circle, 0);
    lv_style_set_border_width(&style_app_icon_circle, 0);
}

/* 动画执行回调 */
static void zoom_anim_cb(void *obj, int32_t value) {
    lv_obj_set_style_transform_zoom((lv_obj_t *)obj,value,0);
}

/* 点击事件回调 */
void app_icon_click_anim(lv_event_t *e) {
    lv_obj_t *target = lv_event_get_target(e);
    lv_event_code_t code = lv_event_get_code(e);

    if (code == LV_EVENT_PRESSED) {
        // lv_anim_t a;
        // lv_anim_init(&a);
        // lv_anim_set_var(&a, target);
        // lv_anim_set_exec_cb(&a, (lv_anim_exec_xcb_t)zoom_anim_cb);
        // lv_anim_set_time(&a, 150);
        // lv_anim_set_values(&a, LV_IMG_ZOOM_NONE, LV_IMG_ZOOM_NONE * 9 / 10);
        // lv_anim_start(&a);

        //获取APP索引，调用app_function.c的逻辑
        uint8_t app_index = (uint8_t)(uintptr_t)lv_obj_get_user_data(target);
        //切换到对应APP页面（调用app_function.c的load_app_page）
        load_app_page(app_index);
        //执行对应APP的功能（后续在app_function.c中实现）
        execute_app_function(app_index);
    } 
    // else if (code == LV_EVENT_RELEASED) {
    //     lv_anim_t a;
    //     lv_anim_init(&a);
    //     lv_anim_set_var(&a, target);
    //     lv_anim_set_exec_cb(&a, (lv_anim_exec_xcb_t)zoom_anim_cb);
    //     lv_anim_set_time(&a, 150);
    //     lv_anim_set_values(&a, lv_obj_get_style_transform_zoom(target, 0), LV_IMG_ZOOM_NONE);
    //     lv_anim_start(&a);

        
    // }
}


/**
 * 创建一个主流圆形渐变APP图标
 * @param *parent 父对象窗口
 * @param *icon_str 图标文本的值（图标图案）
 * @param *font_style 字体样式
 * @param color_top 顶层颜色样式
 * @param color_bottom 底层颜色样式
 * @param app_index APP索引
 * 返回值：图标按钮
 */
lv_obj_t *create_mainstream_icon(lv_obj_t *parent, const char *icon_str,const lv_font_t *font_style, 
                                        lv_color_t color_top, lv_color_t color_bottom,uint8_t app_index)
{
    lv_obj_t *icon_btn = lv_btn_create(parent);
    lv_obj_set_size(icon_btn, ICON_SIZE, ICON_SIZE);
    lv_obj_center(icon_btn);//相对于父对象，居中放置
    lv_obj_add_style(icon_btn, &style_app_icon_circle, 0);
    lv_obj_add_event_cb(icon_btn, app_icon_click_anim, LV_EVENT_ALL, NULL);

    //给按钮设置用户数据 = 对应APP索引
    lv_obj_set_user_data(icon_btn, (void *)(uintptr_t)app_index);

    /* 渐变背景（变量名统一为 color_top 和 color_bottom） */
    lv_obj_set_style_bg_color(icon_btn, color_top, 0);
    lv_obj_set_style_bg_grad_color(icon_btn, color_bottom, 0);
    lv_obj_set_style_bg_grad_dir(icon_btn, LV_GRAD_DIR_VER, 0);

    /* 使用 Font Awesome 字体图标 */
    lv_obj_t *label = lv_label_create(icon_btn);
    lv_label_set_text(label, icon_str);
    //设置标签字体
    lv_obj_set_style_text_font(label, font_style, 0);
    lv_obj_set_style_text_color(label, lv_color_white(), 0);
    lv_obj_center(label);

    return icon_btn;
}

/* mian */
void watch_gui_init(void)
{
    style_init();
    //先创建主菜单，再隐藏；create_home_page要在create_watch_menu后面，不然没有对象隐藏
    create_watch_menu();    // 创建主菜单
    create_home_page();     // 创建首页
    create_app_window();    // 创建APP窗口

    time_display_init();    //初始化时间显示（注册LVGL定时器），用于更新显示时间

    //禁止全屏刷新，只刷变化区域
    lv_disp_set_bg_opa(watch_window.scr_tile_page_main, LV_OPA_COVER);  // 禁止背景透明刷新
    lv_obj_set_style_bg_opa(watch_window.scr_tile_page_main, LV_OPA_COVER, 0);  // 全屏不透明

    // // 禁用不必要的抗锯齿（文字可保留，控件关闭）
    // lv_style_set_antialias(&lv_style_scr, LV_STATE_DEFAULT, 0);
}

//void debug_chinese_font(void)
//{
//    // 测试1：ASCII字符（验证LVGL基本功能）
//    lv_obj_t *label1 = lv_label_create(lv_scr_act());
//    lv_label_set_text(label1, "ASCII Test");
//    lv_obj_align(label1, LV_ALIGN_TOP_MID, 0, 20);
//
//    // 测试2：使用默认字体显示中文（应该显示乱码或方框）
//    lv_obj_t *label2 = lv_label_create(lv_scr_act());
//    lv_label_set_text(label2, "中文测试1");
//    lv_obj_align(label2, LV_ALIGN_TOP_MID, 0, 60);
//
//    // 测试3：使用中文字体
//    lv_obj_t *label3 = lv_label_create(lv_scr_act());
//    lv_obj_set_style_text_font(label3, &lv_font_chinese_14, LV_STATE_DEFAULT);
//    lv_label_set_text(label3, "中文测试2");
//    lv_obj_align(label3, LV_ALIGN_TOP_MID, 0, 100);
//
////    // 测试4：打印字体信息到串口
////    printf("Font height: %d\n", chinese_16.line_height);
////    printf("Font base line: %d\n", chinese_16.base_line);
//}
