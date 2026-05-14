#include "main/include/disp_driver.h"
#include "driver/spi_master.h"
#include "driver/gpio.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_log.h"
#include "main/include/gui_app.h"

// static const char *TAG = "DISP";

//双缓存实现并行渲染效果，提高FPS                                          
static lv_disp_draw_buf_t draw_buf;
static lv_color_t buf1[LV_BUF_SIZE];
static lv_color_t buf2[LV_BUF_SIZE];

//SPI句柄
spi_device_handle_t spi_dev;

// 用于在完成回调中调用 lv_disp_flush_ready 的驱动指针
lv_disp_drv_t *g_disp_drv = NULL;

// 非阻塞传输完成标志（分块传输时需统计）
volatile int g_trans_pending = 0;

// // ---------- 内部辅助：手动 CS ----------
// static inline void lcd_select(void) {
//     gpio_set_level(PIN_LCD_CS, 0);
// }
// static inline void lcd_deselect(void) {
//     gpio_set_level(PIN_LCD_CS, 1);
// }

// ---------- 基础 SPI 写，硬件 SPI，一次8位匹配从机----------
static void lcd_write_byte(uint8_t data) {
    spi_transaction_t t = {
        .length = 8,
        .tx_buffer = &data
    };
    // 启动 SPI 传输并阻塞等待，直到8位数据发送完成所有的数据
    spi_device_polling_transmit(spi_dev, &t);
}

// 16位数据发送：根据 LV_COLOR_16_SWAP 决定字节顺序
// 固定大端模式（使用了DMA）
static void lcd_send_data(uint16_t data)
{
    gpio_set_level(PIN_LCD_DC, 1);
// #if LV_COLOR_16_SWAP
//     lcd_write_byte(data & 0xFF);        // 先低字节
//     lcd_write_byte(data >> 8);
// #else
    // 固定大端序，坐标发送必须高字节在前
    lcd_write_byte(data >> 8);          // 先高字节
    lcd_write_byte(data & 0xFF);
// #endif
}

// 命令模式
static void lcd_write_cmd(uint8_t cmd) {
    gpio_set_level(PIN_LCD_DC, 0);
    lcd_write_byte(cmd);
}

// 数据模式
static void lcd_write_data(uint8_t data) {
    gpio_set_level(PIN_LCD_DC, 1);
    lcd_write_byte(data);
}

// 设置窗口区域（画屏幕）
static void lcd_set_window(uint16_t x1, uint16_t y1, uint16_t x2, uint16_t y2)
{
    lcd_write_cmd(0x2A);    // 列地址
    lcd_send_data(x1);
    lcd_send_data(x2);

    lcd_write_cmd(0x2B);    // 行地址
    lcd_send_data(y1);
    lcd_send_data(y2);

    lcd_write_cmd(0x2C);    // 开始写 GRAM
}

