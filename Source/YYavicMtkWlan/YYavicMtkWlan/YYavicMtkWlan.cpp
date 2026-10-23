/*
 * Reworked by Yusufcan Yavic, 2026, for YYavicWiFi (native MT7922 Wi-Fi on macOS).
 * Starting point: the SimpleMtkWlan version of this file by laobamac.
 * Added and changed by Yusufcan Yavic: radio bridge for the native IO80211/Skywalk layer, asynchronous TX kick, scan/join/status/power calls, boot-args.
 * See CHANGELOG.md. The original notices below remain in force; these
 * modifications are distributed under the same license as this file.
 */
//
//  YYavicMtkWlan.cpp
//  YYavicWiFi
//
//  Originally written by laobamac (王孝慈) on 2026/3/18 for SimpleMtkWlan.
//  Copyright © 2026 laobamac (王孝慈). All rights reserved.
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
#include "YYavicMtkWlan.hpp"
#include <linux/types.h>
#include <linux/kernel.h>

#include <IOKit/IOInterruptController.h>
#include <IOKit/IOCommandGate.h>
#include <IOKit/IOInterruptEventSource.h>
#include <IOKit/network/IONetworkMedium.h>
/* YYavic radio bridge data hook (mtk80211/openbsd/sys/_mbuf.cpp) */
extern "C" { extern void (*yy_rx_hook_fn)(void *, void *); extern void *yy_rx_hook_ctx; }
extern "C" void mwx_yy_link_info(struct ieee80211com *ic, uint8_t *bss_bw, uint8_t *center, uint8_t *rx);
#include <net/ethernet.h>
#include <crypto/sha1.h>
#include <net80211/ieee80211_node.h>
#include <net80211/ieee80211_ioctl.h>

#include <kern/thread_call.h>
#include <stdarg.h>
#include <libkern/libkern.h>
static IOSimpleLock *gSmtwLock = NULL;
static IOService *gSmtwProv = NULL;
static char gSmtwHead[7168]; static size_t gSmtwHeadLen = 0;
static char gSmtwTail[24576]; static size_t gSmtwTailLen = 0;

extern "C" void smtw_trace(const char *fmt, ...)
{
    char line[200];
    va_list ap;
    va_start(ap, fmt);
    int n = vsnprintf(line, sizeof(line), fmt, ap);
    va_end(ap);
    if (n <= 0) return;
    if (n >= (int)sizeof(line)) n = (int)sizeof(line) - 1;
    IOLog("%s", line);
    if (!gSmtwLock) return;
    IOSimpleLockLock(gSmtwLock);
    if (gSmtwHeadLen + n < sizeof(gSmtwHead)) {
        memcpy(gSmtwHead + gSmtwHeadLen, line, n);
        gSmtwHeadLen += n;
        gSmtwHead[gSmtwHeadLen] = 0;
    }
    if (gSmtwTailLen + n >= sizeof(gSmtwTail)) {
        size_t drop = gSmtwTailLen + n - (sizeof(gSmtwTail) - 1);
        if (drop > gSmtwTailLen) drop = gSmtwTailLen;
        memmove(gSmtwTail, gSmtwTail + drop, gSmtwTailLen - drop);
        gSmtwTailLen -= drop;
    }
    memcpy(gSmtwTail + gSmtwTailLen, line, n);
    gSmtwTailLen += n;
    gSmtwTail[gSmtwTailLen] = 0;
    IOSimpleLockUnlock(gSmtwLock);
}

extern "C" void smtw_publish(void)
{
    if (!gSmtwLock || !gSmtwProv) return;
    char *a = (char *)IOMalloc(sizeof(gSmtwHead));
    char *b = (char *)IOMalloc(sizeof(gSmtwTail));
    if (!a || !b) { if (a) IOFree(a, sizeof(gSmtwHead)); if (b) IOFree(b, sizeof(gSmtwTail)); return; }
    IOSimpleLockLock(gSmtwLock);
    memcpy(a, gSmtwHead, gSmtwHeadLen + 1);
    memcpy(b, gSmtwTail, gSmtwTailLen + 1);
    IOSimpleLockUnlock(gSmtwLock);
    gSmtwProv->setProperty("smtw-trace", a);
    gSmtwProv->setProperty("smtw-last", b);
    IOFree(a, sizeof(gSmtwHead));
    IOFree(b, sizeof(gSmtwTail));
}


static thread_call_t gSmtwPubTc = NULL;
extern "C" uint32_t mwx_poll_fires(void);
static void smtw_pub_tick(thread_call_param_t, thread_call_param_t)
{
    static uint32_t hb_n, last_fires, last_state_n;
    uint32_t f = mwx_poll_fires();
    {
        /* R9T trace38: kalici loga (unified log) 10 sn'de bir kalp atisi - donma tanisi */
        static uint32_t hb10, last10;
        if (++hb10 % 10 == 0) {
            IOLog("YYavicMtkWlan: hb t=%us poll_fires=%u delta=%u\n", hb10, f, f - last10);
            last10 = f;
        }
    }
    if (f != 0 && hb_n < 60 && (++last_state_n % 2) == 0) {
        hb_n++;
        smtw_trace("HB #%u poll_fires=%u delta=%u\n", hb_n, f, f - last_fires);
        last_fires = f;
    }
    smtw_publish();
    uint64_t dl;
    clock_interval_to_deadline(1000, kMillisecondScale, &dl);
    thread_call_enter_delayed(gSmtwPubTc, dl);
}

#define super IOEthernetController
OSDefineMetaClassAndStructors(YYavicMtkWlan, IOEthernetController)
OSDefineMetaClassAndStructors(CTimeout, OSObject)

IOWorkLoop *_fWorkloop;
IOCommandGate *_fCommandGate;
/* YYavic 0.6.6 asynchronous TX kick (see yy_radio_tx_batch) */
static IOInterruptEventSource *gYYTxKick;
static struct _ifnet *gYYTxIfp;
uint64_t gYYTxKicks, gYYTxKickRuns;	/* mtkscan dbg 34 */

static void yy_tx_kick_action(OSObject *, IOInterruptEventSource *, int)
{
    gYYTxKickRuns++;
    if (gYYTxIfp != NULL)
        (*gYYTxIfp->if_start)(gYYTxIfp);
}

bool YYavicMtkWlan::init(OSDictionary *properties)
{
    return super::init(properties);
}

#define  PCI_MSI_FLAGS        2    /* Message Control */
#define  PCI_CAP_ID_MSI        0x05    /* Message Signalled Interrupts */
#define  PCI_MSIX_FLAGS        2    /* Message Control */
#define  PCI_CAP_ID_MSIX    0x11    /* MSI-X */
#define  PCI_MSIX_FLAGS_ENABLE    0x8000    /* MSI-X enable */
#define  PCI_MSI_FLAGS_ENABLE    0x0001    /* MSI feature enabled */

static void pciMsiSetEnable(IOPCIDevice *device, UInt8 msiCap, int enable)
{
    u16 control;
    
    control = device->configRead16(msiCap + PCI_MSI_FLAGS);
    control &= ~PCI_MSI_FLAGS_ENABLE;
    if (enable)
        control |= PCI_MSI_FLAGS_ENABLE;
    device->configWrite16(msiCap + PCI_MSI_FLAGS, control);
}

