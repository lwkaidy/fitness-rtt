/*
 * Copyright (c) 2026, RT-Thread Development Team
 * SPDX-License-Identifier: Apache-2.0
 *
 * File   : sensor_task.c
 * Brief  : 传感器采样任务 — 使用 RT-Thread ADC 设备框架
 *
 * API 确认（drv_adc.c）：
 *   rt_device_find("adc0")   → rt_adc_device_t
 *   rt_adc_enable(dev, ch)   → 使能通道，初始化 LPADC，进行校准
 *   rt_adc_read(dev, ch, &v) → 16-bit 软件触发采样 (高精度模式)
 *
 * 踏频采样原理：
 *   霍尔传感器输出 0~3.3V 模拟信号，ADC 以 20ms 采样。
 *   带迟滞的上升沿计数，每 1s 计算 RPM，低通滤波后写入 g_fit。
 *   同时通过 mailbox 通知 service_task。
 */

#include <rtthread.h>
#include <rtdevice.h>
#include "fitness_ctrl.h"

#define TAG          "sensor"
#define SAMPLE_MS    20U    /* ADC 采样周期 */
#define WINDOW_MS    1000U  /* RPM 计算窗口 */

void sensor_task_entry(void *p)
{
    /* ── 查找并使能 ADC ──────────────────────────────────────────── */
    rt_adc_device_t adc = (rt_adc_device_t)rt_device_find(FITNESS_ADC_DEV);
    if (adc == RT_NULL) {
        rt_kprintf("[%s] ERROR: device '%s' not found! "
                   "Check BSP_USING_ADC0 in Kconfig.\n", TAG, FITNESS_ADC_DEV);
        return;
    }

    /* 使能两路通道（drv_adc.c 内部会初始化 LPADC + 校准） */
    rt_adc_enable(adc, ADC_CH_CADENCE);
    rt_adc_enable(adc, ADC_CH_RESISTANCE);
    rt_kprintf("[%s] ADC ready. CH%d=cadence, CH%d=resistance.\n",
               TAG, ADC_CH_CADENCE, ADC_CH_RESISTANCE);

    rt_bool_t  in_pulse  = RT_FALSE;
    rt_uint32_t pulse_cnt = 0;
    rt_uint32_t total_rev = 0;
    rt_tick_t  t_start   = rt_tick_get();

    while (1)
    {
        /* ── 踏频通道采样 ──────────────────────────────────────── */
        rt_uint32_t raw_cad = rt_adc_read(adc, ADC_CH_CADENCE);

        /* 带迟滞的上升沿检测 */
        if (!in_pulse && raw_cad > HALL_THRESH_HIGH) {
            pulse_cnt++;
            total_rev++;
            in_pulse = RT_TRUE;
        } else if (in_pulse && raw_cad < HALL_THRESH_LOW) {
            in_pulse = RT_FALSE;
        }

        /* ── 阻力反馈通道采样 ────────────────────────────────── */
        rt_uint32_t raw_fb = rt_adc_read(adc, ADC_CH_RESISTANCE);
        rt_uint32_t fb_mv = raw_fb * ADC_VREF_MV / ADC_FULL_SCALE;

        /* ── 每 1s 计算 RPM，写入全局状态 ────────────────────── */
        rt_tick_t now = rt_tick_get();
        if ((now - t_start) >= rt_tick_from_millisecond(WINDOW_MS))
        {
            rt_uint32_t measured_rpm = pulse_cnt * 60U / MAGNETS_PER_REV;

            rt_mutex_take(&mtx_state, RT_WAITING_FOREVER);
            /* 一阶低通滤波 α=0.3 */
            g_fit.rpm      = (g_fit.rpm * 7U + measured_rpm * 3U) / 10U;
            g_fit.speed_km_h_x10 = rpm_to_speed_x10(g_fit.rpm);
            g_fit.is_rotating    = (g_fit.rpm > 0);
            g_fit.total_rev      = total_rev;
            g_fit.elapsed_s     += 1U;
            g_fit.resistance_fb_mv = fb_mv;
            /* 卡路里更新 */
            g_fit.calorie_x10 = calc_calorie_x10(
                g_fit.rpm, g_fit.resistance_level, g_fit.elapsed_s);
            g_fit.distance_m = g_fit.total_rev * 1700U / 1000U;
            rt_mutex_release(&mtx_state);

            /* 通知 service_task */
            rt_mb_send(&mb_rpm, (rt_ubase_t)g_fit.rpm);

            pulse_cnt = 0;
            t_start   = now;
        }

        rt_thread_mdelay(SAMPLE_MS);
    }
}

