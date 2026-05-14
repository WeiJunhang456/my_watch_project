#ifndef TOUCH_DRIVER_H
#define TOUCH_DRIVER_H

#include "lvgl.h"

#define TOUCH_SCL_GPIO     22
#define TOUCH_SDA_GPIO     21
#define TOUCH_RST_GPIO     33
#define TOUCH_INT_GPIO     35

#define I2C_MASTER_NUM     I2C_NUM_0
#define I2C_MASTER_FREQ_HZ 400000       // 400 kHz，FT6336G 支持

// FT6336G 7位从机地址
#define FT6336G_ADDR       0x38

// 寄存器定义
#define FT_DEVIDE_MODE      0x00
#define FT_REG_NUM_FINGER   0x02
#define FT_TP1_REG          0x03
#define FT_ID_G_CIPHER_MID  0x9F    // 芯片代号（中字节），地址 0x9F
#define FT_ID_G_CIPHER_LOW  0xA0    // 芯片代号（低字节），地址 0xA0，0x01 代表 FT6336G
#define FT_ID_G_CIPHER_HIGH 0xA3    // 芯片代号（高字节），地址 0xA3
#define FT_ID_G_FOCALTECH_ID 0xA8   // VENDOR ID，地址 0xA8，默认值应为 0x11

void app_touch_init(void);
void app_touch_read(lv_indev_drv_t *indev_drv, lv_indev_data_t *data);
void touch_lvgl_register(void);

#endif