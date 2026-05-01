/*
 * Copyright (c) 2026, RT-Thread Development Team
 * SPDX-License-Identifier: Apache-2.0
 *
 * File   : key_task.c
 * Brief  : 按键扫描任务 — 5 状态 FSM 消抖 + 长按连发
 *
 * API 确认（drv_pin.c）：
 *   rt_pin_mode(pin, PIN_MODE_INPUT_PULLUP)   → 内部上拉输入
 *   rt_pin_read(pin)                          → 读电平 (0=低/按下)
 *
 * 引脚：SW3=GPIO0[6], SW2=GPIO1[7]（板载按键）
 *       MODE=GPIO3[3]（外接 Arduino D5）
 *       均为低电平有效，内部上拉
 */

#include <rtthread.h>
#include <rtdevice.h>
#include "fitness_ctrl.h"

#define TAG          "key"
#define SCAN_MS      10U
#define DEBOUNCE_CNT 3U
#define LONG_MS      800U
#define REPEAT_MS    200U

typedef enum {
    KS_IDLE=0, KS_CONFIRM, KS_PRESSED, KS_LONG, KS_RELEASE
} key_state_t;

typedef struct {
    rt_base_t          pin;
    struct rt_semaphore *sem;
    key_state_t        state;
    rt_uint8_t         dbcnt;
    rt_tick_t          press_tick;
    rt_tick_t          repeat_tick;
} key_ctx_t;

static key_ctx_t s_keys[3];

static void key_fsm(key_ctx_t *k)
{
    rt_bool_t active = (rt_pin_read(k->pin) == PIN_LOW);
    rt_tick_t now    = rt_tick_get();

    switch (k->state)
    {
    case KS_IDLE:
        if (active) { k->dbcnt = 1; k->state = KS_CONFIRM; }
        break;
    case KS_CONFIRM:
        if (active) {
            if (++k->dbcnt >= DEBOUNCE_CNT) {
                k->press_tick  = now;
                k->repeat_tick = now;
                k->state = KS_PRESSED;
                rt_sem_release(k->sem);
            }
        } else { k->dbcnt = 0; k->state = KS_IDLE; }
        break;
    case KS_PRESSED:
        if (!active) { k->dbcnt = 0; k->state = KS_RELEASE; }
        else {
            rt_uint32_t held = (now - k->press_tick)*1000/RT_TICK_PER_SECOND;
            if (held >= LONG_MS) k->state = KS_LONG;
        }
        break;
    case KS_LONG:
        if (!active) { k->dbcnt = 0; k->state = KS_RELEASE; }
        else {
            rt_uint32_t rpt = (now - k->repeat_tick)*1000/RT_TICK_PER_SECOND;
            if (rpt >= REPEAT_MS) {
                k->repeat_tick = now;
                rt_sem_release(k->sem);
            }
        }
        break;
    case KS_RELEASE:
        if (!active) {
            if (++k->dbcnt >= DEBOUNCE_CNT) {
                k->dbcnt = 0; k->state = KS_IDLE;
            }
        } else { k->state = KS_PRESSED; }
        break;
    default: k->state = KS_IDLE; break;
    }
}

void key_task_entry(void *p)
{
    s_keys[0] = (key_ctx_t){PIN_KEY_UP,   &sem_key_up,   KS_IDLE};
    s_keys[1] = (key_ctx_t){PIN_KEY_DOWN, &sem_key_down, KS_IDLE};
    s_keys[2] = (key_ctx_t){PIN_KEY_MODE, &sem_key_mode, KS_IDLE};

    for (int i = 0; i < 3; i++)
        rt_pin_mode(s_keys[i].pin, PIN_MODE_INPUT_PULLUP);

    rt_kprintf("[%s] 3 keys ready: SW3(P0_6)=UP, SW2(P1_7)=DOWN, P3_3=MODE\n", TAG);

    while (1) {
        for (int i = 0; i < 3; i++)
            key_fsm(&s_keys[i]);
        rt_thread_mdelay(SCAN_MS);
    }
}


