/*
 * Copyright (c) 2026, RT-Thread Development Team
 * SPDX-License-Identifier: Apache-2.0
 *
 * File   : led_task.c
 * Brief  : LED 指示灯任务
 *
 * 使用 FRDM-MCXA156 板载 RGB LED (GPIO3_12/13/14) 指示系统状态：
 *
 *   待机 (未旋转)    → 蓝色慢闪 (1 Hz)
 *   运动中 (旋转)    → 绿色常亮，亮度随速度变化
 *   阻力变化反馈     → 白色闪烁 3 次
 *   看门狗异常       → 红色快闪 (4 Hz)
 *   间歇训练高峰     → 红色呼吸
 *   间歇训练低谷     → 绿色呼吸
 *
 * API：
 *   rt_pin_write(pin, value)   → PIN_HIGH / PIN_LOW
 *     低电平点亮（LED 共阳接法），PIN_LOW = 亮, PIN_HIGH = 灭
 */

#include <rtthread.h>
#include <rtdevice.h>
#include "fitness_ctrl.h"
#include "drv_pin.h"

#define TAG             "led"
#define LED_CYCLE_MS    50U     /* LED 刷新周期 50 ms */

/* LED 亮灭（共阳：低电平点亮） */
#define LED_ON(pin)     rt_pin_write(pin, PIN_LOW)
#define LED_OFF(pin)    rt_pin_write(pin, PIN_HIGH)

/* ─── 内部状态 ──────────────────────────────────────────────────────── */
static rt_uint8_t  s_flash_cnt;            /* 闪烁剩余次数 */
static rt_bool_t   s_wdt_warn;             /* 看门狗告警 */

/* 公共接口：请求白色闪烁 N 次（其他任务调用） */
void led_flash_request(rt_uint8_t times)
{
    s_flash_cnt = times * 2;  /* ON+OFF = 1次闪烁 */
}

/* 公共接口：设置看门狗告警 */
void led_set_wdt_warn(rt_bool_t warn)
{
    s_wdt_warn = warn;
}

/* ─── 辅助：设置 RGB 颜色 ───────────────────────────────────────────── */
static void led_set_rgb(rt_bool_t r, rt_bool_t g, rt_bool_t b)
{
    if (r) LED_ON(PIN_LED_R); else LED_OFF(PIN_LED_R);
    if (g) LED_ON(PIN_LED_G); else LED_OFF(PIN_LED_G);
    if (b) LED_ON(PIN_LED_B); else LED_OFF(PIN_LED_B);
}

static void led_all_off(void)
{
    LED_OFF(PIN_LED_R);
    LED_OFF(PIN_LED_G);
    LED_OFF(PIN_LED_B);
}

/* ─── 辅助：简易呼吸亮度（正弦近似，周期 ~1 s @ 50 ms 步进）────────── */
static rt_bool_t breath_value(rt_uint32_t tick)
{
    /* tick 0~19 映射到 0→1→0 的呼吸曲线 */
    rt_uint32_t phase = tick % 20;
    if (phase < 10)
        return (phase < 5) ? RT_TRUE : RT_FALSE;   /* 简化：前半亮后半灭 */
    else
        return (phase < 15) ? RT_TRUE : RT_FALSE;
}

/* ─── 任务入口 ──────────────────────────────────────────────────────── */
void led_task_entry(void *p)
{
    /* 初始化 LED 引脚为输出，默认全灭 */
    rt_pin_mode(PIN_LED_R, PIN_MODE_OUTPUT);
    rt_pin_mode(PIN_LED_G, PIN_MODE_OUTPUT);
    rt_pin_mode(PIN_LED_B, PIN_MODE_OUTPUT);
    led_all_off();

    rt_kprintf("[%s] RGB LED task started.\n", TAG);

    rt_uint32_t cycle = 0;   /* 周期计数器 */

    while (1)
    {
        rt_thread_mdelay(LED_CYCLE_MS);
        cycle++;

        /* ── 优先级1：白色闪烁（阻力变化反馈）────────────────── */
        if (s_flash_cnt > 0)
        {
            s_flash_cnt--;
            if (s_flash_cnt & 1)
                led_set_rgb(1, 1, 1);   /* 白色 ON */
            else
                led_all_off();
            continue;
        }

        /* ── 优先级2：看门狗告警 → 红色快闪 ──────────────────── */
        if (s_wdt_warn)
        {
            rt_bool_t on = (cycle % 10) < 5;   /* ~4 Hz 快闪 */
            if (on) LED_ON(PIN_LED_R); else LED_OFF(PIN_LED_R);
            LED_OFF(PIN_LED_G);
            LED_OFF(PIN_LED_B);
            continue;
        }

        /* ── 读取全局状态 ─────────────────────────────────────── */
        rt_mutex_take(&mtx_state, RT_WAITING_FOREVER);
        rt_bool_t   rotating = g_fit.is_rotating;
        rt_uint32_t rpm      = g_fit.rpm;
        fit_mode_t  mode     = g_fit.mode;
        rt_uint8_t  level    = g_fit.resistance_level;
        rt_mutex_release(&mtx_state);

        if (!rotating)
        {
            /* ── 待机：蓝色慢闪 ~1 Hz ─────────────────────────── */
            rt_bool_t on = (cycle % 20) < 10;
            if (on) LED_ON(PIN_LED_B); else LED_OFF(PIN_LED_B);
            LED_OFF(PIN_LED_R);
            LED_OFF(PIN_LED_G);
        }
        else if (mode == FIT_MODE_INTERVAL)
        {
            /* ── 间歇训练：高峰红色呼吸 / 低谷绿色呼吸 ───────── */
            rt_bool_t breath = breath_value(cycle);
            if (level >= 5)
            {
                /* 高阻力 → 红色呼吸 */
                if (breath) LED_ON(PIN_LED_R); else LED_OFF(PIN_LED_R);
                LED_OFF(PIN_LED_G);
                LED_OFF(PIN_LED_B);
            }
            else
            {
                /* 低阻力 → 绿色呼吸 */
                if (breath) LED_ON(PIN_LED_G); else LED_OFF(PIN_LED_G);
                LED_OFF(PIN_LED_R);
                LED_OFF(PIN_LED_B);
            }
        }
        else
        {
            /* ── 运动中：绿色常亮，速度越快蓝色叠加 ───────────── */
            LED_OFF(PIN_LED_R);

            /* 绿色常亮 */
            LED_ON(PIN_LED_G);

            /* 蓝色：速度 > 60 rpm 亮，> 100 rpm 闪烁 */
            if (rpm > 100)
            {
                rt_bool_t blink = (cycle % 6) < 3;
                if (blink) LED_ON(PIN_LED_B); else LED_OFF(PIN_LED_B);
            }
            else if (rpm > 60)
            {
                LED_ON(PIN_LED_B);
            }
            else
            {
                LED_OFF(PIN_LED_B);
            }
        }
    }
}