static void pciMsiXClearAndSet(IOPCIDevice *device, UInt8 msixCap, UInt16 clear, UInt16 set)
{
    u16 ctrl;
    
    ctrl = device->configRead16(msixCap + PCI_MSIX_FLAGS);
    ctrl &= ~clear;
    ctrl |= set;
    device->configWrite16(msixCap + PCI_MSIX_FLAGS, ctrl);
}

IOService* YYavicMtkWlan::probe(IOService *provider, SInt32 *score)
{
    bool isMatch = false;
    if (!gSmtwLock) gSmtwLock = IOSimpleLockAlloc();
    gSmtwProv = provider;
    smtw_trace("probe called provider=%p\n", (void *)provider);
    smtw_publish();
    super::probe(provider, score);
    UInt8 msiCap;
    UInt8 msixCap;
    IOPCIDevice* device = OSDynamicCast(IOPCIDevice, provider);
    if (!device) {
        smtw_trace("probe: not IOPCIDevice\n"); smtw_publish();
        return NULL;
    }
    if (MtkMwx::mwx_match(device)) {
        isMatch = true;
        fHalService = new MtkMwx;
    }
    if (isMatch) {
        device->findPCICapability(PCI_CAP_ID_MSIX, &msixCap);
        if (msixCap) {
            pciMsiXClearAndSet(device, msixCap, PCI_MSIX_FLAGS_ENABLE, 0);
        }
        device->findPCICapability(PCI_CAP_ID_MSI, &msiCap);
        /* YYavic: MSI is enabled by IOPCIFamily when the messaged interrupt
         * index is registered (compat pci_intr_establish); setting the enable
         * bit by hand left the address/data unprogrammed and no handler. */
        if (!msiCap && !msixCap) {
            XYLog("%s No MSI cap\n", __FUNCTION__);
            fHalService->release();
            fHalService = NULL;
            return NULL;
        }
        smtw_trace("probe: match OK\n"); smtw_publish();
        return this;
    }
    smtw_trace("probe: no match\n"); smtw_publish();
    return NULL;
}

bool YYavicMtkWlan::configureInterface(IONetworkInterface *netif)
{
    IONetworkData *nd;
    struct _ifnet *ifp = &fHalService->get80211Controller()->ic_ac.ac_if;
    
    if (super::configureInterface(netif) == false) {
        XYLog("super failed\n");
        return false;
    }
    
    nd = netif->getParameter(kIONetworkStatsKey);
    if (!nd || !(fpNetStats = (IONetworkStats *)nd->getBuffer())) {
        XYLog("network statistics buffer unavailable?\n");
        return false;
    }
    ifp->netStat = fpNetStats;
    ether_ifattach(ifp, OSDynamicCast(IOEthernetInterface, netif));
    fpNetStats->collisions = 0;
#ifdef __PRIVATE_SPI__
    // TXWI freelist and DMA rings are also accessed by interrupt events on
    // this work loop. The default pull thread is not serialized with them.
    if (netif->configureOutputPullModel(
            fHalService->getDriverInfo()->getTxQueueSize(),
            kIONetworkWorkLoopSynchronous, 0,
            IOEthernetInterface::kOutputPacketSchedulingModelNormal, 0)
            != kIOReturnSuccess)
        return false;
#endif
    
    return true;
}

struct _ifnet *YYavicMtkWlan::getIfp()
{
    return &fHalService->get80211Controller()->ic_ac.ac_if;
}

IOEthernetInterface *YYavicMtkWlan::getNetworkInterface()
{
    return getIfp()->iface;
}

bool YYavicMtkWlan::createMediumTables(const IONetworkMedium **primary)
{
    IONetworkMedium    *medium;
    
    OSDictionary *mediumDict = OSDictionary::withCapacity(1);
    if (mediumDict == NULL) {
        XYLog("Cannot allocate OSDictionary\n");
        return false;
    }
    
    medium = IONetworkMedium::medium(kIOMediumEthernetAuto, 100 * 1000000);
    IONetworkMedium::addMedium(mediumDict, medium);
    medium->release();
    if (primary) {
        *primary = medium;
    }
    
    bool result = publishMediumDictionary(mediumDict);
    if (!result) {
        XYLog("Cannot publish medium dictionary!\n");
    }

    mediumDict->release();
    return result;
}

void YYavicMtkWlan::joinSSID(const char *ssid_name, const char *ssid_pwd)
{
    struct ieee80211com *ic = fHalService->get80211Controller();
    
    if (strlen(ssid_pwd) == 0) {
        memset(&nwkey, 0, sizeof(ieee80211_nwkey));
        nwkey.i_wepon = IEEE80211_NWKEY_OPEN;
        nwkey.i_defkid = 0;
        memcpy(join.i_nwid, ssid_name, strlen(ssid_name));
        join.i_len = strlen(ssid_name);
        join.i_flags = IEEE80211_JOIN_NWKEY;
    } else {
        memset(&wpa, 0, sizeof(ieee80211_wpaparams));
        wpa.i_enabled = 1;
        wpa.i_ciphers = 0;
        wpa.i_groupcipher = 0;
        wpa.i_protos = IEEE80211_WPA_PROTO_WPA1 | IEEE80211_WPA_PROTO_WPA2;
        wpa.i_akms = IEEE80211_WPA_AKM_PSK | IEEE80211_WPA_AKM_8021X | IEEE80211_WPA_AKM_SHA256_PSK | IEEE80211_WPA_AKM_SHA256_8021X;
        memcpy(wpa.i_name, "smtkw", strlen("smtkw"));
        memset(&psk, 0, sizeof(ieee80211_wpapsk));
        memcpy(psk.i_name, "smtkw", strlen("smtkw"));
        psk.i_enabled = 1;
        pbkdf2_sha1(ssid_pwd, (const uint8_t*)ssid_name, strlen(ssid_name),
                    4096, psk.i_psk , 32);
        memset(&nwkey, 0, sizeof(ieee80211_nwkey));
        nwkey.i_wepon = 0;
        nwkey.i_defkid = 0;
        memset(&join, 0, sizeof(ieee80211_join));
        join.i_wpaparams = wpa;
        join.i_wpapsk = psk;
        join.i_flags = IEEE80211_JOIN_WPAPSK | IEEE80211_JOIN_ANY | IEEE80211_JOIN_WPA | IEEE80211_JOIN_8021X;
        join.i_nwkey = nwkey;
        join.i_len = strlen(ssid_name);
        memcpy(join.i_nwid, ssid_name, join.i_len);
    }
    if (ieee80211_add_ess(ic, &join) == 0)
        ic->ic_flags |= IEEE80211_F_AUTO_JOIN;
}

