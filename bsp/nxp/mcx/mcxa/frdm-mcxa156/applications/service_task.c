/*
 * Copyright (c) 2026, RT-Thread Development Team
 * SPDX-License-Identifier: Apache-2.0
 *
 * File   : service_task.c
 * Brief  : 运动服务任务 — PWM 阻力控制 + 运动模式管理
 *
 * API 确认（drv_pwm.c）：
 *   rt_device_find("pwm0")      → struct rt_device_pwm *
 *   rt_pwm_enable(dev, ch)      → 启动 PWM 输出
 *   rt_pwm_set(dev, ch, period, pulse) → period/pulse 单位 ns
 *   rt_pwm_disable(dev, ch)     → 停止输出
 *
 * 注意：drv_pwm.c 内 mcx_drv_pwm_set 计算 dutyCyclePercent = pulse*100/period
 *       因此直接传 period_ns / pulse_ns 即可。
 */

#include <rtthread.h>
#include <rtdevice.h>
#include "fitness_ctrl.h"

#define TAG  "service"

/* 间歇训练程序（引用 main.c 中定义） */
extern const struct { rt_uint8_t level; rt_uint32_t duration_s; } g_interval_prog[];
extern const rt_uint32_t g_interval_steps;

static struct rt_device_pwm *s_pwm = RT_NULL;

/* 将等级映射到 PWM 并立即写入 */
static void apply_resistance(rt_uint8_t level)
{
    if (level < RESISTANCE_LV_MIN) level = RESISTANCE_LV_MIN;
    if (level > RESISTANCE_LV_MAX) level = RESISTANCE_LV_MAX;

    rt_uint32_t pulse = level_to_pulse_ns(level);

    if (s_pwm != RT_NULL)
        rt_pwm_set(s_pwm, PWM_CHANNEL, PWM_PERIOD_NS, pulse);

    rt_mutex_take(&mtx_state, RT_WAITING_FOREVER);
    g_fit.resistance_level = level;
    g_fit.pwm_pulse_ns     = pulse;
    rt_mutex_release(&mtx_state);

    /* 通知 LED 任务：阻力变化白色闪烁 */
    led_flash_request(3);
}

void service_task_entry(void *p)
{
    /* ── 查找并初始化 PWM ───────────────────────────────────────── */
    s_pwm = (struct rt_device_pwm *)rt_device_find(FITNESS_PWM_DEV);
    if (s_pwm == RT_NULL) {
        rt_kprintf("[%s] ERROR: device '%s' not found! "
                   "Check BSP_USING_PWM0 in Kconfig.\n", TAG, FITNESS_PWM_DEV);
        return;
    }

    rt_pwm_enable(s_pwm, PWM_CHANNEL);
    apply_resistance(g_fit.resistance_level);   /* 初始阻力等级 4 */

    rt_kprintf("[%s] PWM ready. '%s' CH%u, period=%u ns.\n",
               TAG, FITNESS_PWM_DEV, PWM_CHANNEL, PWM_PERIOD_NS);

    /* 间歇训练内部状态 */
    rt_uint32_t itvl_step = 0;
    rt_uint32_t itvl_sec  = 0;

    rt_ubase_t rpm_val = 0;

    while (1)
    {
        /* 等待 sensor_task 发来的 RPM（最多 2s 超时） */
        rt_err_t r = rt_mb_recv(&mb_rpm, &rpm_val,
                                rt_tick_from_millisecond(2000));
        if (r != RT_EOK) rpm_val = 0;   /* 超时→停止状态 */

        /* ── 读当前状态 ───────────────────────────────────────── */
        rt_mutex_take(&mtx_state, RT_WAITING_FOREVER);
        rt_uint8_t   cur_lv   = g_fit.resistance_level;
        fit_mode_t   cur_mode = g_fit.mode;
        rt_uint32_t  elapsed  = g_fit.elapsed_s;
        rt_mutex_release(&mtx_state);

        /* ── 检查模式切换信号量 ──────────────────────────────── */
        if (rt_sem_trytake(&sem_key_mode) == RT_EOK) {
            fit_mode_t next = (fit_mode_t)((cur_mode + 1) % FIT_MODE_MAX);
            rt_mutex_take(&mtx_state, RT_WAITING_FOREVER);
            g_fit.mode = next;
            rt_mutex_release(&mtx_state);
            itvl_step = 0;
            itvl_sec  = 0;
            cur_mode  = next;
            rt_kprintf("[%s] mode -> %u\n", TAG, (unsigned)next);
        }

        if (cur_mode == FIT_MODE_FREE || cur_mode == FIT_MODE_PROGRAM)
        {
            /* 手动按键调整阻力 */
            if (rt_sem_trytake(&sem_key_up)   == RT_EOK && cur_lv < RESISTANCE_LV_MAX)
                apply_resistance(cur_lv + 1);
            if (rt_sem_trytake(&sem_key_down) == RT_EOK && cur_lv > RESISTANCE_LV_MIN)
                apply_resistance(cur_lv - 1);
        }
        else  /* FIT_MODE_INTERVAL */
        {
            /* 丢弃手动按键 */
            rt_sem_trytake(&sem_key_up);
            rt_sem_trytake(&sem_key_down);

            /* 每秒推进间歇步骤 */
            static rt_uint32_t last_elapsed = 0;
            if (elapsed != last_elapsed) {
                itvl_sec++;
                last_elapsed = elapsed;
                if (itvl_sec >= g_interval_prog[itvl_step].duration_s) {
                    itvl_sec  = 0;
                    itvl_step = (itvl_step + 1) % g_interval_steps;
                    apply_resistance(g_interval_prog[itvl_step].level);
                    rt_kprintf("[%s] interval step %u -> lv%u\n",
                               TAG, (unsigned)itvl_step,
                               g_interval_prog[itvl_step].level);
                }
            }
        }

        /* 若状态被 FinSH 命令修改（fit_level），同步到 PWM */
        rt_mutex_take(&mtx_state, RT_WAITING_FOREVER);
        rt_uint32_t wanted_pulse = level_to_pulse_ns(g_fit.resistance_level);
        rt_bool_t need_update = (wanted_pulse != g_fit.pwm_pulse_ns);
        rt_mutex_release(&mtx_state);

        if (need_update && s_pwm)
        {
            rt_mutex_take(&mtx_state, RT_WAITING_FOREVER);
            rt_uint8_t lv = g_fit.resistance_level;
            rt_mutex_release(&mtx_state);
            apply_resistance(lv);
        }

        rt_thread_mdelay(50);
    }
}

