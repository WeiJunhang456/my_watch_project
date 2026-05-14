#ifndef DISP_DRIVER_H
#define DISP_DRIVER_H

#include "lvgl.h"
#include "driver/spi_master.h"   // 让所有包含此头文件的模块都能使用 SPI 类型

// 引脚定义（与你成功点亮的代码一致）
#define PIN_LCD_CS   5
#define PIN_LCD_DC   26
#define PIN_LCD_RST  25
#define PIN_LCD_BL   27
#define PIN_MOSI     23
#define PIN_SCLK     18
#define PIN_MISO     19
#define LCD_HOST    SPI2_HOST

//控制液晶屏反转
#define LCD_INVERT_NO 0
#define LCD_INVERT_Y 1
#define LCD_INVERT_X 0
#define LCD_INVERT_XY 0

// ESP32 DMA单次传输最大长度为4092字节
#define MAX_DMA_BYTES 4092 
// ===== LVGL 缓冲区 =====
#define LV_BUF_SIZE (WATCH_SCREEN_W * 30)   //240×100 = 24000 像素，约 48KB 每块
                                            //WATCH_SCREEN_H 屏幕水平分辨率

// 全局驱动指针，供 main 的任务使用                                           
extern lv_disp_drv_t *g_disp_drv;
extern volatile int g_trans_pending;    // 记录当前传输段数
extern spi_device_handle_t spi_dev;     //访问SPI句柄
                                            
// 初始化硬件和 LVGL 显示接口
void lcd_init(void);
void lvgl_disp_init(void);

#endif