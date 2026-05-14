#include "main/include/touch_driver.h"
#include "main/include/disp_driver.h"
#include "driver/i2c.h"
#include "driver/gpio.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

// static const char *TAG = "TOUCH";

// ---------- I2C 写/读（稳定版，不修改驱动）----------
// static esp_err_t i2c_write_reg(uint8_t reg, uint8_t *data, uint8_t len) {
//     i2c_cmd_handle_t cmd = i2c_cmd_link_create();
//     i2c_master_start(cmd);
//     i2c_master_write_byte(cmd, (FT6336G_ADDR << 1) | I2C_MASTER_WRITE, true);
//     i2c_master_write_byte(cmd, reg, true);
//     if (data && len) {
//         i2c_master_write(cmd, data, len, true);
//     }
//     i2c_master_stop(cmd);
//     esp_err_t ret = i2c_master_cmd_begin(I2C_MASTER_NUM, cmd, 50 / portTICK_PERIOD_MS);
//     i2c_cmd_link_delete(cmd);
//     return ret;
// }

static esp_err_t i2c_read_reg(uint8_t reg, uint8_t *data, uint8_t len) {
    if (len == 0) return ESP_OK;
    i2c_cmd_handle_t cmd = i2c_cmd_link_create();
    i2c_master_start(cmd);
    i2c_master_write_byte(cmd, (FT6336G_ADDR << 1) | I2C_MASTER_WRITE, true);
    i2c_master_write_byte(cmd, reg, true);
    i2c_master_start(cmd);      // 重复起始
    i2c_master_write_byte(cmd, (FT6336G_ADDR << 1) | I2C_MASTER_READ, true);
    if (len > 1) {
        i2c_master_read(cmd, data, len - 1, I2C_MASTER_ACK);
    }
    i2c_master_read_byte(cmd, data + len - 1, I2C_MASTER_NACK);
    i2c_master_stop(cmd);
    //执行IIC相关任务
    esp_err_t ret = i2c_master_cmd_begin(I2C_MASTER_NUM, cmd, 100 / portTICK_PERIOD_MS);
    i2c_cmd_link_delete(cmd);
    return ret;
}

// ---------- FT6336 封装（适配旧版 FT6336_WR_Reg/RD_Reg 接口）----------
// static uint8_t FT6336_write_Reg(uint8_t reg, uint8_t *buf, uint8_t len) {
//     if (i2c_write_reg(reg, buf, len) != ESP_OK) return 1;
//     return 0;
// }

static void FT6336_read_Reg(uint8_t reg, uint8_t *buf, uint8_t len) {
    i2c_read_reg(reg, buf, len);
}

// ---------- 触摸初始化（确保驱动被安装）----------
void app_touch_init(void) {
    // 配置复位引脚（输出高电平）
    gpio_config_t rst_conf = {
        .pin_bit_mask = (1ULL << TOUCH_RST_GPIO),
        .mode = GPIO_MODE_OUTPUT
    };
    gpio_config(&rst_conf);
    gpio_set_level(TOUCH_RST_GPIO, 1);
    vTaskDelay(pdMS_TO_TICKS(10));

    // 初始化 I2C 驱动
    i2c_config_t i2c_conf = {
        .mode = I2C_MODE_MASTER,
        .sda_io_num = TOUCH_SDA_GPIO,
        .scl_io_num = TOUCH_SCL_GPIO,
        .sda_pullup_en = GPIO_PULLUP_ENABLE,
        .scl_pullup_en = GPIO_PULLUP_ENABLE,
        .master.clk_speed = I2C_MASTER_FREQ_HZ,
    };
    ESP_ERROR_CHECK(i2c_param_config(I2C_MASTER_NUM, &i2c_conf));
    ESP_ERROR_CHECK(i2c_driver_install(I2C_MASTER_NUM, I2C_MODE_MASTER, 0, 0, 0));

    //硬件复位 + 延时等待芯片就绪
    gpio_set_level(TOUCH_RST_GPIO, 0);
    vTaskDelay(pdMS_TO_TICKS(10));
    gpio_set_level(TOUCH_RST_GPIO, 1);
    vTaskDelay(pdMS_TO_TICKS(500));

    // 读取 ID 验证
    uint8_t temp;
    FT6336_read_Reg(FT_ID_G_FOCALTECH_ID, &temp, 1);
    // ESP_LOGI(TAG, "Vendor ID: 0x%02X (expect 0x11)", temp);

    uint8_t mid, low, high;
    FT6336_read_Reg(FT_ID_G_CIPHER_MID, &mid, 1);
    FT6336_read_Reg(FT_ID_G_CIPHER_LOW, &low, 1);
    FT6336_read_Reg(FT_ID_G_CIPHER_HIGH, &high, 1);
    // ESP_LOGI(TAG, "Chip ID: 0x%02X 0x%02X 0x%02X", high, mid, low);
}