void YYavicMtkWlan::associateSSID(const char *ssid, const char *pwd)
{
    struct ieee80211com *ic = fHalService->get80211Controller();
    if (strlen(pwd) == 0) {
        memcpy(nwid.i_nwid, ssid, 32);
        nwid.i_len = strlen((char *)nwid.i_nwid);
        memset(ic->ic_des_essid, 0, IEEE80211_NWID_LEN);
        ic->ic_des_esslen = nwid.i_len;
        memcpy(ic->ic_des_essid, nwid.i_nwid, nwid.i_len);
        if (ic->ic_des_esslen > 0) {
            /* 'nwid' disables auto-join magic */
            ic->ic_flags &= ~IEEE80211_F_AUTO_JOIN;
        } else if (!TAILQ_EMPTY(&ic->ic_ess)) {
            /* '-nwid' re-enables auto-join */
            ic->ic_flags |= IEEE80211_F_AUTO_JOIN;
        }
        /* disable WPA/WEP */
        ieee80211_disable_rsn(ic);
        ieee80211_disable_wep(ic);
    } else {
        memset(&psk, 0, sizeof(psk));
        memcpy(nwid.i_nwid, ssid, 32);
        nwid.i_len = strlen((char *)nwid.i_nwid);
        memset(ic->ic_des_essid, 0, IEEE80211_NWID_LEN);
        ic->ic_des_esslen = nwid.i_len;
        memcpy(ic->ic_des_essid, nwid.i_nwid, nwid.i_len);
        if (ic->ic_des_esslen > 0) {
            /* 'nwid' disables auto-join magic */
            ic->ic_flags &= ~IEEE80211_F_AUTO_JOIN;
        } else if (!TAILQ_EMPTY(&ic->ic_ess)) {
            /* '-nwid' re-enables auto-join */
            ic->ic_flags |= IEEE80211_F_AUTO_JOIN;
        }
        /* disable WPA/WEP */
        ieee80211_disable_rsn(ic);
        ieee80211_disable_wep(ic);
        size_t passlen = strlen(pwd);
        /* Parse a WPA passphrase */
        if (passlen < 8 || passlen > 63)
            XYLog("wpakey: passphrase must be between "
                  "8 and 63 characters");
        if (nwid.i_len == 0)
            XYLog("wpakey: nwid not set");
        pbkdf2_sha1(pwd, (const uint8_t*)ssid, nwid.i_len, 4096,
                    psk.i_psk, 32);
        psk.i_enabled = 1;
        if (psk.i_enabled) {
            ic->ic_flags |= IEEE80211_F_PSK;
            memcpy(ic->ic_psk, psk.i_psk, sizeof(ic->ic_psk));
            if (ic->ic_flags & IEEE80211_F_WEPON)
                ieee80211_disable_wep(ic);
        } else {
            ic->ic_flags &= ~IEEE80211_F_PSK;
            memset(ic->ic_psk, 0, sizeof(ic->ic_psk));
        }
        memset(&wpa, 0, sizeof(wpa));
        ieee80211_ioctl_getwpaparms(ic, &wpa);
        wpa.i_enabled = psk.i_enabled;
        wpa.i_ciphers = 0;
        wpa.i_groupcipher = 0;
        wpa.i_protos = IEEE80211_WPA_PROTO_WPA1 | IEEE80211_WPA_PROTO_WPA2;
        wpa.i_akms = IEEE80211_WPA_AKM_PSK | IEEE80211_WPA_AKM_8021X | IEEE80211_WPA_AKM_SHA256_PSK | IEEE80211_WPA_AKM_SHA256_8021X;
        ieee80211_ioctl_setwpaparms(ic, &wpa);
    }
    if (ic->ic_state > IEEE80211_S_AUTH && ic->ic_bss != NULL)
        IEEE80211_SEND_MGMT(ic, ic->ic_bss, IEEE80211_FC0_SUBTYPE_DEAUTH, IEEE80211_REASON_AUTH_LEAVE);
    ieee80211_del_ess(ic, NULL, 0, 1);
    struct ieee80211_node *selbs = ieee80211_node_choose_bss(ic, 0, NULL);
    if (selbs == NULL) {
        if (ic->ic_state != IEEE80211_S_SCAN) {
            ieee80211_new_state(ic, IEEE80211_S_SCAN, -1);
        }
    } else {
        if (ic->ic_state > IEEE80211_S_AUTH) {
            ieee80211_node_join_bss(ic, selbs, 1);
            fHalService->getDriverController()->clearScanningFlags();
        } else {
            if (ic->ic_state != IEEE80211_S_SCAN) {
                ieee80211_new_state(ic, IEEE80211_S_SCAN, -1);
            }
        }
    }
}

