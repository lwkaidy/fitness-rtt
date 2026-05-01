/*
 * Copyright (c) 2026, RT-Thread Development Team
 * SPDX-License-Identifier: Apache-2.0
 *
 * File   : fitness_ctrl.h
 * Brief  : 健身器材控制器 — 全局定义
 *          基于 FRDM-MCXA156 + RT-Thread v5.2.1 标准 BSP
 *
 * 引脚确认来源：
 *   drv_pin.h   → GET_PINS(port, pin) = 32*port + pin
 *   main.c      → LED_PIN = (3*32)+12  (GPIO3_12)
 *   pin_mux.c   → 各外设引脚复用配置
 */

#ifndef __FITNESS_CTRL_H__
#define __FITNESS_CTRL_H__

#include <rtthread.h>
#include <rtdevice.h>
#include "drv_pin.h"   /* GET_PINS 宏 */

/* ─── 设备名称（对应 drv_xxx.c 中注册的名称）────────────────────────── */
#define FITNESS_ADC_DEV     "adc0"      /* drv_adc.c: BSP_USING_ADC0 */
#define FITNESS_PWM_DEV     "pwm0"      /* drv_pwm.c: BSP_USING_PWM0 */
#define FITNESS_WDT_DEV     "wdt"       /* drv_wdt.c: RT_USING_WDT   */
#define FITNESS_UART_DEV    "uart0"     /* drv_uart.c: console 串口   */

/* ─── ADC 通道 ─────────────────────────────────────────────────────────
 * LPADC0 通道 → 引脚映射 (参考 pin_mux.c)
 *   CH0 → P0_0 (Arduino A0)  踏频霍尔传感器
 *   CH1 → P0_1 (Arduino A1)  阻力反馈电流
 */
#define ADC_CH_CADENCE      0           /* 踏频/转速 */
#define ADC_CH_RESISTANCE   1           /* 阻力反馈电流 */
#define ADC_FULL_SCALE      65535U      /* 16-bit 高精度模式 */
#define ADC_VREF_MV         3300U       /* 参考电压 mV */

/* 霍尔传感器过零迟滞阈值 (16-bit ADC, 约 2.2V/1.9V) */
#define HALL_THRESH_HIGH    43690U      /* ~2.20V → 43690/65535*3300 */
#define HALL_THRESH_LOW     37888U      /* ~1.90V → 37888/65535*3300 */
#define MAGNETS_PER_REV     1U          /* 飞轮每圈磁铁数 */

/* ─── PWM 通道（submodule 0 = pwm0）────────────────────────────────────
 * rt_pwm_set(dev, channel, period_ns, pulse_ns)
 * channel = 0 对应 drv_pwm.c 中 kPWM_Module_0
 * 20 kHz → period = 50000 ns
 */
#define PWM_CHANNEL         0U
#define PWM_PERIOD_NS       50000U      /* 20 kHz */
#define PWM_PULSE_MIN_NS    5000U       /* 10% 最小占空比 */
#define PWM_PULSE_MAX_NS    45000U      /* 90% 最大占空比 */
#define RESISTANCE_LV_MIN   1U
#define RESISTANCE_LV_MAX   8U

/* ─── GPIO 引脚（GET_PINS 确认于 drv_pin.h / main.c）──────────────────
 * board.h:  LED_RED = GPIO3[12], LED_GREEN = GPIO3[13], LED_BLUE = GPIO3[0]
 * 板载按键: SW3 = GPIO0[6], SW2 = GPIO1[7]（低电平有效，内部上拉）
 */
#define PIN_KEY_UP          GET_PINS(0, 6)   /* 板载 SW3 → 阻力+ */
#define PIN_KEY_DOWN        GET_PINS(1, 7)   /* 板载 SW2 → 阻力- */
#define PIN_KEY_MODE        GET_PINS(3, 3)   /* 外接 Arduino D5 → 模式切换 */

#define PIN_LED_R           GET_PINS(3, 2)   /* 外接 Arduino D4 (P3_2) → LED 红色 */
#define PIN_LED_G           GET_PINS(3, 1)   /* 外接 Arduino D3 (P3_1) → LED 绿色 */
#define PIN_LED_B           GET_PINS(3, 0)   /* 外接 Arduino D2 (P3_0) → LED 蓝色 */