// ---------- 显示驱动回调（DMA分段传输）----------
//ESP32是小端模式，但是ILI9341期望的数据是大端，所以要去lv_conf.h设置LV_COLOR_16_SWAP宏，将ESP32的小端发送数据交换成大端再发送
static void disp_flush(lv_disp_drv_t *drv, const lv_area_t *area, lv_color_t *color_p) {
    uint32_t w = area->x2 - area->x1 + 1;   //x轴长度
    uint32_t h = area->y2 - area->y1 + 1;   //y轴长度
    uint32_t size = w * h;                  //总像素数
    uint32_t total_bytes = size * sizeof(lv_color_t);   //total_bytes 是这些像素总共占用的字节数（每个像素 2 字节，RGB565）

    //设置LCD屏幕
    lcd_set_window(area->x1, area->y1, area->x2, area->y2);
    gpio_set_level(PIN_LCD_DC, 1);   // 数据模式，发送像素数据

    //指向当前要发送的数据块起始地址
    uint8_t *buf_ptr = (uint8_t *)color_p;
    //保留总共占用的字节数的初值
    uint32_t bytes_remaining = total_bytes;

    // 清空待传输计数
    int trans_count = 0;
    
    while(bytes_remaining > 0)
    {
        //本次发送的字节数，一次最多发送MAX_DMA_BYTES byte
        uint32_t chunk_size = (bytes_remaining > MAX_DMA_BYTES) ? MAX_DMA_BYTES : bytes_remaining;

        //初始化SPI事务分配空间，结构体作用是传输一次SPI
        spi_transaction_t *trans = calloc(1, sizeof(spi_transaction_t));
        assert(trans != NULL);      //调试用程序，有问题会打印错误
        trans->length = chunk_size * 8;  // 数据总长度，长度单位是bit（byte转bit）
        trans->tx_buffer = buf_ptr;      //指向数据块地址

        // 启动 SPI 传输并阻塞等待，直到 DMA 完成所有数据的发送
        // 此函数内部：
        // 1. 获取 SPI 总线控制权（spi_dev句柄传入）
        // 2. 配置并启动 DMA 引擎，开始从 buf_ptr 搬运数据到 SPI 外设（trans参数传入）
        // 3. 轮询等待 DMA 传输完成标志
        // 4. 释放总线控制权
        // spi_device_polling_transmit(spi_dev,&trans);

        // 将事务放入 SPI 队列，非阻塞
        esp_err_t ret = spi_device_queue_trans(spi_dev, trans, 0);   // 0 = 不等待
        if (ret != ESP_OK) {
            // 队列满，等待一会儿重试（正常情况下不会）
            vTaskDelay(pdMS_TO_TICKS(1));
            ret = spi_device_queue_trans(spi_dev, trans, portMAX_DELAY);
            assert(ret == ESP_OK);
        }

        // 指针向前移动，指向下一个数据块待发送数据
        buf_ptr += chunk_size;
        // 更新剩余字节数
        bytes_remaining -= chunk_size;
        //记统计本次 disp_flush 调用一共产生了多少个 DMA 事务（即分了多少块）
        trans_count++;
    }

    // 记录这次 disp_flush 产生的分块数量，方便后续flush_finish_task计算块来判断DMA有没有传输完毕
    g_trans_pending = trans_count;

    //注册LVGL回调，通知LVGL可以重用缓冲区进行下一轮绘制（改到flush_finish_task调用）
    // lv_disp_flush_ready(drv);
}

// ---------- 屏幕初始化（使用已验证的极简序列 + 伽马）----------
static void lcd_init_cmds(void) {
    // 硬件复位
    gpio_set_level(PIN_LCD_RST, 0); //先拉低复位
    vTaskDelay(pdMS_TO_TICKS(10));
    gpio_set_level(PIN_LCD_RST, 1);
    vTaskDelay(pdMS_TO_TICKS(120)); 

    //软件复位
    lcd_write_cmd(0x01);   
    vTaskDelay(pdMS_TO_TICKS(150));
 
    lcd_write_cmd(0x11);   // 退出睡眠
    vTaskDelay(pdMS_TO_TICKS(150));

    lcd_write_cmd(0x3A);   // 16-bit 像素格式
    lcd_write_data(0x55);

    // Gamma 校准 (提高显示效果)
    lcd_write_cmd(0xE0);   // 正伽马
    lcd_write_data(0x0F); lcd_write_data(0x35); lcd_write_data(0x31); lcd_write_data(0x0B);
    lcd_write_data(0x0E); lcd_write_data(0x06); lcd_write_data(0x49); lcd_write_data(0xA7);
    lcd_write_data(0x33); lcd_write_data(0x07); lcd_write_data(0x0F); lcd_write_data(0x03);
    lcd_write_data(0x0C); lcd_write_data(0x0A); lcd_write_data(0x00);

    lcd_write_cmd(0xE1);   // 负伽马
    lcd_write_data(0x00); lcd_write_data(0x0A); lcd_write_data(0x0F); lcd_write_data(0x04);
    lcd_write_data(0x11); lcd_write_data(0x08); lcd_write_data(0x36); lcd_write_data(0x58);
    lcd_write_data(0x4D); lcd_write_data(0x07); lcd_write_data(0x10); lcd_write_data(0x0C);
    lcd_write_data(0x32); lcd_write_data(0x34); lcd_write_data(0x0F);

#if LCD_INVERT_NO
    lcd_write_cmd(0x36);   // MADCTL
    lcd_write_data(0x08);   //改为 BGR 顺序
#elif LCD_INVERT_Y
    lcd_write_cmd(0x36);   // MADCTL
    lcd_write_data(0x88);  // MY | BGR ：上下翻转，保持 BGR
#elif LCD_INVERT_X
    lcd_write_cmd(0x36);   // MADCTL
    lcd_write_data(0x48);  // MX | BGR ：上下翻转，保持 BGR
#elif LCD_INVERT_XY
    lcd_write_cmd(0x36);   // MADCTL
    lcd_write_data(0xC8);  // MX | MY | BGR ：上下翻转，保持 BGR
#endif

    lcd_write_cmd(0x21);   // 反转显示
    lcd_write_cmd(0x29);   // 开显示
    vTaskDelay(pdMS_TO_TICKS(100));
}