bool YYavicMtkWlan::start(IOService *provider)
{
    int boot_value = 0;
    smtw_trace("start entered\n"); smtw_publish();
    if (!gSmtwPubTc) {
        gSmtwPubTc = thread_call_allocate(smtw_pub_tick, NULL);
        if (gSmtwPubTc) smtw_pub_tick(NULL, NULL);
    }
    if (!super::start(provider)) {
        { smtw_trace("start FAIL at line %d\n", __LINE__); smtw_publish(); return false; }
    }
    pciNub = OSDynamicCast(IOPCIDevice, provider);
    if (!pciNub) {
        { smtw_trace("start FAIL at line %d\n", __LINE__); smtw_publish(); return false; }
    }
    smtw_trace("PCI cfg before: cmd=0x%04x cap_ptr=0x%02x d40=0x%08x d44=0x%08x d48=0x%08x\n",
        pciNub->configRead16(0x04), pciNub->configRead8(0x34), pciNub->configRead32(0x40), pciNub->configRead32(0x44), pciNub->configRead32(0x48));
    pciNub->setBusMasterEnable(true);
    pciNub->setIOEnable(true);
    pciNub->setMemoryEnable(true);
    pciNub->configWrite8(0x41, 0);
    smtw_trace("PCI cfg after 0x41 write: cmd=0x%04x d40=0x%08x d44=0x%08x\n",
        pciNub->configRead16(0x04), pciNub->configRead32(0x40), pciNub->configRead32(0x44));
    {
        /* ASPM L0s/L1 kapat: MT7921/MT7922 bazi AMD platformlarinda L1 yuzunden donuyor (Linux mt7921e disable_aspm). */
        UInt8 expOff = 0;
        if (pciNub->findPCICapability(0x10 /* PCI Express */, &expOff) && expOff) {
            UInt16 lc = pciNub->configRead16(expOff + 0x10);
            smtw_trace("PCIe cap@0x%02x LnkCtl before=0x%04x LnkSta=0x%04x DevCtl=0x%04x\n",
                expOff, lc, pciNub->configRead16(expOff + 0x12), pciNub->configRead16(expOff + 0x08));
            pciNub->configWrite16(expOff + 0x10, lc & ~(UInt16)0x0003);
            smtw_trace("PCIe LnkCtl after=0x%04x (ASPM L0s/L1 off)\n", pciNub->configRead16(expOff + 0x10));
        } else {
            smtw_trace("PCIe capability not found\n");
        }
        IOSleep(100);   /* cihaz D0'a oturana kadar ilk MMIO oncesi bekle */
    }
    if (pciNub->requestPowerDomainState(kIOPMPowerOn,
                                        (IOPowerConnection *) getParentEntry(gIOPowerPlane), IOPMLowestState) != IOPMNoErr) {
        { smtw_trace("start FAIL at line %d\n", __LINE__); smtw_publish(); return false; }
    }
    if (initPCIPowerManagment(pciNub) == false) {
        super::stop(pciNub);
        { smtw_trace("start FAIL at line %d\n", __LINE__); smtw_publish(); return false; }
    }
    if (_fWorkloop == NULL) {
        XYLog("No _fWorkloop!!\n");
        super::stop(pciNub);
        releaseAll();
        { smtw_trace("start FAIL at line %d\n", __LINE__); smtw_publish(); return false; }
    }
    _fCommandGate = IOCommandGate::commandGate(this, (IOCommandGate::Action)YYavicMtkWlan::tsleepHandler);
    if (_fCommandGate == 0) {
        XYLog("No command gate!!\n");
        super::stop(pciNub);
        releaseAll();
        { smtw_trace("start FAIL at line %d\n", __LINE__); smtw_publish(); return false; }
    }
    _fWorkloop->addEventSource(_fCommandGate);
    gYYTxKick = IOInterruptEventSource::interruptEventSource(this,
        (IOInterruptEventSource::Action)yy_tx_kick_action);
    if (gYYTxKick != NULL && _fWorkloop->addEventSource(gYYTxKick) != kIOReturnSuccess)
        OSSafeReleaseNULL(gYYTxKick);
    XYLog("YYavic: async TX kick %s\n", gYYTxKick != NULL ? "on" : "OFF (gate fallback)");
    const IONetworkMedium *primaryMedium;
    if (!createMediumTables(&primaryMedium) ||
        !setCurrentMedium(primaryMedium) || !setSelectedMedium(primaryMedium)) {
        XYLog("setup medium fail\n");
        releaseAll();
        { smtw_trace("start FAIL at line %d\n", __LINE__); smtw_publish(); return false; }
    }
    fHalService->initWithController(this, _fWorkloop, _fCommandGate);
    
    if (PE_parse_boot_argn("-novht", &boot_value, sizeof(boot_value)))
        fHalService->get80211Controller()->ic_userflags |= IEEE80211_F_NOVHT;
    if (PE_parse_boot_argn("-noht40", &boot_value, sizeof(boot_value)))
        fHalService->get80211Controller()->ic_userflags |= IEEE80211_F_NOHT40;
    {
        extern uint8_t gYYNoHe;	/* hal_mwx/MtkMwx.cpp */
        if (PE_parse_boot_argn("-yyavicnohe", &boot_value, sizeof(boot_value)))
            gYYNoHe = 1;
    }
    
    if (!fHalService->attach(pciNub)) {
        XYLog("attach fail\n");
        super::stop(pciNub);
        releaseAll();
        { smtw_trace("start FAIL at line %d\n", __LINE__); smtw_publish(); return false; }
    }
    if (!attachInterface((IONetworkInterface **)&fNetIf, true)) {
        XYLog("attach to interface fail\n");
        fHalService->detach(pciNub);
        super::stop(pciNub);
        releaseAll();
        { smtw_trace("start FAIL at line %d\n", __LINE__); smtw_publish(); return false; }
    }
    fWatchdogWorkLoop = IOWorkLoop::workLoop();
    if (fWatchdogWorkLoop == NULL) {
        XYLog("init watchdog workloop fail\n");
        fHalService->detach(pciNub);
        super::stop(pciNub);
        releaseAll();
        { smtw_trace("start FAIL at line %d\n", __LINE__); smtw_publish(); return false; }
    }
    watchdogTimer = IOTimerEventSource::timerEventSource(this, OSMemberFunctionCast(IOTimerEventSource::Action, this, &YYavicMtkWlan::watchdogAction));
    if (!watchdogTimer) {
        XYLog("init watchdog fail\n");
        fHalService->detach(pciNub);
        super::stop(pciNub);
        releaseAll();
        { smtw_trace("start FAIL at line %d\n", __LINE__); smtw_publish(); return false; }
    }
    fWatchdogWorkLoop->addEventSource(watchdogTimer);
    setLinkStatus(kIONetworkLinkValid);
    OSObject *wifiEntryObject = NULL;
    OSDictionary *wifiEntry = NULL;
    OSString *entryKey = NULL;
    OSDictionary *wifiDict = OSDynamicCast(OSDictionary, getProperty("WiFiConfig"));
    if (wifiDict != NULL) {
        OSCollectionIterator *iterator = OSCollectionIterator::withCollection(wifiDict);
        while ((wifiEntryObject = iterator->getNextObject())) {
            entryKey = OSDynamicCast(OSString, wifiEntryObject);
            if (entryKey == NULL) {
                continue;
            }
            wifiEntry = OSDynamicCast(OSDictionary, wifiDict->getObject(entryKey));
            if (wifiEntry == NULL) {
                continue;
            }
            OSString *ssidObj = OSDynamicCast(OSString, wifiEntry->getObject("ssid"));
            OSString *pwdObj = OSDynamicCast(OSString, wifiEntry->getObject("password"));
            if (ssidObj == NULL || pwdObj == NULL || ssidObj->isEqualTo("")) {
                continue;
            }
            
            joinSSID(ssidObj->getCStringNoCopy(), pwdObj->getCStringNoCopy());
        }
        iterator->release();
    }
    if (TAILQ_EMPTY(&fHalService->get80211Controller()->ic_ess)) {
        fHalService->get80211Controller()->ic_flags |= IEEE80211_F_AUTO_JOIN;
    }
    registerService();
    fNetIf->registerService();
    smtw_trace("start OK, interface registered\n"); smtw_publish();
    return true;
}

void YYavicMtkWlan::watchdogAction(IOTimerEventSource *timer)
{
    smtw_publish();
    struct _ifnet *ifp = getIfp();
    (*ifp->if_watchdog)(ifp);
    watchdogTimer->setTimeoutMS(kWatchDogTimerPeriod);
}

const OSString * YYavicMtkWlan::newVendorString() const
{
    return OSString::withCString("Apple");
}

const OSString * YYavicMtkWlan::newModelString() const
{
    return OSString::withCString("MediaTek MT792x Wireless Card");
}

bool YYavicMtkWlan::initPCIPowerManagment(IOPCIDevice *provider)
{
    UInt16 reg16;

    reg16 = provider->configRead16(kIOPCIConfigCommand);

    reg16 |= ( kIOPCICommandBusMaster       |
               kIOPCICommandMemorySpace     |
               kIOPCICommandMemWrInvalidate );

    reg16 &= ~kIOPCICommandIOSpace;  // disable I/O space

    provider->configWrite16( kIOPCIConfigCommand, reg16 );
    provider->findPCICapability(kIOPCIPowerManagementCapability,
                                &pmPCICapPtr);
    if (pmPCICapPtr) {
        UInt16 pciPMCReg = provider->configRead32( pmPCICapPtr ) >> 16;
        if (pciPMCReg & kPCIPMCPMESupportFromD3Cold) {
            magicPacketSupported = true;
        }
        provider->configWrite16((pmPCICapPtr + 4), 0x8000 );
        IOSleep(10);
    }
    return true;
}

bool YYavicMtkWlan::createWorkLoop()
{
    _fWorkloop = IOWorkLoop::workLoop();
    return _fWorkloop != 0;
}

IOWorkLoop *YYavicMtkWlan::getWorkLoop() const
{
    return _fWorkloop;
}

IOReturn YYavicMtkWlan::selectMedium(const IONetworkMedium *medium) {
    setSelectedMedium(medium);
    return kIOReturnSuccess;
}

