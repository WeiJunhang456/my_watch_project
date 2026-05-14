#ifndef APP_FUNCTION_H
#define APP_FUNCTION_H

#include "main/include/gui_app.h"
#include "main/include/wifi_iocn_function.h"
#include "main/include/timer_iocn_function.h"
#include "main/include/weather_iocn_function.h"
#include "main/include/heart_iocn_function.h"
#include "main/include/heart_adc_collect.h"

#define APP_NUM 5

/*想用 app_func_array[index]() → 必须声明它是 void (*)(void) 类型（函数指针类型）！
因为：这个数组里存的不是数字、不是字符串，存的是函数的地址，编译器必须知道：这是一个无参数、无返回值的函数指针*/
//我想用变量索引调用函数 → 必须用函数指针数组 → 必须告诉编译器数组里存的是函数（void (*)(void)），通过函数指针转到相应函数

// 声明APP功能回调类型，将void (*)(void) 类型修饰取别名app_func_cb_t
typedef void (*app_func_cb_t)(void);



// 告诉编译器这个变量在其他.c文件中定义
// void (*)(void)与app_func_cb_t等价 ，所以void (*app_func_array[APP_NUM])(void) = app_func_cb_t app_func_array[APP_NUM]
extern void (*app_func_array[APP_NUM])(void);  

// 1. 定义各APP的功能回调函数   
void app_clock_func(void);   /* 时钟功能逻辑 */   
void app_bluetooth_func(void);/* 蓝牙功能逻辑 */
void app_weather_func(void);      /* 天气功能逻辑 */  
void app_heart_func(void);   /* 心率功能逻辑 */   

// 声明APP页面切换函数（已有）
void load_app_page(int index);
// 声明APP窗口创建函数（已有）
void create_app_window(void);
// 声明各APP功能初始化函数
void app_functions_init(void);
void execute_app_function(uint8_t index);
//统一退出路径
void app_exit(void);

#endif