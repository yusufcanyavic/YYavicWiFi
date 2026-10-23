/*
 * Reworked by Yusufcan Yavic, 2026, for YYavicWiFi (native MT7922 Wi-Fi on macOS).
 * Starting point: the SimpleMtkWlan version of this file by laobamac.
 * Added and changed by Yusufcan Yavic: tasks run on the driver work loop.
 * See CHANGELOG.md. The original notices below remain in force; these
 * modifications are distributed under the same license as this file.
 */
//
//  _task.cpp
//  YYavicWiFi
//
//  Originally written by laobamac (王孝慈) on 2026/4/15 for SimpleMtkWlan.
//  Copyright © 2026 laobamac (王孝慈). All rights reserved.
//

//
//  _task.cpp
//  SimpleMtkWlan
//
//  Created by qcwap on 2020/3/1.
//  Copyright © 2020 钟先耀. All rights reserved.
//

/*
* Copyright (C) 2020  钟先耀
*
* This program is free software; you can redistribute it and/or modify
* it under the terms of the GNU General Public License as published by
* the Free Software Foundation; either version 2 of the License, or
* (at your option) any later version.
*
* This program is distributed in the hope that it will be useful,
* but WITHOUT ANY WARRANTY; without even the implied warranty of
* MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
* GNU General Public License for more details.
*/

#include <sys/_task.h>
#include <IOKit/IOLib.h>
#include <IOKit/IOWorkLoop.h>
#include <IOKit/IOTimerEventSource.h>
#include <kern/thread_call.h>

/*
 * R9T: net80211 (ic_tree vb.) splnet() ile korunmuyor (no-op). Gorevleri (end_scan, newstate...) thread_call
 * iplikleri yerine RX/kesme ile AYNI workloop iplikte calistir; boylece agac erisimleri seri olur
 * (itlwm tasarimi). trace8-9 panigi: ieee80211_node_choose_bss -> double free.
 */
extern IOWorkLoop *_fWorkloop;
extern "C" void smtw_trace(const char *fmt, ...);
volatile thread_t gMwxWlThread;   /* workloop ipligi: zamanlayici islevleri tarafindan kaydedilir */

/* The HAL owns each queue. Quiesce before destroying callback arguments.
 * No producer may use a queue after taskq_destroy().
 */
struct taskq {
    IORecursiveLock *lock;
    thread_call_t call;
    IOTimerEventSource *wl_timer;
    struct task_list work;
    bool stopping;
};
static struct taskq system_queue;
struct taskq *const systq = &system_queue;

static void taskq_run(thread_call_param_t arg, thread_call_param_t)
{
    struct taskq *q = (struct taskq *)arg;
    for (;;) {
        IORecursiveLockLock(q->lock);
        struct task *item = TAILQ_FIRST(&q->work);
        if (q->stopping || item == NULL) {
            IORecursiveLockUnlock(q->lock);
            return;
        }
        TAILQ_REMOVE(&q->work, item, t_entry);
        item->t_flags &= ~TASK_ONQUEUE;
        struct task work = *item;
        IORecursiveLockUnlock(q->lock);
        work.t_func(work.t_arg);
    }
}

static struct taskq *gWlQueues[8];
static IOTimerEventSource *gWlTimers[8];
static int gWlCount;

static void taskq_wl_fire(OSObject *, IOTimerEventSource *t)
{
    gMwxWlThread = current_thread();
    for (int i = 0; i < gWlCount; i++)
        if (gWlTimers[i] == t) { taskq_run((thread_call_param_t)gWlQueues[i], NULL); return; }
}