// ---------- LVGL 输入回调（轻量，不阻塞）----------
void app_touch_read(lv_indev_drv_t *indev_drv, lv_indev_data_t *data) {
    static uint32_t smooth_x = 0, smooth_y = 0;
    static bool was_pressed = false;    // 记录按键是否按下
    static bool first_touch = true;  // 标记是否需要初始化平滑值

    uint8_t mode = 0;
    FT6336_read_Reg(FT_REG_NUM_FINGER, &mode, 1);

    // mode 必须严格等于 1（单点触摸）或 2（多点），且坐标有效
    if (mode == 1 || mode == 2) 
    {
        uint8_t buf[4];
        FT6336_read_Reg(FT_TP1_REG, buf, 4);

        // FT6336 数据格式：XH[3:0]=X[11:8], XL=X[7:0], YH[3:0]=Y[11:8], YL=Y[7:0]
        uint16_t x = ((uint16_t)(buf[0] & 0x0F) << 8) | buf[1];
        uint16_t y = ((uint16_t)(buf[2] & 0x0F) << 8) | buf[3];

        // 有效性检查：坐标必须在屏幕范围内，且不能是0（未初始化值）
        // FT6336 典型最大值：X=240, Y=320 左右
        if (x <= 0 || y <= 0 || x > 240 || y > 320) 
        {
            data->state = LV_INDEV_STATE_REL;
            if (was_pressed) {
                // 保持最后一次坐标，避免释放时坐标跳变
                data->point.x = smooth_x;
                data->point.y = smooth_y;
            }
            was_pressed = false;
            return;
        }

    #if LCD_INVERT_NO
            uint16_t mapped_x = 239 - x;
            uint16_t mapped_y = y;
    #elif LCD_INVERT_Y
            uint16_t mapped_x = 239 - x;
            uint16_t mapped_y = 319 - y;
    #elif LCD_INVERT_X
            uint16_t mapped_x = x;
            uint16_t mapped_y = y;
    #elif LCD_INVERT_XY
            uint16_t mapped_x = x;
            uint16_t mapped_y = 319 - y;
    #endif

        // 低通滤波：首次触摸直接赋值，避免从 0 开始导致位置滞后
        if (first_touch)
        {
            smooth_x = mapped_x;
            smooth_y = mapped_y;
            first_touch = false;
        } else 
        {
            smooth_x = (smooth_x * 3 + mapped_x) / 4;
            smooth_y = (smooth_y * 3 + mapped_y) / 4;
        }

        //X、Y坐标
        data->point.x = smooth_x;
        data->point.y = smooth_y;
        data->state = LV_INDEV_STATE_PR;
        was_pressed = true;     //手指按下
    } 
    else 
    {
            data->state = LV_INDEV_STATE_REL;
            if (was_pressed) 
            {
                data->point.x = smooth_x;
                data->point.y = smooth_y;
            }
            was_pressed = false;   //手指抬起
            first_touch = true;  // 手指抬起后，下次触摸重新初始化平滑值
    }
}