/* ─── 任务优先级 / 栈 ──────────────────────────────────────────────── */
#define TASK_PRIO_WDT       5
#define TASK_PRIO_KEY       6
#define TASK_PRIO_SVC       9
#define TASK_PRIO_SENSOR    11
#define TASK_PRIO_DISPLAY   12
#define TASK_PRIO_LED       13

#define STACK_WDT           512
#define STACK_KEY           512
#define STACK_SVC           2048
#define STACK_SENSOR        1024
#define STACK_DISPLAY       2048
#define STACK_LED           512

/* ─── 运动模式 ─────────────────────────────────────────────────────── */
typedef enum {
    FIT_MODE_FREE      = 0,   /* 自由骑行 */
    FIT_MODE_INTERVAL  = 1,   /* 间歇训练 */
    FIT_MODE_PROGRAM   = 2,   /* 预设程序 */
    FIT_MODE_MAX
} fit_mode_t;

/* ─── 全局运动状态（受 mtx_state 保护）────────────────────────────── */
typedef struct {
    /* 传感器 */
    rt_uint32_t rpm;                /* 踏频 rpm */
    rt_uint32_t speed_km_h_x10;     /* 速度 × 10 (km/h) */
    rt_bool_t   is_rotating;
    rt_uint32_t resistance_fb_mv;   /* 阻力反馈电压 mV */

    /* 控制 */
    rt_uint8_t  resistance_level;   /* 阻力等级 1~8 */
    rt_uint32_t pwm_pulse_ns;       /* 当前 PWM 脉宽 ns */

    /* 统计 */
    rt_uint32_t elapsed_s;          /* 运动时长 s */
    rt_uint32_t total_rev;          /* 累计转数 */
    rt_uint32_t calorie_x10;        /* 卡路里 × 10 */
    rt_uint32_t distance_m;         /* 里程 m */

    /* 系统 */
    fit_mode_t  mode;
    rt_uint32_t wdt_feed_cnt;       /* 喂狗次数（调试用） */
} fit_state_t;

/* ─── IPC 对象声明（在 main.c 中定义）──────────────────────────────── */
extern struct rt_mailbox  mb_rpm;       /* sensor→service: rpm值 */
extern struct rt_semaphore sem_key_up;
extern struct rt_semaphore sem_key_down;
extern struct rt_semaphore sem_key_mode;
extern struct rt_mutex     mtx_state;
extern fit_state_t         g_fit;       /* 全局状态 */

/* ─── LED 接口（在 led_task.c 中定义）─────────────────────────────── */
extern void led_flash_request(rt_uint8_t times);   /* 请求白色闪烁 */
extern void led_set_wdt_warn(rt_bool_t warn);       /* 设置看门狗告警 */

/* ─── 内联算法 ──────────────────────────────────────────────────────── */

/** 阻力等级 → PWM 脉宽(ns)，线性映射 10%~90% */
static inline rt_uint32_t level_to_pulse_ns(rt_uint8_t lv)
{
    if (lv < RESISTANCE_LV_MIN) lv = RESISTANCE_LV_MIN;
    if (lv > RESISTANCE_LV_MAX) lv = RESISTANCE_LV_MAX;
    rt_uint32_t range = PWM_PULSE_MAX_NS - PWM_PULSE_MIN_NS;
    return PWM_PULSE_MIN_NS + range * (lv - 1U) / (RESISTANCE_LV_MAX - 1U);
}

/** 速度换算 km/h×10（飞轮周长 1700 mm）*/
static inline rt_uint32_t rpm_to_speed_x10(rt_uint32_t rpm)
{
    return rpm * 1700U * 6U / 100000U;
}

/** 卡路里×10 (MET≈4+level*0.5, weight=70kg) */
static inline rt_uint32_t calc_calorie_x10(rt_uint32_t rpm, rt_uint8_t lv, rt_uint32_t elapsed_s)
{
    rt_uint32_t met_x10 = 40U + (rt_uint32_t)lv * 5U;
    return met_x10 * 70U * elapsed_s / 3600U;
}

#endif /* __FITNESS_CTRL_H__ */