static bool taskq_setup(struct taskq *q)
{
    q->lock = IORecursiveLockAlloc();
    if (!q->lock) return false;
    TAILQ_INIT(&q->work);
    q->stopping = false;
    q->wl_timer = NULL;
    if (_fWorkloop && gWlCount < 8) {
        IOTimerEventSource *t = IOTimerEventSource::timerEventSource(_fWorkloop, taskq_wl_fire);
        if (t && _fWorkloop->addEventSource(t) == kIOReturnSuccess) {
            q->wl_timer = t;
            gWlQueues[gWlCount] = q;
            gWlTimers[gWlCount] = t;
            gWlCount++;
            q->call = NULL;
            smtw_trace("taskq: workloop zamanlayicisi kuruldu (kuyruk %d)\n", gWlCount);
            return true;
        }
        if (t) t->release();
    }
    smtw_trace("taskq: UYARI thread_call yedegi kullaniliyor (_fWorkloop=%p)\n", (void *)_fWorkloop);
    q->call = thread_call_allocate_with_options(taskq_run, q,
        THREAD_CALL_PRIORITY_KERNEL, THREAD_CALL_OPTIONS_ONCE);
    if (!q->call) {
        IORecursiveLockFree(q->lock);
        q->lock = NULL;
        return false;
    }
    return true;
}

bool taskq_init(void)
{
    /* A second live HAL must not reinitialize the global queue. */
    if (systq->lock != NULL) return false;
    return taskq_setup(systq);
}

struct taskq *taskq_create(const char *, unsigned int nthreads, int, unsigned int)
{
    if (nthreads != 1) return NULL;
    struct taskq *q = (struct taskq *)IOMalloc(sizeof(*q));
    if (!q) return NULL;
    bzero(q, sizeof(*q));
    if (!taskq_setup(q)) { IOFree(q, sizeof(*q)); return NULL; }
    return q;
}

void taskq_quiesce(struct taskq *q)
{
    if (!q || !q->lock) return;
    IORecursiveLockLock(q->lock);
    q->stopping = true;
    struct task *item;
    while ((item = TAILQ_FIRST(&q->work)) != NULL) {
        TAILQ_REMOVE(&q->work, item, t_entry);
        item->t_flags &= ~TASK_ONQUEUE;
    }
    IORecursiveLockUnlock(q->lock);
    /* task_add and resubmission are now rejected under the same lock. */
    if (q->wl_timer)
        q->wl_timer->cancelTimeout();
    else
        thread_call_cancel_wait(q->call);
}

void taskq_destroy(struct taskq *q)
{
    if (!q || !q->lock) return;
    taskq_quiesce(q);
    if (q->call) {
        thread_call_free(q->call);
        q->call = NULL;
    }
    if (q->wl_timer) {
        for (int i = 0; i < gWlCount; i++)
            if (gWlTimers[i] == q->wl_timer) { gWlTimers[i] = NULL; gWlQueues[i] = NULL; }
        if (_fWorkloop) _fWorkloop->removeEventSource(q->wl_timer);
        q->wl_timer->release();
        q->wl_timer = NULL;
    }
    IORecursiveLockFree(q->lock);
    q->lock = NULL;
    if (q != systq) IOFree(q, sizeof(*q));
}

void task_set(struct task *t, void (*fn)(void *), void *arg, const char *name)
{
    t->t_func = fn;
    t->t_arg = arg;
    t->t_flags = 0;
    strlcpy(t->name, name, sizeof(t->name));
}

int task_add(struct taskq *q, struct task *t)
{
    if (!q || !q->lock) return 0;
    IORecursiveLockLock(q->lock);
    if (q->stopping || (t->t_flags & TASK_ONQUEUE)) {
        IORecursiveLockUnlock(q->lock);
        return 0;
    }
    t->t_flags |= TASK_ONQUEUE;
    TAILQ_INSERT_TAIL(&q->work, t, t_entry);
    /* Submit while locked, so shutdown cannot miss a late submission. */
    if (q->wl_timer)
        q->wl_timer->setTimeoutUS(100);
    else
        thread_call_enter(q->call);
    IORecursiveLockUnlock(q->lock);
    return 1;
}

int task_del(struct taskq *q, struct task *t)
{
    if (!q || !q->lock) return 0;
    IORecursiveLockLock(q->lock);
    /* Validate membership rather than trusting a flag from another queue. */
    struct task *item;
    TAILQ_FOREACH(item, &q->work, t_entry) {
        if (item == t) {
            TAILQ_REMOVE(&q->work, t, t_entry);
            t->t_flags &= ~TASK_ONQUEUE;
            IORecursiveLockUnlock(q->lock);
            return 1;
        }
    }
    IORecursiveLockUnlock(q->lock);
    return 0;
}