void YYavicMtkWlan::stop(IOService *provider)
{
    XYLog("%s\n", __FUNCTION__);
    if (fWatchdogWorkLoop && watchdogTimer) {
        watchdogTimer->cancelTimeout();
        watchdogTimer->disable();
        fWatchdogWorkLoop->removeEventSource(watchdogTimer);
        watchdogTimer->release();
        watchdogTimer = NULL;
        fWatchdogWorkLoop->release();
        fWatchdogWorkLoop = NULL;
    }
    struct _ifnet *ifp = &fHalService->get80211Controller()->ic_ac.ac_if;
    super::stop(provider);
    setLinkStatus(kIONetworkLinkValid);
    ether_ifdetach(ifp);
    fHalService->detach(pciNub);
    detachInterface(fNetIf, true);
    OSSafeReleaseNULL(fNetIf);
    releaseAll();
}

void YYavicMtkWlan::releaseAll()
{
    if (fHalService) {
        fHalService->release();
        fHalService = NULL;
    }
    if (_fWorkloop) {
        if (gYYTxKick) {
            gYYTxKick->disable();
            _fWorkloop->removeEventSource(gYYTxKick);
            OSSafeReleaseNULL(gYYTxKick);
        }
        if (_fCommandGate) {
//            _fCommandGate->disable();
            _fWorkloop->removeEventSource(_fCommandGate);
            _fCommandGate->release();
            _fCommandGate = NULL;
        }
        if (fWatchdogWorkLoop && watchdogTimer) {
            watchdogTimer->cancelTimeout();
            fWatchdogWorkLoop->removeEventSource(watchdogTimer);
            watchdogTimer->release();
            watchdogTimer = NULL;
            fWatchdogWorkLoop->release();
            fWatchdogWorkLoop = NULL;
        }
        _fWorkloop->release();
        _fWorkloop = NULL;
    }
    unregistPM();
}

void YYavicMtkWlan::free()
{
    XYLog("%s\n", __FUNCTION__);
    if (fHalService != NULL) {
        fHalService->release();
        fHalService = NULL;
    }
    super::free();
}

IOReturn YYavicMtkWlan::enable(IONetworkInterface *netif)
{
    XYLog("%s\n", __FUNCTION__);
    super::enable(netif);
    _fCommandGate->enable();
    fHalService->enable(netif);
    watchdogTimer->setTimeoutMS(kWatchDogTimerPeriod);
    watchdogTimer->enable();
    return kIOReturnSuccess;
}

IOReturn YYavicMtkWlan::disable(IONetworkInterface *netif)
{
    XYLog("%s\n", __FUNCTION__);
    super::disable(netif);
    watchdogTimer->cancelTimeout();
    watchdogTimer->disable();
    fHalService->disable(netif);
    setLinkStatus(kIONetworkLinkValid);
    return kIOReturnSuccess;
}

bool YYavicMtkWlan::
setLinkStatus(UInt32 status, const IONetworkMedium * activeMedium, UInt64 speed, OSData * data)
{
    _ifnet *ifq = &fHalService->get80211Controller()->ic_ac.ac_if;
    /* YYavic: while the native layer owns the data path this interface
     * stays inactive (no flush: the radio keeps transmitting for the layer). */
    if (yy_rx_hook_fn != NULL && (status & kIONetworkLinkActive))
        return super::setLinkStatus(kIONetworkLinkValid, NULL, 0, NULL);
    bool ret = super::setLinkStatus(status, activeMedium, speed, data);
    if (fNetIf) {
        if (status & kIONetworkLinkActive) {
#ifdef __PRIVATE_SPI__
            fNetIf->startOutputThread();
#endif
        } else if (!(status & kIONetworkLinkNoNetworkChange)) {
#ifdef __PRIVATE_SPI__
            fNetIf->stopOutputThread();
            fNetIf->flushOutputQueue();
#endif
            ifq_flush(&ifq->if_snd);
            mq_purge(&fHalService->get80211Controller()->ic_mgtq);
        }
    }
    return ret;
}

IOReturn YYavicMtkWlan::getHardwareAddress(IOEthernetAddress *addrP)
{
    if (IEEE80211_ADDR_EQ(etheranyaddr, fHalService->get80211Controller()->ic_myaddr)) {
        return kIOReturnError;
    } else {
        IEEE80211_ADDR_COPY(addrP, fHalService->get80211Controller()->ic_myaddr);
        return kIOReturnSuccess;
    }
}

IOReturn YYavicMtkWlan::setHardwareAddress(const IOEthernetAddress *addrP)
{
    if (!fNetIf || !addrP)
        return kIOReturnError;
    if_setlladdr(&fHalService->get80211Controller()->ic_ac.ac_if, addrP->bytes);
    if (fHalService->get80211Controller()->ic_state > IEEE80211_S_INIT) {
        fHalService->disable(fNetIf);
        fHalService->enable(fNetIf);
    }
    return kIOReturnSuccess;
}

#ifdef __PRIVATE_SPI__
IOReturn YYavicMtkWlan::outputStart(IONetworkInterface *interface, IOOptionBits options)
{
    struct _ifnet *ifp = &fHalService->get80211Controller()->ic_ac.ac_if;
    mbuf_t m = NULL;
    if (ifq_is_oactive(&ifp->if_snd))
        return kIOReturnNoResources;
    while (kIOReturnSuccess == interface->dequeueOutputPackets(1, &m)) {
        if (outputPacket(m, NULL)!= kIOReturnOutputSuccess ||
            ifq_is_oactive(&ifp->if_snd))
            return kIOReturnNoResources;
    }
    return kIOReturnSuccess;
}
#endif

static IOReturn yy_if_start_gated(OSObject *, void *a0, void *, void *, void *)
{
    struct _ifnet *ifp = (struct _ifnet *)a0;
    (*ifp->if_start)(ifp);
    return kIOReturnSuccess;
}

/*
 * YYavic 0.6.6: asynchronous TX kick. The native layer's TX thread used to
 * enter the radio gate for every batch (runAction) and waited there while
 * the workloop processed RX -- 0.6.5 measured ~19 us per TX packet in the
 * layer, mostly TCP ACKs during downloads. Now it queues and signals this
 * event source; the workloop drains if_snd as soon as it is free (several
 * signals coalesce into one run).
 */

/*
 * YYavic single kext: batched TX straight from the native layer (no
 * callPlatformFunction, one gate entry per batch instead of per packet;
 * 0.5.2 measured 18.7 us per TX packet that way). `head` is an mbuf list
 * linked with mbuf_nextpkt; every mbuf is consumed.
 */
extern "C" uint32_t yy_radio_tx_batch(IOService *svc, mbuf_t head)
{
    YYavicMtkWlan *that = OSDynamicCast(YYavicMtkWlan, svc);
    uint32_t queued = 0;
    if (that == NULL || that->fHalService == NULL) {
        mbuf_freem_list(head);
        return 0;
    }
    struct ieee80211com *ic = that->fHalService->get80211Controller();
    _ifnet *ifp = &ic->ic_ac.ac_if;
    if (ic->ic_state != IEEE80211_S_RUN || ifp->if_snd.queue == NULL) {
        mbuf_freem_list(head);
        return 0;
    }
    while (head != NULL) {
        mbuf_t m = head;
        head = mbuf_nextpkt(m);
        mbuf_setnextpkt(m, NULL);
        if (!(mbuf_flags(m) & MBUF_PKTHDR) || !ifp->if_snd.queue->lockEnqueue(m)) {
            mbuf_freem(m);
            continue;
        }
        queued++;
    }
    if (queued != 0) {
        if (_fWorkloop != NULL && _fWorkloop->inGate())
            (*ifp->if_start)(ifp);
        else if (gYYTxKick != NULL) {
            gYYTxIfp = ifp;
            gYYTxKicks++;
            gYYTxKick->interruptOccurred(NULL, NULL, 0);
        } else if (_fCommandGate != NULL)
            _fCommandGate->runAction(yy_if_start_gated, ifp);
    }
    return queued;
}

