/*
 * Copyright (c) 2026, RT-Thread Development Team
 * SPDX-License-Identifier: Apache-2.0
 *
 * File   : display_task.c
 * Brief  : 显示/上报任务 — RGB LED 强度指示 + UART JSON 数据上报
 *
 * API 确认（drv_pin.c）：
 *   rt_pin_mode(pin, PIN_MODE_OUTPUT)     → 设置为输出
 *   rt_pin_write(pin, PIN_HIGH/PIN_LOW)   → 写电平
 *
 * LED 低电平点亮（active-low），对应 board schematic
 * 引脚：GET_PINS(3,12/13/14) = GPIO3 pin12/13/14
 */

#include <rtthread.h>
#include <rtdevice.h>
#include "fitness_ctrl.h"
#include <stdio.h>

#define TAG         "display"
#define PERIOD_MS   200U   /* 刷新周期 200 ms */

/* ─── LED 控制（低有效）──────────────────────────────────────────── */
#define LED_ON(pin)   rt_pin_write(pin, PIN_LOW)
#define LED_OFF(pin)  rt_pin_write(pin, PIN_HIGH)

typedef enum {
    LED_OFF_COLOR = 0, LED_BLUE, LED_GREEN, LED_YELLOW, LED_RED
} led_color_t;

static void set_led(led_color_t c)
{
    LED_OFF(PIN_LED_R); LED_OFF(PIN_LED_G); LED_OFF(PIN_LED_B);
    switch (c) {
    case LED_BLUE:   LED_ON(PIN_LED_B); break;
    case LED_GREEN:  LED_ON(PIN_LED_G); break;
    case LED_YELLOW: LED_ON(PIN_LED_R); LED_ON(PIN_LED_G); break;
    case LED_RED:    LED_ON(PIN_LED_R); break;
    default: break;
    }
}

static led_color_t rpm_to_color(rt_uint32_t rpm)
{
    if (rpm == 0)    return LED_OFF_COLOR;
    if (rpm <= 50)   return LED_BLUE;
    if (rpm <= 80)   return LED_GREEN;
    if (rpm <= 100)  return LED_YELLOW;
    return LED_RED;
}

/* ─── UART 上报（使用 rt_kprintf 复用 console 串口）────────────── */
static void uart_report(const fit_state_t *s)
{
    /*
     * 直接使用 rt_kprintf 输出到 uart0（console 设备）。
     * JSON 格式，上位机解析方便。
     * 若需要独立 UART，可改为 rt_device_find/write。
     */
    rt_kprintf("{\"rpm\":%u,\"spd_x10\":%u,\"lv\":%u,"
               "\"elapsed\":%u,\"cal_x10\":%u,\"dist_m\":%u,"
               "\"fb_mv\":%u,\"mode\":%u}\r\n",
               s->rpm,
               s->speed_km_h_x10,
               s->resistance_level,
               s->elapsed_s,
               s->calorie_x10,
               s->distance_m,
               s->resistance_fb_mv,
               (rt_uint32_t)s->mode);
}

/* ─── 任务入口 ──────────────────────────────────────────────────── */
void display_task_entry(void *p)
{
    /* 配置 RGB LED 引脚为输出 */
    rt_pin_mode(PIN_LED_R, PIN_MODE_OUTPUT);
    rt_pin_mode(PIN_LED_G, PIN_MODE_OUTPUT);
    rt_pin_mode(PIN_LED_B, PIN_MODE_OUTPUT);
    LED_OFF(PIN_LED_R); LED_OFF(PIN_LED_G); LED_OFF(PIN_LED_B);

    rt_kprintf("[%s] LED ready. Pins: R=GPIO3_12 G=GPIO3_13 B=GPIO3_14\n", TAG);

    /* 上电蓝灯闪 3 次：系统就绪 */
    for (int i = 0; i < 3; i++) {
        set_led(LED_BLUE);
        rt_thread_mdelay(150);
        set_led(LED_OFF_COLOR);
        rt_thread_mdelay(150);
    }

    fit_state_t snap;

    while (1)
    {
        /* 拍快照（减少锁持有时间） */
        rt_mutex_take(&mtx_state, RT_WAITING_FOREVER);
        rt_memcpy(&snap, &g_fit, sizeof(snap));
        rt_mutex_release(&mtx_state);

        /* 更新 LED */
        set_led(rpm_to_color(snap.rpm));

        /* UART 上报 */
        uart_report(&snap);

        rt_thread_mdelay(PERIOD_MS);
    }
}
