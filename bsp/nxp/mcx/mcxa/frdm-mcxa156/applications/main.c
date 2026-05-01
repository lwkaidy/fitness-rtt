/*
 * Copyright (c) 2026, RT-Thread Development Team
 * SPDX-License-Identifier: Apache-2.0
 *
 * File   : main.c
 * Brief  : 健身器材控制器 — 系统主入口
 *          基于 FRDM-MCXA156 + RT-Thread v5.2.1
 *
 * 功能：
 *   · 初始化全局 IPC 对象（mailbox / semaphore / mutex）
 *   · 创建并启动 5 个任务
 *   · 注册 FinSH 调试命令
 *
 * 使用的 BSP 驱动（均已在 Libraries/drivers 中实现）：
 *   drv_adc.c  → rt_adc_device    (BSP_USING_ADC0)
 *   drv_pwm.c  → rt_device_pwm   (BSP_USING_PWM0)
 *   drv_pin.c  → rt_pin_device   (RT_USING_PIN)
 *   drv_wdt.c  → rt_watchdog_t   (RT_USING_WDT)
 *   drv_uart.c → serial           (RT_USING_SERIAL_V1)
 */

#include <rtthread.h>
#include <rtdevice.h>
#include <stdlib.h>
#include "drv_pin.h"
#include "fitness_ctrl.h"
#define LED_PIN        ((3*32)+12)
/* RT-Thread version string for display */
#define STRINGIFY(x) #x
#define TOSTRING(x) STRINGIFY(x)
#define RT_VERSION_STRING  TOSTRING(RT_VERSION_MAJOR) "." \
                          TOSTRING(RT_VERSION_MINOR) "." \
                          TOSTRING(RT_VERSION_PATCH)

/* ─── 外部任务入口 ─────────────────────────────────────────────────── */
extern void sensor_task_entry(void *p);
extern void service_task_entry(void *p);
extern void display_task_entry(void *p);
extern void key_task_entry(void *p);
extern void wdt_task_entry(void *p);
extern void led_task_entry(void *p);

/* ─── 全局 IPC / 状态定义 ──────────────────────────────────────────── */
struct rt_mailbox  mb_rpm;
struct rt_semaphore sem_key_up;
struct rt_semaphore sem_key_down;
struct rt_semaphore sem_key_mode;
struct rt_mutex     mtx_state;
fit_state_t         g_fit = {0};

static rt_ubase_t mb_rpm_pool[8];  /* mailbox 消息池 */

/* ─── 间歇训练程序（service_task 使用）──────────────────────────────── */
typedef struct { rt_uint8_t level; rt_uint32_t duration_s; } itvl_t;
const itvl_t g_interval_prog[] = {
    {2, 120}, {5, 60}, {7, 30}, {4, 60},
    {6, 60},  {8, 30}, {3, 120}
};
const rt_uint32_t g_interval_steps =
    sizeof(g_interval_prog) / sizeof(g_interval_prog[0]);

/* ─── main ─────────────────────────────────────────────────────────── */
int main(void)
{
    rt_kprintf("\n");
    rt_kprintf("╔══════════════════════════════════════════╗\n");
    rt_kprintf("║  FRDM-MCXA156 Fitness Controller v1.0   ║\n");
    rt_kprintf("║  RT-Thread %-12s                 ║\n", RT_VERSION_STRING);
    rt_kprintf("║  Build: %-32s ║\n", __DATE__ " " __TIME__);
    rt_kprintf("╚══════════════════════════════════════════╝\n\n");

    /* ── 初始化 IPC ─────────────────────────────────────────────── */
    rt_mb_init(&mb_rpm, "mb_rpm", mb_rpm_pool,
               sizeof(mb_rpm_pool)/sizeof(rt_ubase_t), RT_IPC_FLAG_FIFO);
    rt_sem_init(&sem_key_up,   "key_up",  0, RT_IPC_FLAG_FIFO);
    rt_sem_init(&sem_key_down, "key_dn",  0, RT_IPC_FLAG_FIFO);
    rt_sem_init(&sem_key_mode, "key_mod", 0, RT_IPC_FLAG_FIFO);
    rt_mutex_init(&mtx_state,  "st_mtx",  RT_IPC_FLAG_PRIO);

    /* ── 初始默认状态 ────────────────────────────────────────────── */
    g_fit.resistance_level = 4;
    g_fit.pwm_pulse_ns     = level_to_pulse_ns(4);
    g_fit.mode             = FIT_MODE_FREE;

    /* ── 创建任务（按优先级从高到低）───────────────────────────── */
    rt_thread_t tid;

    /* 1. 看门狗喂狗任务 — 最高优先级，保证器材安全 */
    tid = rt_thread_create("wdt",     wdt_task_entry,     RT_NULL,
                           STACK_WDT,     TASK_PRIO_WDT,     20);
    RT_ASSERT(tid != RT_NULL);
    rt_thread_startup(tid);

    /* 2. 按键扫描任务 */
    tid = rt_thread_create("key",     key_task_entry,     RT_NULL,
                           STACK_KEY,     TASK_PRIO_KEY,     20);
    RT_ASSERT(tid != RT_NULL);
    rt_thread_startup(tid);

    /* 3. 运动服务任务（算法 + 模式控制） */
    tid = rt_thread_create("svc",     service_task_entry, RT_NULL,
                           STACK_SVC,     TASK_PRIO_SVC,     20);
    RT_ASSERT(tid != RT_NULL);
    rt_thread_startup(tid);

    /* 4. 传感器采样任务 */
    tid = rt_thread_create("sensor",  sensor_task_entry,  RT_NULL,
                           STACK_SENSOR,  TASK_PRIO_SENSOR,  20);
    RT_ASSERT(tid != RT_NULL);
    rt_thread_startup(tid);

    /* 5. 显示/上报任务 */
    tid = rt_thread_create("display", display_task_entry, RT_NULL,
                           STACK_DISPLAY, TASK_PRIO_DISPLAY, 20);
    RT_ASSERT(tid != RT_NULL);
    rt_thread_startup(tid);

rt_pin_write(LED_PIN, PIN_HIGH); 

    /* 6. LED 指示灯任务 */
    tid = rt_thread_create("led",     led_task_entry,     RT_NULL,
                           STACK_LED,     TASK_PRIO_LED,     20);
    RT_ASSERT(tid != RT_NULL);
    rt_thread_startup(tid);

    rt_kprintf("[main] All tasks started (6 threads).\n");
    return RT_EOK;
		
//    rt_pin_mode(LED_PIN, PIN_MODE_OUTPUT);  /* Set GPIO as Output */
//    while (1)
//    {
//        rt_pin_write(LED_PIN, PIN_HIGH);    /* Set GPIO output 1 */
//        rt_thread_mdelay(333);               /* Delay 500mS */
//        rt_pin_write(LED_PIN, PIN_LOW);     /* Set GPIO output 0 */
//        rt_thread_mdelay(333);               /* Delay 500mS */
//    }
}