UInt32 YYavicMtkWlan::outputPacket(mbuf_t m, void *param)
{
//    XYLog("%s\n", __FUNCTION__);
    IOReturn ret = kIOReturnOutputSuccess;
    _ifnet *ifp = &fHalService->get80211Controller()->ic_ac.ac_if;

    if (fHalService->get80211Controller()->ic_state != IEEE80211_S_RUN || ifp->if_snd.queue == NULL) {
        if (m && mbuf_type(m) != MBUF_TYPE_FREE) {
            freePacket(m);
        }
        return kIOReturnOutputDropped;
    }
    /* YYavic: return after each failure; the old code went on with a NULL
     * or already freed mbuf. */
    if (m == NULL) {
        XYLog("%s m==NULL!!\n", __FUNCTION__);
        ifp->netStat->outputErrors++;
        return kIOReturnOutputDropped;
    }
    if (mbuf_type(m) == MBUF_TYPE_FREE) {
        XYLog("%s mbuf is FREE!!\n", __FUNCTION__);
        ifp->netStat->outputErrors++;
        return kIOReturnOutputDropped;
    }
    if (!(mbuf_flags(m) & MBUF_PKTHDR) ){
        XYLog("%s pkthdr is NULL!!\n", __FUNCTION__);
        ifp->netStat->outputErrors++;
        freePacket(m);
        return kIOReturnOutputDropped;
    }
    if (!ifp->if_snd.queue->lockEnqueue(m)) {
        freePacket(m);
        ret = kIOReturnOutputDropped;
    }
    /*
     * YYavic: mwx_start fills TX ring slots and the CPU index without a lock;
     * the interrupt source, the poll timer and net80211 all run it on this
     * work loop's gate, the output paths did not (TX DMA froze on a broken
     * descriptor under load). Start through the gate; a call that already
     * holds it (net80211) runs directly. bridge9 used its own lock instead and
     * deadlocked against timeout_add, which takes this gate.
     */
    if (_fCommandGate != NULL && _fWorkloop != NULL && !_fWorkloop->inGate())
        _fCommandGate->runAction(yy_if_start_gated, ifp);
    else
        (*ifp->if_start)(ifp);
    return ret;
}

UInt32 YYavicMtkWlan::getFeatures() const
{
    return fHalService->getDriverInfo()->supportedFeatures();
}

IOReturn YYavicMtkWlan::setPromiscuousMode(IOEnetPromiscuousMode mode)
{
    return kIOReturnSuccess;
}

IOReturn YYavicMtkWlan::setMulticastMode(IOEnetMulticastMode mode)
{
    return kIOReturnSuccess;
}

IOReturn YYavicMtkWlan::setMulticastList(IOEthernetAddress* addr, UInt32 len)
{
    return fHalService->getDriverController()->setMulticastList(addr, len);
}

IOReturn YYavicMtkWlan::getPacketFilters(const OSSymbol *group, UInt32 *filters) const
{
    IOReturn    rtn = kIOReturnSuccess;
    if (group == gIOEthernetWakeOnLANFilterGroup && magicPacketSupported) {
        *filters = kIOEthernetWakeOnMagicPacket;
    } else if (group == gIONetworkFilterGroup) {
        *filters = kIOPacketFilterMulticast | kIOPacketFilterPromiscuous;
    } else {
        rtn = IOEthernetController::getPacketFilters(group, filters);
    }
    return rtn;
}

IOReturn YYavicMtkWlan::
tsleepHandler(OSObject* owner, void* arg0, void* arg1, void* arg2, void* arg3)
{
    YYavicMtkWlan* dev = OSDynamicCast(YYavicMtkWlan, owner);
    if (dev == 0)
        return kIOReturnError;
    
    if (arg1 == 0) {
        if (_fCommandGate->commandSleep(arg0, THREAD_INTERRUPTIBLE) == THREAD_AWAKENED)
            return kIOReturnSuccess;
        else
            return kIOReturnTimeout;
    } else {
        AbsoluteTime deadline;
        clock_interval_to_deadline((*(int*)arg1), kNanosecondScale, reinterpret_cast<uint64_t*> (&deadline));
        if (_fCommandGate->commandSleep(arg0, deadline, THREAD_INTERRUPTIBLE) == THREAD_AWAKENED)
            return kIOReturnSuccess;
        else
            return kIOReturnTimeout;
    }
}


/* YYavic radio bridge: associateSSID() with a ready PMK (from macOS) instead
 * of a passphrase; pmk == NULL joins an open network. */
void YYavicMtkWlan::associatePMK(const uint8_t *ssid, uint8_t ssid_len, const uint8_t *pmk)
{
    struct ieee80211com *ic = fHalService->get80211Controller();
    if (ssid_len > IEEE80211_NWID_LEN)
        ssid_len = IEEE80211_NWID_LEN;
    memset(&nwid, 0, sizeof(nwid));
    memcpy(nwid.i_nwid, ssid, ssid_len);
    nwid.i_len = ssid_len;
    memset(ic->ic_des_essid, 0, IEEE80211_NWID_LEN);
    ic->ic_des_esslen = nwid.i_len;
    memcpy(ic->ic_des_essid, nwid.i_nwid, nwid.i_len);
    if (ic->ic_des_esslen > 0)
        ic->ic_flags &= ~IEEE80211_F_AUTO_JOIN;     /* 'nwid' disables auto-join magic */
    ieee80211_disable_rsn(ic);
    ieee80211_disable_wep(ic);
    if (pmk != NULL) {
        memset(&psk, 0, sizeof(psk));
        memcpy(psk.i_psk, pmk, 32);
        psk.i_enabled = 1;
        ic->ic_flags |= IEEE80211_F_PSK;
        memcpy(ic->ic_psk, psk.i_psk, sizeof(ic->ic_psk));
        memset(&wpa, 0, sizeof(wpa));
        ieee80211_ioctl_getwpaparms(ic, &wpa);
        wpa.i_enabled = 1;
        wpa.i_ciphers = 0;
        wpa.i_groupcipher = 0;
        wpa.i_protos = IEEE80211_WPA_PROTO_WPA1 | IEEE80211_WPA_PROTO_WPA2;
        wpa.i_akms = IEEE80211_WPA_AKM_PSK | IEEE80211_WPA_AKM_8021X | IEEE80211_WPA_AKM_SHA256_PSK | IEEE80211_WPA_AKM_SHA256_8021X;
        ieee80211_ioctl_setwpaparms(ic, &wpa);
    } else {
        ic->ic_flags &= ~IEEE80211_F_PSK;
        memset(ic->ic_psk, 0, sizeof(ic->ic_psk));
    }
    if (ic->ic_state > IEEE80211_S_AUTH && ic->ic_bss != NULL)
        IEEE80211_SEND_MGMT(ic, ic->ic_bss, IEEE80211_FC0_SUBTYPE_DEAUTH, IEEE80211_REASON_AUTH_LEAVE);
    ieee80211_del_ess(ic, NULL, 0, 1);
    struct ieee80211_node *selbs = ieee80211_node_choose_bss(ic, 0, NULL);
    if (selbs == NULL) {
        if (ic->ic_state != IEEE80211_S_SCAN)
            ieee80211_new_state(ic, IEEE80211_S_SCAN, -1);
    } else {
        if (ic->ic_state > IEEE80211_S_AUTH) {
            ieee80211_node_join_bss(ic, selbs, 1);
            fHalService->getDriverController()->clearScanningFlags();
        } else if (ic->ic_state != IEEE80211_S_SCAN) {
            ieee80211_new_state(ic, IEEE80211_S_SCAN, -1);
        }
    }
}
/* ------------------------------------------------------------------------
 * YYavic radio bridge (yy_radio.h). Runs every request on the command gate,
 * like the mtkscan user client, so it is serialized with the net80211 state
 * machine.
 * ------------------------------------------------------------------------ */