//静态全局变量，BSS段
static lv_indev_drv_t indev_drv;                 // 一个输入设备驱动描述结构体
//lv_indev_drv_t结构体成员一定要static修饰，因为：
/* lv_indev_drv_register 内部会立即复制驱动结构体中的一些关键字段到新分配的 lv_indev_t 对象中
，但它并没有完整拷贝整个结构体，而是只拷贝了部分成员，并保留了对原结构体某些成员的指针引用（
例如 user_data、自定义的 driver_data 等）。更重要的是，在某些 LVGL 8.3 版本中，
输入设备的回调函数指针 read_cb 虽然被复制，但 LVGL 在处理输入设备时可能会在后续流程中再次读取原始驱动结构体，而不是完全依赖于注册时的拷贝。
当 touch_lvgl_register 函数返回后，栈上的 indev_drv 变量立即失效，内存被回收。
后续 LVGL 任务（如 lv_timer_handler）在处理输入设备时，如果再次访问这个已失效的栈内存，就会读到随机数据，导致：
read_cb 函数指针被破坏 → LVGL 调用了一个无效地址，引发 LoadProhibited 崩溃（你之前遇到过的）。
或者 read_cb 碰巧指向某个可执行地址但功能错误 → 返回了虚假的触摸状态（例如一直报告按下），导致屏幕持续刷新。
甚至user_data 被更改 → 传递错误参数，引发不可预料的行为，而使用该变量不再随函数返回而销毁，始终保持在初始化时的数值，
LVGL 后续对其的任何引用都是安全的，所以一切恢复正常 */

/*本次问题的根本原因是 lv_indev_drv_t 使用了局部变量而非静态变量。
虽然 LVGL 的 lv_indev_drv_register 内部会对驱动结构体进行 memcpy 复制，
但在 ESP32 的 Xtensa 架构下，函数返回后栈空间立即被复用，
导致 lv_indev_drv_t 原栈内存中的某些字段（如设备类型 type 或内部状态）被后续函数调用破坏。
这使得 LVGL 的输入设备处理函数 lv_indev_handler 在解析触摸状态时进入错误分支，持续误标屏幕右下角区域为脏，
引发无限刷新；同时 lv_indev_set_cursor 因访问被破坏的指针而崩溃，导致触摸注册失败、点击无响应。将 lv_indev_drv_t 改为 static 后，
变量存储于 BSS 段，生命周期贯穿程序始终，避免了栈复用破坏，屏幕刷新和触摸功能均恢复正常*/

void touch_lvgl_register(void){  
    // 注册触摸输入设备  
    //使用static修饰结构体，防止函数结束后，系统回收/复用空间，导致后续回调函数/组件中的相关处理函数找不到类中的成员/正确的路径
    //lv_indev_drv_t indev_drv; 
    lv_indev_drv_init(&indev_drv);            // 用 LVGL 官方默认值初始化它（安全做法），lv_indev_drv_init：保证结构体里不出现随机值，是移植的官方推荐范例。
    indev_drv.type = LV_INDEV_TYPE_POINTER;   // 触摸屏属于指针设备 // 指定设备类型：触摸屏/鼠标
    indev_drv.read_cb = app_touch_read;           // 你写好的读取函数   //  绑定你写的硬件读取回调函数
    lv_indev_t * indev = lv_indev_drv_register(&indev_drv);        /*注册到 LVGL ，正式注册进 LVGL 的内核列表lv_indev_drv_register：将驱动挂载到 LVGL 内部，此后所有控件都能自动响应触摸*/
    if (indev == NULL) {
        return;
    }
    // // 注册触摸输入设备，并接收返回的设备句柄
    // // 用返回的句柄彻底移除光标
    //lv_indev_set_cursor(indev, NULL); // ← 删除！有bug
}