/* ─── FinSH 调试命令 ───────────────────────────────────────────────── */

static int cmd_fit_status(int argc, char **argv)
{
    rt_mutex_take(&mtx_state, RT_WAITING_FOREVER);
    fit_state_t s = g_fit;
    rt_mutex_release(&mtx_state);

    const char *modes[] = {"Free", "Interval", "Program"};
    rt_kprintf("──────────────────────────────────────────────\n");
    rt_kprintf("  RPM        : %-5u  Speed : %u.%u km/h\n",
               s.rpm, s.speed_km_h_x10/10, s.speed_km_h_x10%10);
    rt_kprintf("  Resistance : Level %u/%u  Pulse: %u ns\n",
               s.resistance_level, RESISTANCE_LV_MAX, s.pwm_pulse_ns);
    rt_kprintf("  Feedback   : %u mV\n", s.resistance_fb_mv);
    rt_kprintf("  Time       : %02u:%02u  Calories: %u.%u kcal\n",
               s.elapsed_s/60, s.elapsed_s%60,
               s.calorie_x10/10, s.calorie_x10%10);
    rt_kprintf("  Distance   : %u m  Total Rev: %u\n",
               s.distance_m, s.total_rev);
    rt_kprintf("  Mode       : %s  WDT feeds: %u\n",
               modes[s.mode], s.wdt_feed_cnt);
    rt_kprintf("──────────────────────────────────────────────\n");
    return 0;
}
MSH_CMD_EXPORT_ALIAS(cmd_fit_status, fit_status, Show fitness controller status);

static int cmd_fit_level(int argc, char **argv)
{
    if (argc != 2) { rt_kprintf("Usage: fit_level <1-8>\n"); return -1; }
    int lv = atoi(argv[1]);
    if (lv < 1 || lv > 8) { rt_kprintf("Level must be 1~8\n"); return -1; }

    /* 修改状态，service_task 下次循环会读取并更新 PWM */
    rt_mutex_take(&mtx_state, RT_WAITING_FOREVER);
    g_fit.resistance_level = (rt_uint8_t)lv;
    g_fit.pwm_pulse_ns     = level_to_pulse_ns((rt_uint8_t)lv);
    rt_mutex_release(&mtx_state);

    rt_kprintf("Resistance -> Level %d\n", lv);
    return 0;
}
MSH_CMD_EXPORT_ALIAS(cmd_fit_level, fit_level, Set resistance level 1-8);

static int cmd_fit_mode(int argc, char **argv)
{
    if (argc != 2) {
        rt_kprintf("Usage: fit_mode <0=free|1=interval|2=program>\n");
        return -1;
    }
    int m = atoi(argv[1]);
    if (m < 0 || m >= FIT_MODE_MAX) { rt_kprintf("Invalid mode\n"); return -1; }

    rt_mutex_take(&mtx_state, RT_WAITING_FOREVER);
    g_fit.mode = (fit_mode_t)m;
    rt_mutex_release(&mtx_state);

    const char *modes[] = {"Free", "Interval", "Program"};
    rt_kprintf("Mode -> %s\n", modes[m]);
    return 0;
}
MSH_CMD_EXPORT_ALIAS(cmd_fit_mode, fit_mode, Set workout mode 0/1/2);

static int cmd_fit_reset(int argc, char **argv)
{
    rt_mutex_take(&mtx_state, RT_WAITING_FOREVER);
    g_fit.elapsed_s   = 0;
    g_fit.total_rev   = 0;
    g_fit.calorie_x10 = 0;
    g_fit.distance_m  = 0;
    rt_mutex_release(&mtx_state);
    rt_kprintf("Stats reset.\n");
    return 0;
}
MSH_CMD_EXPORT_ALIAS(cmd_fit_reset, fit_reset, Reset workout statistics);



