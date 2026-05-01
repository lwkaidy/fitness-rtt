/*
 * Copyright (c) 2026, RT-Thread Development Team
 * SPDX-License-Identifier: Apache-2.0
 *
 * File   : wdt_task.c
 * Brief  : 看门狗喂狗任务
 *
 * API 确认（drv_wdt.c）：
 *   rt_device_find("wdt")                        → rt_device_t
 *   rt_device_open(dev, RT_DEVICE_FLAG_DEACTIVED) → 打开设备
 *   rt_device_control(dev, RT_DEVICE_CTRL_WDT_SET_TIMEOUT, &timeout_s) → 设置超时
 *   rt_device_control(dev, RT_DEVICE_CTRL_WDT_START, NULL) → 启动
 *   rt_device_control(dev, RT_DEVICE_CTRL_WDT_KEEPALIVE, NULL) → 喂狗
 *
 * drv_wdt.c 中的 wdt_control 函数处理以上所有 cmd。
 * KEEPALIVE 内部调用 delayWwdtWindow() + WWDT_Refresh()。
 */

#include <rtthread.h>
#include <rtdevice.h>
#include "fitness_ctrl.h"

#define TAG          "wdt"
#define FEED_MS      2000U   /* 喂狗间隔 2 s（WDT 超时 4 s） */
#define TIMEOUT_S    4U      /* 与 drv_wdt.c 中默认值一致 */

void wdt_task_entry(void *p)
{
    rt_device_t wdt = rt_device_find(FITNESS_WDT_DEV);
    if (wdt == RT_NULL) {
        rt_kprintf("[%s] WARN: device 'wdt' not found, "
                   "check RT_USING_WDT in rtconfig.h!\n", TAG);
        /* 没有 WDT 时任务继续存活，只是不喂狗 */
        while (1) rt_thread_mdelay(10000);
    }

    rt_device_open(wdt, RT_DEVICE_FLAG_DEACTIVATE);

    rt_uint32_t timeout = TIMEOUT_S;
    rt_device_control(wdt, RT_DEVICE_CTRL_WDT_SET_TIMEOUT, &timeout);
    rt_device_control(wdt, RT_DEVICE_CTRL_WDT_START, RT_NULL);

    rt_kprintf("[%s] WWDT0 started. timeout=%us, feed_interval=%ums\n",
               TAG, TIMEOUT_S, FEED_MS);

    while (1)
    {
        rt_device_control(wdt, RT_DEVICE_CTRL_WDT_KEEPALIVE, RT_NULL);

        rt_mutex_take(&mtx_state, RT_WAITING_FOREVER);
        g_fit.wdt_feed_cnt++;
        rt_mutex_release(&mtx_state);

        rt_thread_mdelay(FEED_MS);
    }
}

