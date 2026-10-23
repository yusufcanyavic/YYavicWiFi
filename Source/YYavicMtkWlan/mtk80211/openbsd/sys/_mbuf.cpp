/*
 * Reworked by Yusufcan Yavic, 2026, for YYavicWiFi (native MT7922 Wi-Fi on macOS).
 * Starting point: the SimpleMtkWlan version of this file by laobamac.
 * Added and changed by Yusufcan Yavic: RX hook for the native data path.
 * See CHANGELOG.md. The original notices below remain in force; these
 * modifications are distributed under the same license as this file.
 */
//
//  _mbuf.cpp
//  YYavicWiFi
//
//  Originally written by laobamac (王孝慈) on 2026/4/21 for SimpleMtkWlan.
//  Copyright © 2026 laobamac (王孝慈). All rights reserved.
//

//
//  _mbuf.cpp
//  SimpleMtkWlan
//
//  Created by qcwap on 2020/6/14.
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

#include <sys/_mbuf.h>
extern "C" {
#include <net/bpf.h>
}
#include <IOKit/IOCommandGate.h>

extern IOCommandGate *_fCommandGate;

struct network_header {
    char pad[0x48];
};

/* YYavic radio bridge (yy_radio.h, ABI 3): data hook, set on the command gate. */
extern "C" {
void (*yy_rx_hook_fn)(void *ctx, void *m) = NULL;
void *yy_rx_hook_ctx = NULL;
volatile uint32_t yy_rx_hook_frames = 0;    /* frames handed to the hook (dbg 29) */
}

static IOReturn _if_input(OSObject *target, void *arg0, void *arg1, void *arg2, void *arg3)
{
    mbuf_t m;
    bool isEmpty = true;
    struct _ifnet *ifq = (struct _ifnet *)arg0;
    struct mbuf_list *ml = (struct mbuf_list *)arg1;

    /* Native layer attached: it owns every received frame. */
    if (yy_rx_hook_fn != NULL) {
        while ((m = ml_dequeue(ml)) != NULL) {
            yy_rx_hook_frames++;
            yy_rx_hook_fn(yy_rx_hook_ctx, m);
        }
        return kIOReturnSuccess;
    }
    
    /* Detach before transferring ownership. IOKit requires nextpkt == NULL,
     * and the driver must not walk a packet after inputPacket consumes it. */
    while ((m = ml_dequeue(ml)) != NULL) {
        if (ifq->iface == NULL) {
            panic("%s ifq->iface == NULL!!!\n", __FUNCTION__);
            break;
        }
        if (m == NULL) {
            XYLog("%s m == NULL!!!\n", __FUNCTION__);
            continue;
        }
        //        XYLog("%s %d 啊啊啊啊 ifq->iface->inputPacket(m) hdr_len=%d len=%d\n", __FUNCTION__, __LINE__, mbuf_pkthdr_len(m), mbuf_len(m));
        isEmpty = false;
        ifq->iface->inputPacket(m, 0, IONetworkInterface::kInputOptionQueuePacket);
        if (ifq->netStat != NULL) {
            ifq->netStat->inputPackets++;
        }
    }
    if (!isEmpty) {
        ifq->iface->flushInputQueue();
    }
    return kIOReturnSuccess;
}

int if_input(struct _ifnet *ifq, struct mbuf_list *ml)
{
    return _fCommandGate->runAction((IOCommandGate::Action)_if_input, ifq, ml);
}