// ==================== 电源与背光 ====================
static void lcd_gpio_init(void)
{
    //设置引脚模式
    gpio_set_direction(PIN_LCD_DC, GPIO_MODE_OUTPUT);
    gpio_set_direction(PIN_LCD_RST, GPIO_MODE_OUTPUT);
    gpio_set_direction(PIN_LCD_BL, GPIO_MODE_OUTPUT);

    gpio_set_level(PIN_LCD_BL, 1);      // 点亮背光
}

// ==================== 总线与设备初始化 ====================
static void lcd_spi_init(void)
{
    spi_bus_config_t buscfg = {
        .mosi_io_num = PIN_MOSI,
        .miso_io_num = -1,          // 不接 MISO
        .sclk_io_num = PIN_SCLK,
        .quadwp_io_num = -1,
        .quadhd_io_num = -1,
        .max_transfer_sz = 4092 //不超过4092，SPI DMA 默认最大传输长度是 4092 字节（受限于 DMA 描述符）
    };

    spi_device_interface_config_t devcfg = {
        .mode = 0,                  // SPI 模式 0
        //速率不可以超过26MHZ（双缓存）
        .clock_speed_hz = 26 * 1000 * 1000,   // 26 MHz
        .spics_io_num = PIN_LCD_CS,
        .queue_size = 7,
        .pre_cb = NULL,
        .post_cb = NULL     //SPI驱动中断，这里不使用
    };

    spi_bus_initialize(LCD_HOST, &buscfg, SPI_DMA_CH_AUTO);   //启用DMA,通道系统自动选择
    spi_bus_add_device(LCD_HOST, &devcfg, &spi_dev);
}

//LCD硬件初始化
void lcd_init(void)
{
    lcd_gpio_init();
    lcd_spi_init(); 
    lcd_init_cmds();  
}

// ---------- LVGL 端口初始化(main.c调用) ----------
void lvgl_disp_init(void) {
    //LVGL核心初始化，调用LVGL组件
    lv_init();
    // 设置显示缓冲区 (大小可根据内存调整)
    lv_disp_draw_buf_init(&draw_buf, buf1, buf2, LV_BUF_SIZE);

    //使用static修饰，防止函数结束后，系统回收复用空间，导致后续回调函数找不到类中的成员
    static lv_disp_drv_t disp_drv;
    //LVGL初始化显示驱动（先拿驱动默认值）
    lv_disp_drv_init(&disp_drv);
    disp_drv.hor_res = WATCH_SCREEN_W;     // 屏幕宽度
    disp_drv.ver_res = WATCH_SCREEN_H;     // 屏幕高度
    disp_drv.flush_cb = disp_flush;        // 注册回调函数
    disp_drv.draw_buf = &draw_buf;
    // 如果屏幕小且 SPI 快，可以设为 0；如果撕裂严重，设为 1
    disp_drv.full_refresh = 1;
    disp_drv.direct_mode = 0;
    lv_disp_drv_register(&disp_drv);

    //将驱动值复制一份（注册驱动）
    g_disp_drv = &disp_drv;
}