#include "yy_radio.h"

enum { YY_RF_NONE, YY_RF_ABI, YY_RF_SCAN_START, YY_RF_SCAN_COPY, YY_RF_CHANNELS, YY_RF_JOIN, YY_RF_STATUS, YY_RF_LEAVE,
       YY_RF_MAC, YY_RF_DATA_HOOK, YY_RF_TX, YY_RF_SET_MAC, YY_RF_POWER };
struct yy_radio_gate_args { int fn; void *p1, *p2; };

static IOReturn yy_radio_gate(OSObject *owner, void *a0, void *, void *, void *)
{
    YYavicMtkWlan *that = OSDynamicCast(YYavicMtkWlan, owner);
    struct yy_radio_gate_args *a = (struct yy_radio_gate_args *)a0;
    if (that == NULL || a == NULL || that->fHalService == NULL)
        return kIOReturnNotReady;
    struct ieee80211com *ic = that->fHalService->get80211Controller();
    if (ic == NULL)
        return kIOReturnNotReady;

    switch (a->fn) {
    case YY_RF_SCAN_START: {
        if (a->p1)
            *(uint32_t *)a->p1 = ic->ic_state;
        /* Associated: background scan. Otherwise net80211 is already
         * scanning on its own and the node tree is the result. */
        if (ic->ic_state == IEEE80211_S_RUN)
            ieee80211_begin_cache_bgscan(&ic->ic_ac.ac_if);
        return kIOReturnSuccess;
    }
    case YY_RF_SCAN_COPY: {
        struct yy_radio_bss *out = (struct yy_radio_bss *)a->p1;
        uint32_t *count = (uint32_t *)a->p2;
        if (out == NULL || count == NULL)
            return kIOReturnBadArgument;
        uint32_t cap = *count, n = 0;
        struct ieee80211_node *ni;
        RB_FOREACH(ni, ieee80211_tree, &ic->ic_tree) {
            if (n >= cap)
                break;
            if (ni->ni_chan == NULL || ni->ni_esslen == 0)
                continue;
            struct yy_radio_bss *b = &out[n];
            bzero(b, sizeof(*b));
            memcpy(b->bssid, ni->ni_bssid, 6);
            b->ssid_len = ni->ni_esslen > 32 ? 32 : ni->ni_esslen;
            memcpy(b->ssid, ni->ni_essid, b->ssid_len);
            b->channel = ieee80211_chan2ieee(ic, ni->ni_chan);
            b->band = IEEE80211_IS_CHAN_5GHZ(ni->ni_chan) ? YY_RADIO_BAND_5G : YY_RADIO_BAND_2G;
            b->nrates = ni->ni_rates.rs_nrates > 16 ? 16 : ni->ni_rates.rs_nrates;
            memcpy(b->rates, ni->ni_rates.rs_rates, b->nrates);
            b->rssi_dbm = (int16_t)(-100 + ni->ni_rssi);   /* kMtkMinDbm + ni_rssi, as mtkscan */
            b->capinfo = ni->ni_capinfo;
            b->intval = ni->ni_intval;
            /* ni_rsnie_tlv holds all elements of the last beacon/probe
             * response (ieee80211_recv_probe_resp), not only RSN. */
            if (ni->ni_rsnie_tlv != NULL && ni->ni_rsnie_tlv_len > 0) {
                b->ies_len = ni->ni_rsnie_tlv_len > sizeof(b->ies) ? sizeof(b->ies) : ni->ni_rsnie_tlv_len;
                memcpy(b->ies, ni->ni_rsnie_tlv, b->ies_len);
            }
            n++;
        }
        *count = n;
        return kIOReturnSuccess;
    }
    case YY_RF_CHANNELS: {
        struct yy_radio_chan *out = (struct yy_radio_chan *)a->p1;
        uint32_t *count = (uint32_t *)a->p2;
        if (out == NULL || count == NULL)
            return kIOReturnBadArgument;
        uint32_t cap = *count, n = 0;
        for (int i = 0; i <= IEEE80211_CHAN_MAX && n < cap; i++) {
            struct ieee80211_channel *c = &ic->ic_channels[i];
            if (c->ic_freq == 0)
                continue;
            out[n].channel = ieee80211_chan2ieee(ic, c);
            out[n].band = IEEE80211_IS_CHAN_5GHZ(c) ? YY_RADIO_BAND_5G : YY_RADIO_BAND_2G;
            out[n].passive = (c->ic_flags & IEEE80211_CHAN_PASSIVE) ? 1 : 0;
            out[n].reserved = 0;
            n++;
        }
        *count = n;
        return kIOReturnSuccess;
    }
    case YY_RF_JOIN: {
        const struct yy_radio_join *j = (const struct yy_radio_join *)a->p1;
        if (j == NULL || j->ssid_len == 0 || j->ssid_len > 32)
            return kIOReturnBadArgument;
        that->associatePMK(j->ssid, j->ssid_len, j->has_pmk ? j->pmk : NULL);
        return kIOReturnSuccess;
    }
    case YY_RF_STATUS: {
        struct yy_radio_status *st = (struct yy_radio_status *)a->p1;
        if (st == NULL)
            return kIOReturnBadArgument;
        bzero(st, sizeof(*st));
        st->state = ic->ic_state;
        st->rsn = (ic->ic_flags & IEEE80211_F_RSNON) ? 1 : 0;
        struct ieee80211_node *ni = ic->ic_bss;
        if (ni != NULL && ic->ic_state >= IEEE80211_S_AUTH) {
            memcpy(st->bssid, ni->ni_bssid, 6);
            st->ssid_len = ni->ni_esslen > 32 ? 32 : ni->ni_esslen;
            memcpy(st->ssid, ni->ni_essid, st->ssid_len);
            if (ni->ni_chan != NULL) {
                st->channel = ieee80211_chan2ieee(ic, ni->ni_chan);
                st->band = IEEE80211_IS_CHAN_5GHZ(ni->ni_chan) ? YY_RADIO_BAND_5G : YY_RADIO_BAND_2G;
            }
            st->rssi_dbm = (int16_t)(-100 + ni->ni_rssi);
            st->port_valid = (!st->rsn || ni->ni_port_valid) ? 1 : 0;
            if (ni->ni_txrate >= 0 && ni->ni_txrate < ni->ni_rates.rs_nrates)
                st->tx_rate_mbps = (ni->ni_rates.rs_rates[ni->ni_txrate] & IEEE80211_RATE_VAL) / 2;
            if (ic->ic_state == IEEE80211_S_RUN) {
                uint8_t rx[5];
                mwx_yy_link_info(ic, &st->bss_bw, &st->center, rx);
                st->rx_mode = rx[0]; st->rx_mcs = rx[1]; st->rx_nss = rx[2]; st->rx_bw = rx[3]; st->rx_sgi = rx[4];
            }
            st->he_link = (ni->ni_flags & IEEE80211_NODE_HE) != 0;
        }
        st->he_enabled = (ic->ic_flags & IEEE80211_F_HEON) != 0;
        return kIOReturnSuccess;
    }
    case YY_RF_LEAVE: {
        if (ic->ic_state > IEEE80211_S_AUTH && ic->ic_bss != NULL)
            IEEE80211_SEND_MGMT(ic, ic->ic_bss, IEEE80211_FC0_SUBTYPE_DEAUTH, IEEE80211_REASON_AUTH_LEAVE);
        ieee80211_del_ess(ic, NULL, 0, 1);
        memset(ic->ic_des_essid, 0, IEEE80211_NWID_LEN);
        ic->ic_des_esslen = 0;
        if (ic->ic_state != IEEE80211_S_SCAN && ic->ic_state != IEEE80211_S_INIT)
            ieee80211_new_state(ic, IEEE80211_S_SCAN, -1);
        return kIOReturnSuccess;
    }
    case YY_RF_MAC: {
        if (a->p1 == NULL)
            return kIOReturnBadArgument;
        if (IEEE80211_ADDR_EQ(etheranyaddr, ic->ic_myaddr))
            return kIOReturnNotReady;
        memcpy(a->p1, ic->ic_myaddr, 6);
        return kIOReturnSuccess;
    }
    case YY_RF_SET_MAC: {
        MtkMwx *hal = OSDynamicCast(MtkMwx, that->fHalService);
        if (a->p1 == NULL)
            return kIOReturnBadArgument;
        if (hal == NULL)
            return kIOReturnUnsupported;
        if (ic->ic_state > IEEE80211_S_SCAN)
            return kIOReturnBusy;           /* not while (being) associated */
        return hal->setOwnAddress((const uint8_t *)a->p1);
    }
    case YY_RF_POWER: {
        if (a->p1 == NULL)
            return kIOReturnBadArgument;
        bool on = *(const uint32_t *)a->p1 != 0;
        if (!on) {
            if (ic->ic_state > IEEE80211_S_AUTH && ic->ic_bss != NULL) {
                IEEE80211_SEND_MGMT(ic, ic->ic_bss, IEEE80211_FC0_SUBTYPE_DEAUTH, IEEE80211_REASON_AUTH_LEAVE);
                IOSleep(30);                /* let the deauth out before the device goes down */
            }
            ieee80211_del_ess(ic, NULL, 0, 1);
            memset(ic->ic_des_essid, 0, IEEE80211_NWID_LEN);
            ic->ic_des_esslen = 0;
            IOReturn r = that->fHalService->disable(NULL);
            XYLog("YYavic: radio power off = 0x%x\n", r);
            return r;
        }
        IOReturn r = that->fHalService->enable(NULL);
        XYLog("YYavic: radio power on = 0x%x\n", r);
        return r;
    }
    case YY_RF_DATA_HOOK: {
        /* On the gate, so _if_input (also gated) never sees a half-set hook. */
        yy_rx_hook_ctx = a->p2;
        yy_rx_hook_fn = (yy_radio_rx_fn)a->p1;
        /*
         * Send queue depth: 2048 frames is ~540 ms at 45 Mbps uplink (measured
         * "Responsiveness 538 ms"). Skywalk's netif queue (fq_codel) does the
         * queueing in front of the layer, so keep this one short while the
         * native layer owns the data path.
         */
        if (ic->ic_ac.ac_if.if_snd.queue != NULL)
            ic->ic_ac.ac_if.if_snd.queue->setCapacity(yy_rx_hook_fn != NULL ? 512 : 2048);
        if (yy_rx_hook_fn != NULL) {
            that->IOEthernetController::setLinkStatus(kIONetworkLinkValid, NULL, 0, NULL);
            XYLog("YYavic: data hook attached, own interface inactive\n");
        } else {
            if (ic->ic_state == IEEE80211_S_RUN)
                that->IOEthernetController::setLinkStatus(kIONetworkLinkValid | kIONetworkLinkActive,
                                                          that->getCurrentMedium(), 0, NULL);
            XYLog("YYavic: data hook detached\n");
        }
        return kIOReturnSuccess;
    }
    default:
        return kIOReturnUnsupported;
    }
}

IOReturn YYavicMtkWlan::callPlatformFunction(const OSSymbol *functionName, bool waitForFunction,
                                             void *param1, void *param2, void *param3, void *param4)
{
    int fn = YY_RF_NONE;
    if (functionName != NULL) {
        if (functionName->isEqualTo(YY_RADIO_FN_ABI))
            fn = YY_RF_ABI;
        else if (functionName->isEqualTo(YY_RADIO_FN_SCAN_START))
            fn = YY_RF_SCAN_START;
        else if (functionName->isEqualTo(YY_RADIO_FN_SCAN_COPY))
            fn = YY_RF_SCAN_COPY;
        else if (functionName->isEqualTo(YY_RADIO_FN_CHANNELS))
            fn = YY_RF_CHANNELS;
        else if (functionName->isEqualTo(YY_RADIO_FN_JOIN))
            fn = YY_RF_JOIN;
        else if (functionName->isEqualTo(YY_RADIO_FN_STATUS))
            fn = YY_RF_STATUS;
        else if (functionName->isEqualTo(YY_RADIO_FN_LEAVE))
            fn = YY_RF_LEAVE;
        else if (functionName->isEqualTo(YY_RADIO_FN_MAC))
            fn = YY_RF_MAC;
        else if (functionName->isEqualTo(YY_RADIO_FN_DATA_HOOK))
            fn = YY_RF_DATA_HOOK;
        else if (functionName->isEqualTo(YY_RADIO_FN_TX))
            fn = YY_RF_TX;
        else if (functionName->isEqualTo(YY_RADIO_FN_SET_MAC))
            fn = YY_RF_SET_MAC;
        else if (functionName->isEqualTo(YY_RADIO_FN_POWER))
            fn = YY_RF_POWER;
    }
    if (fn == YY_RF_NONE)
        return super::callPlatformFunction(functionName, waitForFunction, param1, param2, param3, param4);
    if (fn == YY_RF_ABI) {
        if (param1 != NULL)
            *(uint32_t *)param1 = YY_RADIO_ABI;
        return kIOReturnSuccess;
    }
    if (fn == YY_RF_TX) {
        /* outputPacket enqueues without the gate and starts the radio
         * through it. The RX hook never waits for the layer's work queue, so
         * the layer calling in from its work queue cannot deadlock. */
        if (param1 == NULL)
            return kIOReturnBadArgument;
        return outputPacket((mbuf_t)param1, NULL) == kIOReturnOutputSuccess ? kIOReturnSuccess : kIOReturnOutputDropped;
    }
    if (_fCommandGate == NULL)
        return kIOReturnNotReady;
    struct yy_radio_gate_args a = { fn, param1, param2 };
    /* arg0 is handed to yy_radio_gate as a0; the gate owner is passed separately */
    return _fCommandGate->runAction(yy_radio_gate, &a);
}

