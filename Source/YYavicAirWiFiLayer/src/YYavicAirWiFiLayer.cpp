//
//  YYavicAirWiFiLayer.cpp
//  YYavicAirWiFiLayer - native macOS Wi-Fi (IO80211/Skywalk) layer for MediaTek MT7922
//
//  Copyright (C) 2026 Yusufcan Yavic
//  SPDX-License-Identifier: GPL-3.0-or-later
//

#include "YYavicAirWiFiLayer.hpp"
#include <kern/thread_call.h>
#include <IOKit/pwr_mgt/RootDomain.h>
#include <libkern/OSDebug.h>
#include <sys/proc.h>
#include <net/kpi_interface.h>
#include <IOKit/network/IOOutputQueue.h>
#include <stdarg.h>
#include <IOKit/IODeviceTreeSupport.h>

/* ---------------------------------------------------------------- trace -- */
/* Ring of the most recent trace text plus the first 8 KB, published to the
 * IORegistry once a second (kext IOLog output does not reach the unified log
 * on this system, so the registry is the observation channel). */
static char gYYHead[8192];
static size_t gYYHeadLen;
static char gYYTail[16384];
static size_t gYYTailPos;
static bool gYYTailWrapped;
static IOSimpleLock *gYYLock;
static IOService *gYYPublisher;
static thread_call_t gYYTimer;
/*
 * 0.1.15 panicked inside yy_publish (setProperty -> OSSymbol::free ->
 * OSSymbolPool::removeSymbol). The publisher now keeps its two keys alive
 * for the life of the kext and only touches the registry when the trace
 * changed, instead of allocating and freeing a symbol pair every second.
 */
static const OSSymbol *gYYKeyTrace, *gYYKeyLast;
static volatile uint32_t gYYGen;
static uint32_t gYYPubGen;

extern "C" void yy_trace(const char *fmt, ...)
{
    char line[256];
    va_list ap;
    va_start(ap, fmt);
    int n = vsnprintf(line, sizeof(line), fmt, ap);
    va_end(ap);
    if (n <= 0)
        return;
    if ((size_t)n >= sizeof(line))
        n = sizeof(line) - 1;
    if (gYYLock == nullptr)
        return;
    IOSimpleLockLock(gYYLock);
    if (gYYHeadLen + n < sizeof(gYYHead)) {
        memcpy(gYYHead + gYYHeadLen, line, n);
        gYYHeadLen += n;
    }
    for (int i = 0; i < n; i++) {
        gYYTail[gYYTailPos++] = line[i];
        if (gYYTailPos == sizeof(gYYTail)) {
            gYYTailPos = 0;
            gYYTailWrapped = true;
        }
    }
    gYYGen++;
    IOSimpleLockUnlock(gYYLock);
}

static void yy_publish(thread_call_param_t, thread_call_param_t)
{
    static char head[sizeof(gYYHead) + 1], tail[sizeof(gYYTail) + 1];
    size_t hl, tl = 0;

    if (gYYPublisher == nullptr || gYYKeyTrace == nullptr || gYYKeyLast == nullptr)
        return;
    IOSimpleLockLock(gYYLock);
    uint32_t gen = gYYGen;
    if (gen == gYYPubGen) {
        IOSimpleLockUnlock(gYYLock);
        goto rearm;
    }
    gYYPubGen = gen;
    hl = gYYHeadLen;
    memcpy(head, gYYHead, hl);
    if (gYYTailWrapped) {
        tl = sizeof(gYYTail) - gYYTailPos;
        memcpy(tail, gYYTail + gYYTailPos, tl);
    }
    memcpy(tail + tl, gYYTail, gYYTailPos);
    tl += gYYTailPos;
    IOSimpleLockUnlock(gYYLock);
    head[hl] = 0;
    tail[tl] = 0;
    {
        OSString *h = OSString::withCString(head);
        OSString *t = OSString::withCString(tail);
        if (h != nullptr)
            gYYPublisher->setProperty(gYYKeyTrace, h);
        if (t != nullptr)
            gYYPublisher->setProperty(gYYKeyLast, t);
        OSSafeReleaseNULL(h);
        OSSafeReleaseNULL(t);
    }

rearm:
    uint64_t deadline;
    clock_interval_to_deadline(1000, kMillisecondScale, &deadline);
    thread_call_enter_delayed(gYYTimer, deadline);
}

static void yy_trace_start(IOService *publisher)
{
    if (gYYLock == nullptr)
        gYYLock = IOSimpleLockAlloc();
    if (gYYKeyTrace == nullptr)
        gYYKeyTrace = OSSymbol::withCString("yy-trace");
    if (gYYKeyLast == nullptr)
        gYYKeyLast = OSSymbol::withCString("yy-last");
    gYYPublisher = publisher;
    if (gYYTimer == nullptr) {
        gYYTimer = thread_call_allocate(yy_publish, nullptr);
        uint64_t deadline;
        clock_interval_to_deadline(1000, kMillisecondScale, &deadline);
        thread_call_enter_delayed(gYYTimer, deadline);
    }
}

/* ------------------------------------------------------------------ PCI -- */
#define YY_PCI_CAP_ID_MSI   0x05
#define YY_PCI_CAP_ID_MSIX  0x11

static IO80211WorkQueue *gYYWorkQueue;

/*
 * Arm gate. 0.1.2 and 0.1.3 both had the controller started by
 * _IOConfigThread without yyarm (panic reports), so the gate is enforced in
 * registerService(), probe() and start(), and every blocked attempt records
 * its (unslid) call chain.
 */
static volatile bool gYYArmed;
/* Chosen at arm time: 0 = legacy BSD interface (AirportItlwm scheme),
 * 1 = Skywalk netif datapath (research, BSD client). */
static volatile int gYYMode;
/* the running controller (one instance) */
static class YYavicAirWiFiLayer *gYYCtrlScan;
static uint32_t yy_chan_flags(uint8_t ch, uint8_t band);
static uint32_t yy_chan_flags_w(uint8_t ch, uint8_t band, uint8_t bw, uint8_t center);
static uint32_t yy_rate_100k(uint8_t mode, uint8_t mcs, uint8_t nss, uint8_t bw, uint8_t sgi);
static void yy_bsd_schedule(uint32_t delayMs);
static void yy_drvavail_request();
static uint32_t gYYDrvAvailReason = 0xe0822803;	/* yy-drvavail can change it */

static void yy_trace_bt(const char *what)
{
    void *bt[10];
    unsigned n = OSBacktrace(bt, 10);
    /* Raw (slid) addresses plus two anchors to compute the slides offline:
     * a kernel function and an IO80211Family (kernel collection) function. */
    yy_trace("%s: anchors OSBacktrace=%p IO80211WorkQueue::workQueue=%p\n", what,
             (void *)&OSBacktrace, (void *)&IO80211WorkQueue::workQueue);
    yy_trace("%s: bt", what);
    for (unsigned i = 1; i < n; i++)
        yy_trace(" %p", bt[i]);
    yy_trace("\n");
}

static IOPMPowerState gYYPowerStates[2] = {
    {1, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0},
    {1, kIOPMDeviceUsable, kIOPMPowerOn, kIOPMPowerOn, 0, 0, 0, 0, 0, 0, 0, 0},
};

OSDefineMetaClassAndStructors(YYavicAirWiFiLayerPCI, IOService)

#define YY_AUTOARM_MIN_S    20      /* auto-arm: not before 20 s after start */
static thread_call_t gYYAutoCall;
static void yy_autoarm_fire(thread_call_param_t p0, thread_call_param_t);

IOService *YYavicAirWiFiLayerPCI::probe(IOService *provider, SInt32 *score)
{
    if (IOService::probe(provider, score) == nullptr)
        return nullptr;
    /*
     * 2a runs on IOResources next to the YYavicMtkWlan radio driver (which
     * keeps owning the PCI function and the network connection). The PCI
     * path is kept for when the layer drives the radio itself.
     */
    IOPCIDevice *dev = OSDynamicCast(IOPCIDevice, provider);
    if (dev != nullptr &&
        (dev->configRead16(kIOPCIConfigVendorID) != 0x14c3 ||
         dev->configRead16(kIOPCIConfigDeviceID) != 0x7922))
        return nullptr;
    fPci = dev;
    return this;
}

bool YYavicAirWiFiLayerPCI::start(IOService *provider)
{
    yy_trace_start(this);
    yy_trace("YYavicAirWiFiLayer 2a: PCI start\n");
    if (gYYWorkQueue == nullptr)
        gYYWorkQueue = IO80211WorkQueue::workQueue();
    yy_trace("PCI: workQueue=%p\n", gYYWorkQueue);
    if (gYYWorkQueue == nullptr || !IOService::start(provider))
        return false;

    if (fPci != nullptr) {
        UInt8 msi = 0, msix = 0;
        fPci->findPCICapability(YY_PCI_CAP_ID_MSIX, &msix);
        fPci->findPCICapability(YY_PCI_CAP_ID_MSI, &msi);
        yy_trace("PCI: msi_cap=0x%x msix_cap=0x%x\n", msi, msix);
    } else {
        yy_trace("PCI: provider=%s (no PCI function, radio stays with YYavicMtkWlan)\n",
                 provider->getName());
    }

    UInt8 builtIn = 0;
    setProperty("built-in", OSData::withBytes(&builtIn, sizeof(builtIn)));
    PMinit();
    registerPowerDriver(this, gYYPowerStates, 2);
    if (fPci != nullptr)
        provider->joinPMtree(this);
    else
        getPMRootDomain()->joinPMtree(this);
    /*
     * Crash-safe bring-up: do NOT publish the wrapper at boot. The IO80211
     * controller only matches after `yyarm` (setProperties "yy-arm") is issued
     * from a fully booted system, so a fault cannot cause a boot loop and the
     * unified log / panic report are written to disk.
     */
    fArmed = false;
    setProperty("yy-state", "waiting-for-yyarm");
    yy_trace("PCI: ready, waiting for yyarm (controller not started)\n");
    int off = 0;
    if (PE_parse_boot_argn("-yyavicoff", &off, sizeof(off))) {
        yy_trace("PCI: -yyavicoff: auto-arm disabled\n");
        setProperty("yy-state", "waiting-for-yyarm (-yyavicoff)");
    } else {
        gYYAutoCall = thread_call_allocate(yy_autoarm_fire, this);
        uint64_t dl;
        clock_interval_to_deadline(YY_AUTOARM_MIN_S, kSecondScale, &dl);
        if (gYYAutoCall != nullptr)
            thread_call_enter_delayed(gYYAutoCall, dl);
        yy_trace("PCI: auto-arm in %u s once the radio is up\n", YY_AUTOARM_MIN_S);
    }
    return true;
}

/*
 * 0.1.4 was armed (start passed the gate) although neither yyarm nor arm.sh
 * ran, so every setProperties call is now recorded with its caller, arming
 * needs an explicit string token, and the controller is only published 10 s
 * after arming so the trace reaches disk before anything can go wrong.
 */
#define YY_ARM_TOKEN "yyavic-arm-2a"
static thread_call_t gYYArmCall;

static void yy_arm_fire(thread_call_param_t p0, thread_call_param_t)
{
    YYavicAirWiFiLayerPCI *pci = (YYavicAirWiFiLayerPCI *)p0;
    yy_trace("PCI: arm delay elapsed -> registerService\n");
    pci->setProperty("yy-state", "armed");
    pci->registerService();
}

IOReturn YYavicAirWiFiLayerPCI::setProperties(OSObject *properties)
{
    char pname[64] = "?";
    proc_selfname(pname, sizeof(pname));
    yy_trace("PCI: setProperties pid=%d (%s) type=%s\n", proc_selfpid(), pname,
             properties ? properties->getMetaClass()->getClassName() : "null");
    OSDictionary *d = OSDynamicCast(OSDictionary, properties);
    if (d == nullptr)
        return kIOReturnBadArgument;
    OSCollectionIterator *it = OSCollectionIterator::withCollection(d);
    if (it != nullptr) {
        OSSymbol *k;
        while ((k = OSDynamicCast(OSSymbol, it->getNextObject())) != nullptr) {
            OSObject *v = d->getObject(k);
            yy_trace("PCI:   key %s = %s\n", k->getCStringNoCopy(),
                     v ? v->getMetaClass()->getClassName() : "null");
        }
        it->release();
    }
    if (d->getObject("yy-drvavail") != nullptr && gYYArmed) {
        /* Only the boot form (reason 0xe0822803). The recovery form
         * (0xe0823805) makes WCL replay deferred notifications; sent while
         * associated that panicked WCLNetManager ("invalid state
         * transition"), so it is not offered. */
        uint32_t reason = 0xe0822803;
        gYYDrvAvailReason = reason;
        yy_trace("PCI: yy-drvavail reason=0x%08x -> APPLE80211_M_DRIVER_AVAILABLE\n", reason);
        yy_drvavail_request();
        return kIOReturnSuccess;
    }
    if (d->getObject("yy-bsd") != nullptr && gYYArmed) {
        yy_trace("PCI: yy-bsd -> BSD attach poll\n");
        yy_bsd_schedule(10);
        return kIOReturnSuccess;
    }
    OSString *arm = OSDynamicCast(OSString, d->getObject("yy-arm"));
    int mode = -1;
    if (arm != nullptr && (arm->isEqualTo(YY_ARM_TOKEN) || arm->isEqualTo("yyavic-arm-legacy")))
        mode = 0;
    else if (arm != nullptr && arm->isEqualTo("yyavic-arm-skw"))
        mode = 1;
    if (mode < 0) {
        yy_trace("PCI: setProperties ignored (no arm token)\n");
        return kIOReturnUnsupported;
    }
    armNow(mode, pname);
    return kIOReturnSuccess;
}

void YYavicAirWiFiLayerPCI::armNow(int mode, const char *by)
{
    if (!fArmed)
        gYYMode = mode;
    yy_trace("PCI: arm mode = %s\n", gYYMode == 0 ? "legacy" : "skywalk");
    if (fArmed)
        return;
    fArmed = true;
    gYYArmed = true;
    setProperty("yy-state", "arming");
    setProperty("yy-armed-by", by);
    yy_trace("PCI: ARMED by %s; controller in 10 s\n", by);
    if (gYYArmCall == nullptr)
        gYYArmCall = thread_call_allocate(yy_arm_fire, this);
    uint64_t deadline;
    clock_interval_to_deadline(10, kSecondScale, &deadline);
    thread_call_enter_delayed(gYYArmCall, deadline);
}

/*
 * Auto-arm (single kext): once the radio is up, start the native layer in
 * Skywalk mode without yyarm. Safety:
 *  - boot-arg -yyavicoff: never auto-arm (yyarm still works);
 *  - NVRAM guard: "yyavic-arm-guard" is written before arming and removed
 *    after 60 s of running. If a boot finds it set, that boot died while
 *    armed: skip auto-arm once and clear it (next boot tries again).
 */
#define YY_GUARD_KEY        "yyavic-arm-guard"
#define YY_AUTOARM_TRIES    120     /* x 2 s */
static thread_call_t gYYGuardClearCall;
static uint32_t gYYAutoTries;

/* Only IORegistryEntry virtuals are used on the NVRAM entry: IODTNVRAM's own
 * vtable changed across releases and the SDK header is older than 15.8.1.
 * "IONVRAM-SYNCNOW-PROPERTY" asks IODTNVRAM to write through now. */
static IORegistryEntry *yy_nvram()
{
    /* "/options" in the device tree is the NVRAM entry (AppleEFINVRAM here). */
    return IORegistryEntry::fromPath("/options", gIODTPlane);   /* retained */
}

static void yy_nvram_sync(IORegistryEntry *n)
{
    const OSSymbol *k = OSSymbol::withCString("IONVRAM-SYNCNOW-PROPERTY");
    if (k != nullptr) {
        n->setProperty(k, kOSBooleanTrue);
        k->release();
    }
}

static bool yy_guard_get_and_clear()
{
    IORegistryEntry *n = yy_nvram();
    if (n == nullptr)
        return false;
    const OSSymbol *k = OSSymbol::withCString(YY_GUARD_KEY);
    OSObject *v = k != nullptr ? n->copyProperty(k) : nullptr;
    bool set = v != nullptr;
    OSSafeReleaseNULL(v);
    if (set) {
        n->removeProperty(k);
        yy_nvram_sync(n);
    }
    OSSafeReleaseNULL(k);
    n->release();
    return set;
}

static void yy_guard_set(bool on)
{
    IORegistryEntry *n = yy_nvram();
    if (n == nullptr)
        return;
    const OSSymbol *k = OSSymbol::withCString(YY_GUARD_KEY);
    if (k != nullptr && on) {
        static const char v[] = "arming";
        OSData *d = OSData::withBytes(v, sizeof(v) - 1);
        if (d != nullptr) {
            n->setProperty(k, d);
            d->release();
        }
    } else if (k != nullptr) {
        n->removeProperty(k);
    }
    OSSafeReleaseNULL(k);
    yy_nvram_sync(n);
    n->release();
}

static void yy_guard_clear_fire(thread_call_param_t, thread_call_param_t)
{
    yy_guard_set(false);
    yy_trace("PCI: auto-arm stable for 60 s, NVRAM guard cleared\n");
}

/*
 * The layer is built against the IO80211Family/IOSkywalkFamily class layouts
 * of macOS 15.8.1 (24H32, IO80211_15_8_1.h). On another layout virtual calls
 * land in the wrong slots and the kernel panics, so arm only on Darwin 24.6
 * with OS build 24H32 (registry root "OS Build Version"). -yyavicnoverify
 * skips the check (testers on other builds, at their own risk).
 */
extern const int version_major;
extern const int version_minor;
/* 1 = supported, 0 = not supported, -1 = not known yet: the registry root
 * gets "OS Build Version" only once user space sets kern.osversion, which
 * can be after the layer's first arm attempt (0.7.0 boot test). */
static int yy_os_layout_ok(char *why, size_t whylen)
{
    int dummy = 0;
    if (PE_parse_boot_argn("-yyavicnoverify", &dummy, sizeof(dummy)))
        return 1;
    if (version_major != 24 || version_minor != 6) {
        snprintf(why, whylen, "Darwin %d.%d (needs 24.6, macOS 15.8.1 24H32)", version_major, version_minor);
        return 0;
    }
    IORegistryEntry *root = IORegistryEntry::getRegistryRoot();	/* not retained */
    OSString *b = root != nullptr ? OSDynamicCast(OSString, root->getProperty("OS Build Version")) : nullptr;
    if (b == nullptr) {
        snprintf(why, whylen, "OS build not published yet");
        return -1;
    }
    if (b->isEqualTo("24H32"))
        return 1;
    snprintf(why, whylen, "OS build %s (needs 24H32, macOS 15.8.1)", b->getCStringNoCopy());
    return 0;
}

static void yy_autoarm_fire(thread_call_param_t p0, thread_call_param_t)
{
    YYavicAirWiFiLayerPCI *pci = (YYavicAirWiFiLayerPCI *)p0;
    if (pci->fArmed)
        return;
    {
        char why[96];
        static uint32_t osWaits;
        int ok = yy_os_layout_ok(why, sizeof(why));
        if (ok < 0 && osWaits++ < 150) {            /* wait up to ~5 min for it */
            if (osWaits == 1)
                yy_trace("PCI: waiting for the OS build to be published\n");
            uint64_t dl;
            clock_interval_to_deadline(2, kSecondScale, &dl);
            thread_call_enter_delayed(gYYAutoCall, dl);
            return;
        }
        if (ok != 1) {
            yy_trace("PCI: NOT arming, unsupported macOS: %s\n", why);
            IOLog("YYavicWiFi: native Wi-Fi layer disabled, unsupported macOS: %s\n", why);
            pci->setProperty("yy-state", "disabled: unsupported macOS (needs 15.8.1)");
            return;
        }
        if (osWaits != 0)
            yy_trace("PCI: OS build 24H32 published after %u waits\n", osWaits);
    }
    gYYAutoTries++;
    OSDictionary *m = IOService::serviceMatching("YYavicMtkWlan");
    IOService *r = m != nullptr ? IOService::copyMatchingService(m) : nullptr;
    OSSafeReleaseNULL(m);
    uint32_t abi = 0;
    uint8_t mac[6];
    bool ready = false;
    if (r != nullptr) {
        r->callPlatformFunction(YY_RADIO_FN_ABI, false, &abi, nullptr, nullptr, nullptr);
        ready = abi >= 4 &&
                r->callPlatformFunction(YY_RADIO_FN_MAC, false, mac, nullptr, nullptr, nullptr) == kIOReturnSuccess;
        r->release();
    }
    if (!ready) {
        if (gYYAutoTries < YY_AUTOARM_TRIES) {
            uint64_t dl;
            clock_interval_to_deadline(2, kSecondScale, &dl);
            thread_call_enter_delayed(gYYAutoCall, dl);
        } else {
            yy_trace("PCI: auto-arm gave up, radio not ready (abi %u)\n", abi);
            pci->setProperty("yy-state", "auto-arm: radio not ready");
        }
        return;
    }
    if (yy_guard_get_and_clear()) {
        yy_trace("PCI: auto-arm SKIPPED: previous boot did not survive arming (guard cleared)\n");
        pci->setProperty("yy-state", "auto-arm skipped (guard)");
        return;
    }
    yy_guard_set(true);
    pci->armNow(1, "auto");
    if (gYYGuardClearCall == nullptr)
        gYYGuardClearCall = thread_call_allocate(yy_guard_clear_fire, nullptr);
    uint64_t dl;
    clock_interval_to_deadline(60, kSecondScale, &dl);
    thread_call_enter_delayed(gYYGuardClearCall, dl);
}

void YYavicAirWiFiLayerPCI::registerService(IOOptionBits options)
{
    if (!gYYArmed) {
        yy_trace("PCI: registerService BLOCKED (not armed) opts=0x%x\n", (unsigned)options);
        yy_trace_bt("PCI: registerService caller");
        return;
    }
    yy_trace_bt("PCI: registerService (armed)");
    IOService::registerService(options);
}

void YYavicAirWiFiLayerPCI::stop(IOService *provider)
{
    yy_trace("PCI: stop\n");
    PMstop();
    IOService::stop(provider);
}

IOWorkLoop *YYavicAirWiFiLayerPCI::getWorkLoop() const
{
    return (IOWorkLoop *)gYYWorkQueue;
}

IOReturn YYavicAirWiFiLayerPCI::setPowerState(unsigned long state, IOService *)
{
    yy_trace("PCI: setPowerState %lu\n", state);
    return IOPMAckImplied;
}

/* ------------------------------------------------------------ interface -- */
OSDefineMetaClassAndStructors(YYavicAirWiFiLayerInterface, IO80211InfraProtocol)

static YYavicAirWiFiLayer *yy_ctrl_of(IO80211SkywalkInterface *);

/*
 * Private Wi-Fi address. WCL sets it before every join
 * (APPLE80211_IOC_SET_MAC_ADDRESS -> IO80211MacAddressAgent -> here) and only
 * kIOReturnNotPermitted (0xe00002e2) lets the join go on without it. The radio
 * can take a new own address now (yyRadioSetMac: firmware device re-added),
 * which it accepts while not associated (WCL leaves before joining). Anything
 * else (associated, old radio) is refused that way.
 */
IOReturn YYavicAirWiFiLayerInterface::setMacAddress(ether_addr &a)
{
    YYavicAirWiFiLayer *ctl = yy_ctrl_of(this);
    IOReturn r = kIOReturnNotPermitted;
    if (ctl != nullptr && !(a.octet[0] & 1))
        r = ctl->setRadioMac(a.octet);
    static uint32_t n;
    if (n++ < 10)
        yy_trace("IF: setMacAddress %02x:%02x:%02x:%02x:%02x:%02x = 0x%x\n", a.octet[0], a.octet[1], a.octet[2],
                 a.octet[3], a.octet[4], a.octet[5], r);
    return r;
}

/*
 * WCLJoinManager::handleJoinRequest reads this first and fails the join on
 * error (0.3.1: "unable to get low latency traffic stats retVal 0xe00002c7").
 * 4 bytes: u8, u8, u16; AppleBCMWLANCore::getWCL_LOW_LATENCY_INFO writes
 * zeros when it has no low-latency session, as here.
 */
IOReturn YYavicAirWiFiLayerInterface::getWCL_LOW_LATENCY_INFO(apple80211_low_latency_info *d)
{
    if (d == nullptr)
        return kIOReturnBadArgument;
    bzero(d, 4);
    return kIOReturnSuccess;
}

IOReturn YYavicAirWiFiLayerInterface::getWCL_BSS_INFO(apple80211_beacon_msg *m)
{
    YYavicAirWiFiLayer *ctl = yy_ctrl_of(this);
    IOReturn r = ctl ? ctl->fillBssInfo((uint8_t *)m) : kIOReturnNotReady;
    static uint32_t n;
    if (n++ < 5)
        yy_trace("IF: getWCL_BSS_INFO = 0x%x\n", r);
    return r;
}

IOReturn YYavicAirWiFiLayerInterface::getWCL_EXTENDED_BSS_INFO(apple80211_extended_bss_info *d)
{
    YYavicAirWiFiLayer *ctl = yy_ctrl_of(this);
    IOReturn r = ctl ? ctl->fillExtendedBssInfo((uint8_t *)d) : kIOReturnNotReady;
    static uint32_t n;
    if (n++ < 5)
        yy_trace("IF: getWCL_EXTENDED_BSS_INFO = 0x%x\n", r);
    return r;
}

/* Read by WCLNetManager::linkUp (0x38 bytes, result unchecked); no counters yet. */
IOReturn YYavicAirWiFiLayerInterface::getWCL_TRAFFIC_COUNTERS(apple80211_wcl_traffic_counters *d)
{
    if (d == nullptr)
        return kIOReturnBadArgument;
    bzero(d, 0x38);
    return kIOReturnSuccess;
}

IOReturn YYavicAirWiFiLayerInterface::setWCL_LEAVE_NETWORK(apple80211_leave_network *)
{
    yy_trace("IF: setWCL_LEAVE_NETWORK\n");
    YYavicAirWiFiLayer *ctl = yy_ctrl_of(this);
    return ctl ? ctl->leaveNetwork() : kIOReturnSuccess;
}

#include "YYavicAirWiFiLayerInterface_stubs.cpp.inc"

static YYavicAirWiFiLayer *yy_ctrl_of(IO80211SkywalkInterface *)
{
    return gYYCtrlScan;
}

IOReturn YYavicAirWiFiLayerInterface::getOP_MODE(apple80211_opmode_data *d)
{
    if (d == nullptr)
        return kIOReturnBadArgument;
    d->version = APPLE80211_VERSION;
    d->op_mode = APPLE80211_M_STA;
    return kIOReturnSuccess;
}

IOReturn YYavicAirWiFiLayerInterface::getSUPPORTED_CHANNELS(apple80211_sup_channel_data *d)
{
    YYavicAirWiFiLayer *c = yy_ctrl_of(this);
    return c ? c->fillChannels(d) : kIOReturnNotReady;
}

IOReturn YYavicAirWiFiLayerInterface::getHW_SUPPORTED_CHANNELS(apple80211_sup_channel_data *d)
{
    YYavicAirWiFiLayer *c = yy_ctrl_of(this);
    return c ? c->fillChannels(d) : kIOReturnNotReady;
}

IOReturn YYavicAirWiFiLayerInterface::getCHANNELS_INFO(apple80211_channels_info *d)
{
    YYavicAirWiFiLayer *c = yy_ctrl_of(this);
    return c ? c->fillChannelsInfo(d) : kIOReturnNotReady;
}

IOReturn YYavicAirWiFiLayerInterface::getCOUNTRY_CHANNELS_INFO(apple80211_channels_info *d)
{
    YYavicAirWiFiLayer *c = yy_ctrl_of(this);
    return c ? c->fillChannelsInfo(d) : kIOReturnNotReady;
}

IOReturn YYavicAirWiFiLayerInterface::setWCL_ASSOCIATE(apple80211AssocCandidates *c)
{
    yy_trace("IF: setWCL_ASSOCIATE\n");
    YYavicAirWiFiLayer *ctl = yy_ctrl_of(this);
    return ctl ? ctl->joinStart((const uint8_t *)c) : kIOReturnNotReady;
}

IOReturn YYavicAirWiFiLayerInterface::setWCL_JOIN_ABORT(void *)
{
    yy_trace("IF: setWCL_JOIN_ABORT\n");
    YYavicAirWiFiLayer *ctl = yy_ctrl_of(this);
    return ctl ? ctl->joinAbort() : kIOReturnNotReady;
}

IOReturn YYavicAirWiFiLayerInterface::getCHANNEL(apple80211_channel_data *d)
{
    YYavicAirWiFiLayer *ctl = yy_ctrl_of(this);
    struct yy_radio_status st;
    if (d == nullptr || ctl == nullptr || ctl->radioStatus(&st) != kIOReturnSuccess ||
        st.state != YY_RADIO_S_RUN || st.channel == 0)
        return kIOReturnError;
    d->version = APPLE80211_VERSION;
    d->channel.version = APPLE80211_VERSION;
    d->channel.channel = st.channel;
    d->channel.flags = yy_chan_flags_w(st.channel, st.band, st.bss_bw, st.center);
    return kIOReturnSuccess;
}

IOReturn YYavicAirWiFiLayerInterface::getRSSI(apple80211_rssi_data *d)
{
    YYavicAirWiFiLayer *ctl = yy_ctrl_of(this);
    struct yy_radio_status st;
    if (d == nullptr || ctl == nullptr || ctl->radioStatus(&st) != kIOReturnSuccess ||
        st.state != YY_RADIO_S_RUN)
        return kIOReturnError;
    bzero(d, sizeof(*d));
    d->version = APPLE80211_VERSION;
    d->num_radios = 1;
    d->rssi_unit = APPLE80211_UNIT_DBM;
    d->rssi[0] = st.rssi_dbm;
    d->aggregate_rssi = st.rssi_dbm;
    d->rssi_ext[0] = st.rssi_dbm;
    d->aggregate_rssi_ext = st.rssi_dbm;
    return kIOReturnSuccess;
}

IOReturn YYavicAirWiFiLayerInterface::getRATE(apple80211_rate_data *d)
{
    YYavicAirWiFiLayer *ctl = yy_ctrl_of(this);
    struct yy_radio_status st;
    if (d == nullptr || ctl == nullptr || ctl->radioStatus(&st) != kIOReturnSuccess ||
        st.state != YY_RADIO_S_RUN)
        return kIOReturnError;
    bzero(d, sizeof(*d));
    d->version = APPLE80211_VERSION;
    d->num_radios = 1;
    /* MT7922 firmware does the TX rate control; report the HT/VHT rate the
     * AP is sending at (same link), else the legacy node rate. */
    uint32_t r = yy_rate_100k(st.rx_mode, st.rx_mcs, st.rx_nss, st.rx_bw, st.rx_sgi);
    d->rate[0] = r != 0 ? (r + 5) / 10 : (st.tx_rate_mbps ? st.tx_rate_mbps : 54);
    return kIOReturnSuccess;
}

/* Layouts from AppleBCMWLANCore::getMCS / getMCS_VHT / getNSS /
 * getMAX_NSS_FOR_AP (15.8.1): mcs +4; mcs_vht +4 mcs, +8 nss, +0xc width MHz,
 * +0x10 guard interval ns (800/400); nss +4 (u32); max_nss +4. */
static bool yy_link_rate(YYavicAirWiFiLayer *ctl, struct yy_radio_status *st)
{
    return ctl != nullptr && ctl->radioStatus(st) == kIOReturnSuccess && st->state == YY_RADIO_S_RUN &&
           st->rx_mode >= 2;
}

IOReturn YYavicAirWiFiLayerInterface::getMCS(apple80211_mcs_data *d)
{
    struct yy_radio_status st;
    if (d == nullptr)
        return kIOReturnBadArgument;
    if (!yy_link_rate(yy_ctrl_of(this), &st))
        return kIOReturnNotReady;
    uint8_t *b = (uint8_t *)d;
    *(uint32_t *)(b + 0) = APPLE80211_VERSION;
    *(uint32_t *)(b + 4) = st.rx_mcs;               /* HT: 0-31, VHT: 0-9, HE: 0-11 */
    return kIOReturnSuccess;
}

IOReturn YYavicAirWiFiLayerInterface::getMCS_VHT(apple80211_mcs_vht_data *d)
{
    struct yy_radio_status st;
    if (d == nullptr)
        return kIOReturnBadArgument;
    bool he;
    if (!yy_link_rate(yy_ctrl_of(this), &st) || (st.rx_mode != 4 && !(st.rx_mode >= 8 && st.rx_mode <= 11)))
        return kIOReturnNotReady;
    he = st.rx_mode >= 8;
    uint8_t *b = (uint8_t *)d;
    *(uint32_t *)(b + 0x0) = APPLE80211_VERSION;
    *(uint32_t *)(b + 0x4) = st.rx_mcs;
    *(uint32_t *)(b + 0x8) = st.rx_nss;
    *(uint32_t *)(b + 0xc) = 20u << (st.rx_bw & 3);
    *(uint32_t *)(b + 0x10) = he ? (st.rx_sgi == 2 ? 3200 : st.rx_sgi == 1 ? 1600 : 800)
                                 : (st.rx_sgi ? 400 : 800);
    return kIOReturnSuccess;
}

IOReturn YYavicAirWiFiLayerInterface::getNSS(apple80211_nss_data *d)
{
    struct yy_radio_status st;
    if (d == nullptr)
        return kIOReturnBadArgument;
    if (!yy_link_rate(yy_ctrl_of(this), &st))
        return kIOReturnNotReady;
    uint8_t *b = (uint8_t *)d;
    *(uint32_t *)(b + 0) = APPLE80211_VERSION;
    *(uint32_t *)(b + 4) = st.rx_mode >= 4 ? st.rx_nss : (uint32_t)(st.rx_mcs / 8 + 1);
    return kIOReturnSuccess;
}

IOReturn YYavicAirWiFiLayerInterface::getMAX_NSS_FOR_AP(apple80211_btcoex_max_nss_for_ap_data *d)
{
    if (d == nullptr)
        return kIOReturnBadArgument;
    uint8_t *b = (uint8_t *)d;
    *(uint32_t *)(b + 0) = APPLE80211_VERSION;
    *(uint32_t *)(b + 4) = 2;                       /* MT7922: 2x2 */
    return kIOReturnSuccess;
}

/*
 * airportd reads the roam profile inside an ASSOC request and aborts the join
 * with the read's error (0.3.0: END REQ [ASSOC] err=0xe00002c7). Layout from
 * AppleBCMWLANRoamAdapter::getROAM_PROFILE: 3 x 0x80-byte band records
 * (0x180 total), +0x4 band (0x4, 0x2, 0x400), +0xc = 1 only when the
 * firmware supplied a profile; profile fields left empty here as BCM does
 * when firmware has none.
 */
IOReturn YYavicAirWiFiLayerInterface::getROAM_PROFILE(apple80211_roam_profile_all_bands *p)
{
    static uint32_t n;
    if (n++ < 3)
        yy_trace("IF: getROAM_PROFILE\n");
    if (p == nullptr)
        return 0x16;
    uint8_t *b = (uint8_t *)p;
    bzero(b, 0x180);
    *(uint32_t *)(b + 0x000 + 0x4) = 0x4;
    *(uint32_t *)(b + 0x080 + 0x4) = 0x2;
    *(uint32_t *)(b + 0x100 + 0x4) = 0x400;
    return kIOReturnSuccess;
}

IOReturn YYavicAirWiFiLayerInterface::setWCL_SCAN_REQ(apple80211ScanRequest *)
{
    static uint32_t n;
    if (n++ < 10)
        yy_trace("IF: setWCL_SCAN_REQ #%u\n", n);
    YYavicAirWiFiLayer *c = yy_ctrl_of(this);
    return c ? c->startScan() : kIOReturnNotReady;
}

/* ------------------------------------------------------ skywalk (2b) -- */
/*
 * Bound by exact 15.8.1 export names: the MacKernelSDK typedefs for the queue
 * actions (IOReturn, const IOSkywalkPacket **) do not mangle to what
 * IOSkywalkFamily exports (unsigned int, IOSkywalkPacket * const *).
 * Member functions use the C ABI with `this` first.
 */
struct YYSkwPoolOptions {           /* IOSkywalkPacketBufferPool::PoolOptions */
    uint32_t packetCount;
    uint32_t bufferCount;
    uint32_t bufferSize;
    uint32_t maxBuffersPerPacket;
    uint32_t memorySegmentSize;
    uint32_t poolFlags;
    uint64_t reserved;
};
typedef uint32_t (*YYSkwTxSubAction)(OSObject *, void *queue, void *const *packets, uint32_t n, void *refcon);
typedef uint32_t (*YYSkwQAction)(OSObject *, void *queue, void **packets, uint32_t n, void *refcon);
extern "C" {
void *yy_skw_pool_withName(const char *, OSObject *, uint32_t, const YYSkwPoolOptions *)
    __asm("__ZN25IOSkywalkPacketBufferPool8withNameEPKcP8OSObjectjPKNS_11PoolOptionsE");
void yy_skw_pool_deallocatePacket(void *pool, void *packet)
    __asm("__ZN25IOSkywalkPacketBufferPool16deallocatePacketEP15IOSkywalkPacket");
void *yy_skw_txsub_withPool(void *, uint32_t, uint32_t, OSObject *, YYSkwTxSubAction, void *, uint32_t)
    __asm("__ZN26IOSkywalkTxSubmissionQueue8withPoolEP25IOSkywalkPacketBufferPooljjP8OSObjectPFjS3_PS_PKP15IOSkywalkPacketjPvES9_j");
void *yy_skw_txcomp_withPool(void *, uint32_t, uint32_t, OSObject *, YYSkwQAction, void *, uint32_t)
    __asm("__ZN26IOSkywalkTxCompletionQueue8withPoolEP25IOSkywalkPacketBufferPooljjP8OSObjectPFjS3_PS_PP15IOSkywalkPacketjPvES8_j");
void *yy_skw_rxcomp_withPool(void *, uint32_t, uint32_t, OSObject *, YYSkwQAction, void *, uint32_t)
    __asm("__ZN26IOSkywalkRxCompletionQueue8withPoolEP25IOSkywalkPacketBufferPooljjP8OSObjectPFjS3_PS_PP15IOSkywalkPacketjPvES8_j");
IOReturn yy_infra_registerInfraEthernetInterface(void *self, void *regInfo, void **queues, uint32_t n,
                                                 void *txPool, void *rxPool)
    __asm("__ZN21IO80211InfraInterface30registerInfraEthernetInterfaceEPN26IOSkywalkEthernetInterface16RegistrationInfoEPP20IOSkywalkPacketQueuejP25IOSkywalkPacketBufferPoolS7_");
/* Data path (15.8.1 exports; none of these is overridden by the concrete
 * packet / queue classes in use, so the direct calls equal the virtual ones). */
IOReturn yy_skw_pool_allocatePacket(void *pool, void **packet, uint32_t options)
    __asm("__ZN25IOSkywalkPacketBufferPool14allocatePacketEPP15IOSkywalkPacketj");
void *yy_skw_pkt_va(const void *packet)
    __asm("__ZNK15IOSkywalkPacket21getDataVirtualAddressEv");
uint16_t yy_skw_pkt_off(const void *packet)
    __asm("__ZNK15IOSkywalkPacket13getDataOffsetEv");
uint32_t yy_skw_pkt_len(const void *packet)
    __asm("__ZNK15IOSkywalkPacket13getDataLengthEv");
/* A pool packet has no buffers attached until prepared (0.4.2 panicked in
 * getDataVirtualAddress -> getMemorySegment(NULL)); RX direction is 2, as
 * IOSkywalkRxSubmissionQueue::refillRxRing (TX packets come prepared with 1). */
IOReturn yy_skw_pkt_prepareWithQueue(void *packet, void *queue, uint32_t direction, uint32_t flags)
    __asm("__ZN15IOSkywalkPacket16prepareWithQueueEP20IOSkywalkPacketQueuejj");
uint32_t yy_skw_pkt_bufcount(const void *packet)
    __asm("__ZNK15IOSkywalkPacket20getPacketBufferCountEv");
IOReturn yy_skw_pkt_setOffLen(void *packet, uint16_t off, uint32_t len)
    __asm("__ZN15IOSkywalkPacket22setDataOffsetAndLengthEtj");
IOReturn yy_skw_rxcomp_enqueue(void *queue, void *const *packets, uint32_t n, uint32_t options)
    __asm("__ZN26IOSkywalkRxCompletionQueue14enqueuePacketsEPKP15IOSkywalkPacketjj");
IOReturn yy_skw_txcomp_enqueue(void *queue, void *const *packets, uint32_t n, uint32_t options)
    __asm("__ZN26IOSkywalkTxCompletionQueue14enqueuePacketsEPKP15IOSkywalkPacketjj");
/* AppleBCMWLANSkywalkInterface::enableDatapath enables the queues itself and
 * then asks the submission queue to dequeue; until then the netif doorbell
 * (nxp_tx_qset_notify -> packetSubmission) finds the queue disabled. */
void yy_skw_txsub_enable(void *q) __asm("__ZN26IOSkywalkTxSubmissionQueue6enableEv");
void yy_skw_txcomp_enable(void *q) __asm("__ZN26IOSkywalkTxCompletionQueue6enableEv");
void yy_skw_rxcomp_enable(void *q) __asm("__ZN26IOSkywalkRxCompletionQueue6enableEv");
IOReturn yy_skw_txsub_requestDequeue(void *q, void *ctx, uint32_t flags)
    __asm("__ZN26IOSkywalkTxSubmissionQueue14requestDequeueEPvj");
IOReturn yy_skw_setLinkLayerAddress(void *self, ether_addr *ea)
    __asm("__ZN26IOSkywalkEthernetInterface19setLinkLayerAddressEP10ether_addr");
/* IO80211LinkState: 1 = down, 2 = up (AppleBCMWLANNetAdapter::setLinkDown/setLinkUp) */
bool yy_infra_setLinkState(void *self, uint32_t state, uint32_t a, bool debounce, uint32_t reason)
    __asm("__ZN21IO80211InfraInterface12setLinkStateE16IO80211LinkStatejbj");
}
extern "C" uint32_t yy_radio_tx_batch(IOService *radio, mbuf_t head);   /* radio part, same kext */
#define YY_TX_HEADROOM    128
#define YY_POOL_NOSLEEP   1        /* allocatePacket option bit 0 -> kern_pbufpool_alloc_nosleep */
#define YY_BATCH          32       /* < queue capacity (enqueuePackets needs n < capacity) */
#define YY_RXQ_MAX        1024

#define YY_SKW_QCAP     256
#define YY_SKW_BUFSIZE  2048

static void *gYYPool;

static YYavicAirWiFiLayer *gYYDataCtrl;

/*
 * IOSkywalkTxSubmissionQueue::legacyDequeue treats the return value as the
 * number of packets consumed (0 = stall). Runs on the work queue.
 */
static uint32_t yy_skw_tx_action(OSObject *, void *, void *const *packets, uint32_t n, void *)
{
    YYavicAirWiFiLayer *c = gYYDataCtrl;
    if (c != nullptr)
        return c->txSubmit(packets, n);
    for (uint32_t i = 0; i < n; i++)
        if (packets[i] != nullptr && gYYPool != nullptr)
            yy_skw_pool_deallocatePacket(gYYPool, packets[i]);
    return n;
}

static uint32_t yy_skw_rxcomp_action(OSObject *, void *, void **, uint32_t n, void *)
{
    static uint32_t calls;
    if (calls++ < 5)
        yy_trace("SKW: rx completion action n=%u\n", n);
    return 0;
}

static YYavicAirWiFiLayer *gYYCtrl;

static void yy_drvavail_request()
{
    if (gYYCtrl != nullptr)
        gYYCtrl->scheduleDriverAvailable(10);
}
static thread_call_t gYYBsdCall;
static uint32_t gYYBsdTries;

/*
 * IOSkywalkNetworkBSDClient::start also needs IOInterfaceUnit on the
 * interface, which configd's InterfaceNamer assigns only after the interface
 * is registered (0.1.8: deferBSDAttach(false) right away -> BSD client never
 * started). Apple calls deferBSDAttach(false) later, from
 * AppleBCMWLANCore::setupDriver; here: once IOInterfaceUnit is present.
 */
/*
 * Diagnostics for a BSD client that does not appear: list the interface's
 * clients, dump the fields IOSkywalkNetworkBSDClient::start checks
 * (+0xa8 IOSkywalkInterface ivars, +0xb8 IOSkywalkNetworkInterface ivars and
 * its first word, +0xc0), then instantiate and start the client by hand and
 * record the result.
 */
static void yy_bsd_verify(YYavicAirWiFiLayer *c)
{
    IOService *netif = c->fNetIf;
    bool haveClient = false;
    OSIterator *it = netif->getClientIterator();
    if (it != nullptr) {
        OSObject *o;
        while ((o = it->getNextObject()) != nullptr) {
            yy_trace("BSD: client %s\n", o->getMetaClass()->getClassName());
            if (strcmp(o->getMetaClass()->getClassName(), "IOSkywalkNetworkBSDClient") == 0)
                haveClient = true;
        }
        it->release();
    }
    uint8_t *b = (uint8_t *)netif;
    void *a8 = *(void **)(b + 0xa8), *b8 = *(void **)(b + 0xb8), *c0 = *(void **)(b + 0xc0);
    void *b8w0 = b8 ? *(void **)b8 : nullptr;
    OSObject *defer = netif->copyProperty("IODeferBSDAttach");
    OSObject *unit = netif->copyProperty("IOInterfaceUnit");
    OSObject *bsd = netif->copyProperty("BSD Name");
    yy_trace("BSD: +0xa8=%p +0xb8=%p [+0xb8]=%p +0xc0=%p defer=%s unit=%s bsdname=%s\n", a8, b8, b8w0, c0,
             defer ? defer->getMetaClass()->getClassName() : "none", unit ? "yes" : "none",
             bsd ? "yes" : "none");
    OSSafeReleaseNULL(defer);
    OSSafeReleaseNULL(unit);
    OSSafeReleaseNULL(bsd);
    if (haveClient) {
        yy_trace("BSD: IOSkywalkNetworkBSDClient present\n");
        c->scheduleDriverAvailable(200);  /* 0.6.3: as BCM at chip boot; off: -yyavicnodrvavail */
        return;
    }
    OSObject *o = OSMetaClass::allocClassWithName("IOSkywalkNetworkBSDClient");
    IOService *cl = OSDynamicCast(IOService, o);
    yy_trace("BSD: manual alloc = %p\n", cl);
    if (cl == nullptr) {
        OSSafeReleaseNULL(o);
        return;
    }
    if (!cl->init((OSDictionary *)nullptr)) {
        yy_trace("BSD: manual init FAILED\n");
        cl->release();
        return;
    }
    if (!cl->attach(netif)) {
        yy_trace("BSD: manual attach FAILED\n");
        cl->release();
        return;
    }
    /* Progress meter: start() writes IOInterfaceName (@0x2fb) and then
     * IOInterfaceType (@0x5b6) on the provider before the nexus alloc
     * (@0x64e). Remove both first, see which come back. */
    netif->removeProperty("IOInterfaceName");
    netif->removeProperty("IOInterfaceType");
    uint64_t t0 = mach_absolute_time();
    bool ok = cl->start(netif);
    uint64_t ns;
    absolutetime_to_nanoseconds(mach_absolute_time() - t0, &ns);
    OSObject *pn = netif->copyProperty("IOInterfaceName");
    OSObject *pt = netif->copyProperty("IOInterfaceType");
    yy_trace("BSD: manual start = %d (%llu ms) IOInterfaceName=%s IOInterfaceType=%s\n", ok,
             ns / 1000000ULL, pn ? "set" : "none", pt ? "set" : "none");
    OSSafeReleaseNULL(pn);
    OSSafeReleaseNULL(pt);
    if (!ok)
        cl->detach(netif);
    else
        cl->registerService();
    cl->release();
    bsd = netif->copyProperty("BSD Name");
    yy_trace("BSD: after manual start bsdname=%s\n", bsd ? "yes" : "none");
    OSSafeReleaseNULL(bsd);
    if (ok)
        return;

    /*
     * Experiment: IOSkywalkLegacyEthernet::probe refuses a provider that has
     * IOInterfaceUnit, i.e. the shim only exists for naming before a unit is
     * assigned. If it still holds "en3", the netif nexus alloc would get
     * EBUSY. Terminate the shim and retry once.
     */
    IOService *legacy = nullptr;
    it = netif->getClientIterator();
    if (it != nullptr) {
        OSObject *c2;
        while ((c2 = it->getNextObject()) != nullptr)
            if (strcmp(c2->getMetaClass()->getClassName(), "IOSkywalkLegacyEthernet") == 0)
                legacy = OSDynamicCast(IOService, c2);
        if (legacy)
            legacy->retain();
        it->release();
    }
    if (legacy == nullptr) {
        yy_trace("BSD: no legacy shim to terminate\n");
        return;
    }
    bool tr = legacy->terminate(kIOServiceSynchronous);
    legacy->release();
    yy_trace("BSD: legacy shim terminate = %d\n", tr);
    IOSleep(1000);
    o = OSMetaClass::allocClassWithName("IOSkywalkNetworkBSDClient");
    cl = OSDynamicCast(IOService, o);
    if (cl == nullptr || !cl->init((OSDictionary *)nullptr) || !cl->attach(netif)) {
        yy_trace("BSD: retry alloc/init/attach FAILED\n");
        OSSafeReleaseNULL(o);
        return;
    }
    t0 = mach_absolute_time();
    ok = cl->start(netif);
    absolutetime_to_nanoseconds(mach_absolute_time() - t0, &ns);
    yy_trace("BSD: retry start after shim terminate = %d (%llu ms)\n", ok, ns / 1000000ULL);
    if (!ok)
        cl->detach(netif);
    else
        cl->registerService();
    cl->release();
    bsd = netif->copyProperty("BSD Name");
    yy_trace("BSD: after retry bsdname=%s\n", bsd ? "yes" : "none");
    OSSafeReleaseNULL(bsd);
}

static void yy_bsd_poll(thread_call_param_t, thread_call_param_t)
{
    YYavicAirWiFiLayer *c = gYYCtrl;
    if (c == nullptr || c->fNetIf == nullptr)
        return;
    if (gYYBsdTries == 1000) {
        yy_bsd_verify(c);
        return;
    }
    OSObject *unit = c->fNetIf->copyProperty("IOInterfaceUnit");
    if (unit != nullptr) {
        OSNumber *n = OSDynamicCast(OSNumber, unit);
        yy_trace("SKW: IOInterfaceUnit=%u after %u polls -> deferBSDAttach(false)\n",
                 n ? n->unsigned32BitValue() : 0xffff, gYYBsdTries);
        unit->release();
        c->fNetIf->deferBSDAttach(false);
        yy_trace("SKW: deferBSDAttach(false) done\n");
        gYYBsdTries = 1000;     /* next fire = verification phase */
        uint64_t dl;
        clock_interval_to_deadline(1500, kMillisecondScale, &dl);
        thread_call_enter_delayed(gYYBsdCall, dl);
        return;
    }
    if (++gYYBsdTries >= 60) {
        yy_trace("SKW: no IOInterfaceUnit after 30 s, giving up (use yy-bsd)\n");
        return;
    }
    uint64_t deadline;
    clock_interval_to_deadline(500, kMillisecondScale, &deadline);
    thread_call_enter_delayed(gYYBsdCall, deadline);
}

static void yy_bsd_schedule(uint32_t delayMs)
{
    if (gYYBsdCall == nullptr)
        gYYBsdCall = thread_call_allocate(yy_bsd_poll, nullptr);
    gYYBsdTries = 0;
    uint64_t deadline;
    clock_interval_to_deadline(delayMs, kMillisecondScale, &deadline);
    thread_call_enter_delayed(gYYBsdCall, deadline);
}

/* ------------------------------------------------- legacy BSD iface -- */
OSDefineMetaClassAndStructors(YYavicAirWiFiLayerBSDInterface, IOEthernetInterface)

bool YYavicAirWiFiLayerBSDInterface::initWithSkywalk(IONetworkController *controller,
                                                     YYavicAirWiFiLayerInterface *sky)
{
    fSky = sky;
    fAttached = false;
    return IOEthernetInterface::init(controller);
}

IOReturn YYavicAirWiFiLayerBSDInterface::attachToDataLinkLayer(IOOptionBits options, void *parameter)
{
    IOReturn ret = IOEthernetInterface::attachToDataLinkLayer(options, parameter);
    yy_trace("BSDIF: attachToDataLinkLayer = 0x%x ifnet=%p\n", ret, getIfnet());
    if (ret == kIOReturnSuccess && fSky != nullptr && getIfnet() != nullptr) {
        char name[IFNAMSIZ];
        UInt8 builtIn = 0;
        snprintf(name, sizeof(name), "%s%u", ifnet_name(getIfnet()), ifnet_unit(getIfnet()));
        fSky->setProperty("built-in", OSData::withBytes(&builtIn, sizeof(builtIn)));
        fSky->setProperty("IOInterfaceName", OSString::withCString(name));
        fSky->setProperty(kIOInterfaceUnit, OSNumber::withNumber(ifnet_unit(getIfnet()), 8));
        fSky->setProperty(kIOInterfaceNamePrefix, OSString::withCString(ifnet_name(getIfnet())));
        IOEthernetAddress addr;
        if (OSDynamicCast(IOEthernetController, getController())->getHardwareAddress(&addr) == kIOReturnSuccess)
            setProperty(kIOMACAddress, (void *)&addr, kIOEthernetAddressSize);
        yy_trace("BSDIF: %s -> Skywalk interface registerService\n", name);
        fSky->registerService();
        bool pr = fSky->prepareBSDInterface((__ifnet *)getIfnet(), 0);
        yy_trace("BSDIF: prepareBSDInterface = %d\n", pr);
    }
    fAttached = true;
    return ret;
}

void YYavicAirWiFiLayerBSDInterface::detachFromDataLinkLayer(IOOptionBits options, void *parameter)
{
    yy_trace("BSDIF: detachFromDataLinkLayer\n");
    IOEthernetInterface::detachFromDataLinkLayer(options, parameter);
    fAttached = false;
}

/* As AirportItlwm: once attached, report the Skywalk interface as provider
 * (IOSkywalkFamily casts the ifnet's provider to IOSkywalkNetworkInterface). */
IOService *YYavicAirWiFiLayerBSDInterface::getProvider() const
{
    return fAttached ? (IOService *)fSky : IOEthernetInterface::getProvider();
}

/* ----------------------------------------------------------- controller -- */
OSDefineMetaClassAndStructors(YYavicAirWiFiLayer, IO80211Controller)

bool YYavicAirWiFiLayer::init(OSDictionary *props)
{
    if (!IO80211Controller::init(props))
        return false;
    fNetIf = nullptr;
    fBsdIf = nullptr;
    fRadio = nullptr;
    fScanTimer = nullptr;
    fDrvAvailTimer = nullptr;
    fScanBuf = nullptr;
    fScanCount = 0;
    fRxLock = nullptr;
    fRxHead = fRxTail = nullptr;
    fRxQLen = 0;
    fRxES = nullptr;
    fSymTx = nullptr;
    fDataAttached = false;
    fRadioMacSet = false;
    fLinkDownTicks = 0;
    fRxIn = fRxEnq = fRxDrop = fTxIn = fTxOk = fTxDrop = 0;
    fRxAbs = fTxAbs = 0;
    fTxCopyAbs = fTxRadioAbs = fTxCompAbs = 0;
    fLinkTicks = 0;
    fLinkRsn = false;
    fScanBusy = false;
    fJoinTimer = nullptr;
    fJoinTicks = 0;
    fJoinActive = false;
    fLinked = false;
    bzero(fJoinBssid, sizeof(fJoinBssid));
    fPool = fTxSubQ = fTxCompQ = fRxCompQ = nullptr;
    fCCPipe = fCCStream = fFaultReporter = fIOFaultReporter = nullptr;
    fPowerOn = 1;
    /* Locally administered placeholder MAC until the radio driver is wired in. */
    static const uint8_t mac[6] = {0x02, 0x59, 0x59, 0x7a, 0x92, 0x22};
    memcpy(fMac.octet, mac, sizeof(mac));
    return true;
}

IOService *YYavicAirWiFiLayer::probe(IOService *provider, SInt32 *score)
{
    if (!gYYArmed) {
        yy_trace("CTRL: probe REFUSED (not armed) provider=%s\n", provider ? provider->getName() : "-");
        yy_trace_bt("CTRL: probe caller");
        return nullptr;
    }
    return IO80211Controller::probe(provider, score);
}

bool YYavicAirWiFiLayer::start(IOService *provider)
{
    if (!gYYArmed) {
        yy_trace("CTRL: start REFUSED (not armed)\n");
        return false;
    }
    yy_trace("CTRL: start provider=%s\n", provider ? provider->getName() : "-");
    UInt8 builtIn = 0;
    setProperty("built-in", OSData::withBytes(&builtIn, sizeof(builtIn)));
    setProperty("DriverKitDriver", kOSBooleanFalse);

    if (!IO80211Controller::start(provider)) {
        yy_trace("CTRL: IO80211Controller::start FAILED\n");
        return false;
    }
    yy_trace("CTRL: IO80211Controller::start ok\n");
    gYYCtrlScan = this;
    if (!createMediumTables())
        return false;

    fNetIf = OSTypeAlloc(YYavicAirWiFiLayerInterface);
    if (fNetIf == nullptr) {
        yy_trace("CTRL: interface alloc FAILED\n");
        return false;
    }
    fNetIf->yyRegInfo = nullptr;
    /* The AP only knows the radio's own address, and configd already names
     * this interface by fMac (en3) while the radio's address belongs to its
     * own interface (en2): give the radio fMac instead (ABI 3). */
    adoptRadioMac(false);
    /* Apple order (AppleBCMWLANSkywalkInterface::initWithProviderAndParameters):
     * virtual init() first -> IO80211InfraInterface::init() allocates +0x118. */
    bool ok = fNetIf->initInfra();
    yy_trace("CTRL: interface init() = %d\n", ok);
    if (!ok)
        return false;
    /* With ivars present this only re-runs initIvars() and stores the MAC. */
    ok = static_cast<IO80211SkywalkInterface *>(fNetIf)->init(this, &fMac);
    yy_trace("CTRL: interface init(this, mac) = %d\n", ok);
    if (!ok)
        return false;
    fNetIf->setInterfaceRole(1);
    fNetIf->setInterfaceId(1);
    yy_trace("CTRL: role/id set, role=%u\n", (unsigned)fNetIf->getInterfaceRole());

    ok = fNetIf->attach(this);
    yy_trace("CTRL: interface attach = %d\n", ok);
    if (!ok)
        return false;
    ok = attachInterface(fNetIf, this);
    yy_trace("CTRL: attachInterface = %d\n", ok);
    if (!ok)
        return false;

    if (gYYMode == 0) {
        /* AirportItlwm order: classic interface is attached (and registered)
         * right after the Skywalk interface. */
        fBsdIf = nullptr;
        ok = IONetworkController::attachInterface((IONetworkInterface **)&fBsdIf, true);
        yy_trace("CTRL: legacy attachInterface(bsd) = %d bsd=%p\n", ok, fBsdIf);
        if (!ok)
            return false;
    }

    if (fNetIf->yyRegInfo == nullptr)
        fNetIf->yyRegInfo = (IOSkywalkEthernetInterface::RegistrationInfo *)IOMallocZero(0x130);
    ok = fNetIf->initRegistrationInfo(fNetIf->yyRegInfo, 1, 0x130);
    yy_trace("CTRL: initRegistrationInfo = %d\n", ok);
    {
        /* As AppleBCMWLANCore::createSkywalkInterface fills it after
         * initRegistrationInfo (15.8.1). +0xc is the ifnet subfamily
         * (IOSkywalkNetworkInterface::getInterfaceSubFamily returns
         * [[+0xb8]]->+0xc): 3 = IFNET_SUBFAMILY_WIFI. 0.1.14 left it 0, so en3
         * was "type: Ethernet" and Apple80211 did not list it. */
        uint8_t *ri = (uint8_t *)fNetIf->yyRegInfo;
        *(uint32_t *)(ri + 0x0c) = 3;
        *(uint32_t *)(ri + 0x14) = 2;
        *(uint64_t *)(ri + 0x24) = 0x8000000080ULL;
        *(uint32_t *)(ri + 0x40) = 0;
        memcpy(ri + 0x108, fMac.octet, 6);
    }

    if (gYYMode == 0) {
        /* IOSkywalkNetworkInterface::mExpansionData (+0xb8) and
         * IOSkywalkEthernetInterface::mExpansionData2 (+0x108), both
         * allocated by init(), start with fRegistrationInfo (15.8.1:
         * IOSkywalkEthernetInterface::init stores +0x108,
         * registerEthernetInterface reads [[+0x108]], the BSD client checks
         * [[+0xb8]]). No registerInfraEthernetInterface in this mode, so set
         * them like AirportItlwm does. */
        uint8_t *b = (uint8_t *)fNetIf;
        void **exp1 = *(void ***)(b + 0xb8);
        void **exp2 = *(void ***)(b + 0x108);
        yy_trace("CTRL: expansion +0xb8=%p [0]=%p +0x108=%p [0]=%p\n", exp1, exp1 ? exp1[0] : nullptr,
                 exp2, exp2 ? exp2[0] : nullptr);
        if (exp1 != nullptr && exp1[0] == nullptr) {
            void *r = IOMallocZero(0x130);
            if (r) { memcpy(r, fNetIf->yyRegInfo, 0x130); exp1[0] = r; }
        }
        if (exp2 != nullptr && exp2[0] == nullptr) {
            void *r = IOMallocZero(0x130);
            if (r) { memcpy(r, fNetIf->yyRegInfo, 0x130); exp2[0] = r; }
        }
    }

    fNetIf->deferBSDAttach(true);
    ok = fNetIf->start(this);
    yy_trace("CTRL: interface start = %d\n", ok);
    if (!ok)
        return false;

    if (gYYMode == 1) {
        ok = setupSkywalkDatapath();
        yy_trace("CTRL: skywalk datapath = %d\n", ok);
    } else {
        yy_trace("CTRL: legacy mode, BSD via classic interface\n");
    }

    setLinkStatus(kIONetworkLinkValid);
    registerService();
    yy_trace("CTRL: registered\n");
    return true;
}

/*
 * Apple order (AppleBCMWLANSkywalkInterface::start, 15.8.1): after
 * IO80211InfraInterface::start, add the queues to the work queue
 * (IOWorkLoop::addEventSource), then
 * registerInfraEthernetInterface(regInfo, {TxSub x N, TxComp, RxComp}, N + 2,
 * txPool, rxPool), registerService(kIOServiceAsynchronous),
 * setRunningState(false). Pool options as AppleConvergedIPCSkywalkInterface.
 */
bool YYavicAirWiFiLayer::setupSkywalkDatapath()
{
    YYSkwPoolOptions opts;
    bzero(&opts, sizeof(opts));
    opts.packetCount = 4 * YY_SKW_QCAP;
    opts.bufferCount = 4 * YY_SKW_QCAP;
    opts.bufferSize = YY_SKW_BUFSIZE;
    opts.maxBuffersPerPacket = 1;
    /* AppleBCMWLANSkywalkPacketPool::initWithName passes packetType 1 and
     * AppleBCMWLANPCIeSkywalk::allocSkywalkCommonResources poolFlags 1.
     * With packetType 0 (0.1.7-0.1.12) IOSkywalkNetworkBSDClient::start got
     * as far as the netif nexus alloc and failed there immediately. */
    opts.poolFlags = 1;
    fPool = yy_skw_pool_withName("YYavicAirWiFiLayer", fNetIf, 1, &opts);
    gYYPool = fPool;
    yy_trace("SKW: pool = %p (%s)\n", fPool, fPool ? ((OSObject *)fPool)->getMetaClass()->getClassName() : "-");
    if (fPool == nullptr)
        return false;

    fTxSubQ = yy_skw_txsub_withPool(fPool, YY_SKW_QCAP, 0, fNetIf, yy_skw_tx_action, nullptr, 0);
    fTxCompQ = yy_skw_txcomp_withPool(fPool, YY_SKW_QCAP, 0, fNetIf, nullptr, nullptr, 0);
    fRxCompQ = yy_skw_rxcomp_withPool(fPool, YY_SKW_QCAP, 0, fNetIf, yy_skw_rxcomp_action, nullptr, 0);
    yy_trace("SKW: txsub=%p (%s) txcomp=%p rxcomp=%p\n", fTxSubQ,
             fTxSubQ ? ((OSObject *)fTxSubQ)->getMetaClass()->getClassName() : "-", fTxCompQ, fRxCompQ);
    if (fTxSubQ == nullptr || fTxCompQ == nullptr || fRxCompQ == nullptr)
        return false;

    IOWorkLoop *wl = (IOWorkLoop *)gYYWorkQueue;
    IOReturn r1 = wl->addEventSource((IOEventSource *)fTxSubQ);
    IOReturn r2 = wl->addEventSource((IOEventSource *)fTxCompQ);
    IOReturn r3 = wl->addEventSource((IOEventSource *)fRxCompQ);
    yy_trace("SKW: addEventSource = 0x%x 0x%x 0x%x\n", r1, r2, r3);

    static IOSkywalkEthernetInterface::RegistrationInfo *reg;
    reg = fNetIf->yyRegInfo;
    /* registerInfraEthernetInterface takes the link address from
     * RegistrationInfo +0x108..+0x10d (0.1.7 left it zero -> IOMACAddress 0). */
    memcpy((uint8_t *)reg + 0x108, fMac.octet, 6);
    void *queues[3] = {fTxSubQ, fTxCompQ, fRxCompQ};
    IOReturn rr = yy_infra_registerInfraEthernetInterface(fNetIf, reg, queues, 3, fPool, fPool);
    yy_trace("SKW: registerInfraEthernetInterface = 0x%x\n", rr);
    if (rr != kIOReturnSuccess)
        return false;

    fNetIf->registerService(kIOServiceAsynchronous);
    SInt32 rs = fNetIf->setRunningState(false);
    yy_trace("SKW: interface registerService(async), setRunningState(false) = 0x%x\n", (unsigned)rs);
    /* IOSkywalkNetworkBSDClient::start skips a provider with
     * IODeferBSDAttach=true; deferBSDAttach(false) removes it and calls
     * registerService(0) (AppleBCMWLANCore::setupDriver does this once the
     * driver is ready). This creates the BSD interface airportd talks to. */
    gYYCtrl = this;
    yy_bsd_schedule(500);
    yy_trace("SKW: BSD attach deferred until IOInterfaceUnit is assigned\n");
    return true;
}

IONetworkInterface *YYavicAirWiFiLayer::createInterface()
{
    YYavicAirWiFiLayerBSDInterface *netif = OSTypeAlloc(YYavicAirWiFiLayerBSDInterface);
    if (netif == nullptr)
        return nullptr;
    if (!netif->initWithSkywalk(this, fNetIf)) {
        netif->release();
        return nullptr;
    }
    yy_trace("CTRL: createInterface -> %p\n", netif);
    return netif;
}

bool YYavicAirWiFiLayer::configureInterface(IONetworkInterface *netif)
{
    bool ok = IO80211Controller::configureInterface(netif);
    yy_trace("CTRL: configureInterface = %d\n", ok);
    return ok;
}

/* No radio in this layer yet: drop. */
UInt32 YYavicAirWiFiLayer::outputPacket(mbuf_t m, void *)
{
    static uint32_t n;
    if (n++ < 3)
        yy_trace("CTRL: outputPacket (dropped, no radio)\n");
    if (m != nullptr)
        freePacket(m);
    return kIOReturnOutputDropped;
}

/*
 * IO80211SkywalkInterface::getSupportedMediaArray / handleChosenMedia read the
 * controller's medium dictionary (copyMediumDictionary). Without one the BSD
 * media ioctl (SIOCGIFXMEDIA) on the interface fails with EIO, and Apple80211
 * does not count the interface as Wi-Fi (0.1.13: _getIfListCopy count[0]).
 * Same table as AirportItlwm.
 */
bool YYavicAirWiFiLayer::createMediumTables()
{
    OSDictionary *dict = OSDictionary::withCapacity(2);
    if (dict == nullptr)
        return false;
    IONetworkMedium *primary = IONetworkMedium::medium(kIOMediumIEEE80211, 54000000);
    IONetworkMedium::addMedium(dict, primary);
    IONetworkMedium *none = IONetworkMedium::medium(kIOMediumIEEE80211None, 0);
    IONetworkMedium::addMedium(dict, none);
    OSSafeReleaseNULL(none);
    bool ok = publishMediumDictionary(dict);
    dict->release();
    if (ok && primary != nullptr) {
        setCurrentMedium(primary);
        setSelectedMedium(primary);
    }
    OSSafeReleaseNULL(primary);
    yy_trace("CTRL: medium tables published = %d\n", ok);
    return ok;
}

IOReturn YYavicAirWiFiLayer::selectMedium(const IONetworkMedium *medium)
{
    yy_trace("CTRL: selectMedium\n");
    setSelectedMedium(medium);
    return kIOReturnSuccess;
}

/* ------------------------------------------------------ radio + scan -- */
/*
 * Sequoia (15.x) scan path, from AppleBCMWLANScanAdapter (15.8.1):
 *  setWCL_SCAN_REQ -> start scan; each BSS:
 *    IO80211Controller::postMessage(infraIf, 206, BeaconMetaData(0x40) + IEs, 0x40 + ieLen, true)
 *  done: postMessage(infraIf, 237, &u32 status (0 = ok), 4, true)
 * BeaconMetaData layout (producer AppleBCMWLANBSSBeacon::getBeaconMsgFromWLBSSInfo,
 * consumer IO80211BSSBeacon::setBeaconDataFromMsg): +0 ieLen, +4 AppleChannelSpec,
 * +6 SSID[32], +0x26 ssidLen, +0x27 primary channel, +0x29 BSSID, +0x30 s32 RSSI,
 * +0x34 s16 noise, +0x36 u16 SNR, +0x38 beacon period, +0x3a capability,
 * +0x3c flags (0x4000 RSSI valid, 0x2000 SNR valid, 0x1000 noise valid).
 * AppleChannelSpec_t: chan | bw (0x1000 = 20 MHz) | band (0 = 2.4, 0xc000 = 5 GHz).
 */
#define YY_MSG_WCL_BEACON     206
#define YY_MSG_WCL_SCAN_DONE  237
#define YY_SCAN_MAX           64
#define YY_SCAN_WAIT_MS       2500
#define YY_SCAN_LINKED_MS     300       /* associated: cached results only */
#define YY_NOISE_DBM          (-95)

extern "C" void yy_ctrl_postMessage(void *ctrl, void *iface, unsigned int msg, void *data,
                                    unsigned long len, bool b)
    __asm("__ZN17IO80211Controller11postMessageEP23IO80211SkywalkInterfacejPvmb");


IOService *YYavicAirWiFiLayer::radio()
{
    if (fRadio != nullptr)
        return fRadio;
    OSDictionary *m = IOService::serviceMatching("YYavicMtkWlan");
    if (m == nullptr)
        return nullptr;
    IOService *r = IOService::copyMatchingService(m);
    m->release();
    if (r == nullptr) {
        yy_trace("RADIO: YYavicMtkWlan not found\n");
        return nullptr;
    }
    uint32_t abi = 0;
    IOReturn ret = r->callPlatformFunction(YY_RADIO_FN_ABI, false, &abi, nullptr, nullptr, nullptr);
    if (ret != kIOReturnSuccess || abi < 1) {
        yy_trace("RADIO: bridge missing or wrong ABI (ret=0x%x abi=%u)\n", ret, abi);
        r->release();
        return nullptr;
    }
    yy_trace("RADIO: bridge ABI %u ok\n", abi);
    fRadio = r;     /* keep the reference */
    return fRadio;
}

static uint32_t yy_chan_flags(uint8_t ch, uint8_t band)
{
    uint32_t f = 0x2;                               /* 20 MHz */
    if (band == YY_RADIO_BAND_5G) {
        f |= 0x10;
        if (ch >= 52 && ch <= 144)
            f |= 0x100;                             /* DFS */
    } else {
        f |= 0x8;
    }
    return f;
}

/* apple80211 channel flags for the operating width (0 = 20 .. 3 = 160 MHz). */
static uint32_t yy_chan_flags_w(uint8_t ch, uint8_t band, uint8_t bw, uint8_t center)
{
    uint32_t f = yy_chan_flags(ch, band) & ~0x2u;
    switch (bw) {
    case 1:  f |= 0x4 | (center > ch ? 0x200 : 0); break;       /* 40, EXT_ABV */
    case 2:
    case 3:  f |= 0x400; break;                                  /* 80 (and 160) */
    default: f |= 0x2; break;
    }
    return f;
}

/*
 * Broadcom chanspec (AppleChannelSpec_t): channel = center for 40/80/160,
 * bits 8-10 = 20 MHz sub-band of the primary, 0x1000/0x1800/0x2000/0x2800 =
 * 20/40/80/160 MHz, 0xc000 = 5 GHz.
 */
static uint16_t yy_chanspec(uint8_t ch, uint8_t band, uint8_t bw, uint8_t center)
{
    uint16_t b = band == YY_RADIO_BAND_5G ? 0xc000 : 0;
    static const uint16_t w[4] = { 0x1000, 0x1800, 0x2000, 0x2800 };
    static const uint8_t half[4] = { 0, 2, 6, 14 };
    if (bw > 3 || bw == 0 || center == 0 || ch + half[bw] < center || ch > center + half[bw])
        return (uint16_t)(ch | 0x1000 | b);
    uint16_t sb = (uint16_t)((ch - (center - half[bw])) / 4);
    return (uint16_t)(center | (sb << 8) | w[bw] | b);
}

/* Turkey (ETSI): 2.4 GHz 1-13, 5 GHz 36-64 and 100-140. The radio's table
 * also had 14 and 149-177. */
static bool yy_chan_allowed(uint8_t ch, uint8_t band)
{
    if (band != YY_RADIO_BAND_5G)
        return ch >= 1 && ch <= 13;
    return (ch >= 36 && ch <= 64) || (ch >= 100 && ch <= 140);
}

/* HT/VHT data rate in 100 kbit/s for MCS 0-9, 1 stream, 800 ns GI; HE
 * (modes 8-11) for HE-MCS 0-11, 1 stream, 0.8/1.6/3.2 us GI. */
static uint32_t yy_rate_100k(uint8_t mode, uint8_t mcs, uint8_t nss, uint8_t bw, uint8_t sgi)
{
    static const uint16_t r20[10] = { 65, 130, 195, 260, 390, 520, 585, 650, 780, 867 };
    static const uint16_t r40[10] = { 135, 270, 405, 540, 810, 1080, 1215, 1350, 1620, 1800 };
    static const uint16_t r80[10] = { 293, 585, 878, 1170, 1755, 2340, 2633, 2925, 3510, 3900 };
    /* HE SU, 0.8 us GI, 1 stream (IEEE 802.11ax Table 27-55..27-58), x10 */
    static const uint16_t h20[12] = { 86, 172, 258, 344, 516, 688, 774, 860, 1032, 1147, 1290, 1434 };
    static const uint16_t h40[12] = { 172, 344, 516, 688, 1032, 1376, 1549, 1721, 2065, 2294, 2581, 2868 };
    static const uint16_t h80[12] = { 360, 721, 1081, 1441, 2162, 2882, 3243, 3603, 4324, 4804, 5404, 6005 };
    if (mode >= 8 && mode <= 11) {
        if (mcs > 11 || nss == 0 || nss > 4)
            return 0;
        uint32_t r = bw == 0 ? h20[mcs] : bw == 1 ? h40[mcs] : bw == 2 ? h80[mcs] : 2u * h80[mcs];
        r *= nss;
        if (sgi == 1)                               /* 1.6 us: symbol 13.6 -> 14.4 us */
            r = r * 136 / 144;
        else if (sgi == 2)                          /* 3.2 us: 16.0 us */
            r = r * 136 / 160;
        return r;
    }
    if (mode == 2 || mode == 3) {                   /* HT: MCS 0-31 carries the streams */
        nss = mcs / 8 + 1;
        mcs %= 8;
    }
    if (mcs > 9 || nss == 0 || nss > 4 || mode < 2)
        return 0;
    uint32_t r = bw == 0 ? r20[mcs] : bw == 1 ? r40[mcs] : bw == 2 ? r80[mcs] : 2u * r80[mcs];
    r *= nss;
    if (sgi)
        r = r * 10 / 9;
    return r;
}

IOReturn YYavicAirWiFiLayer::fillChannels(apple80211_sup_channel_data *d)
{
    if (d == nullptr)
        return kIOReturnBadArgument;
    IOService *r = radio();
    if (r == nullptr)
        return kIOReturnNotReady;
    struct yy_radio_chan ch[APPLE80211_MAX_CHANNELS];
    uint32_t n = APPLE80211_MAX_CHANNELS;
    IOReturn ret = r->callPlatformFunction(YY_RADIO_FN_CHANNELS, false, ch, &n, nullptr, nullptr);
    if (ret != kIOReturnSuccess) {
        static uint32_t e;
        if (e++ < 3)
            yy_trace("RADIO: channels FAILED 0x%x\n", ret);
        return ret;
    }
    d->version = APPLE80211_VERSION;
    d->num_channels = 0;
    for (uint32_t i = 0; i < n && d->num_channels < APPLE80211_MAX_CHANNELS; i++) {
        if (!yy_chan_allowed(ch[i].channel, ch[i].band))
            continue;
        uint32_t k = d->num_channels;
        d->supported_channels[k].version = APPLE80211_VERSION;
        d->supported_channels[k].channel = ch[i].channel;
        d->supported_channels[k].flags = yy_chan_flags(ch[i].channel, ch[i].band);
        d->num_channels++;
    }
    static uint32_t once;
    if (once++ < 2)
        yy_trace("CTRL: channels = %u\n", d->num_channels);
    return kIOReturnSuccess;
}

/*
 * apple80211_channels_info (0x2720 bytes, 15.8.1), as AppleBCMWLANCore keeps
 * it at ivars+0x4d08 and returns it from getCHANNELS_INFO (memcpy 0x2720);
 * filled by AppleBCMWLANCore::handleChanInfoTimer:
 *   +0x08 country code (3 chars), +0x0c u16 count (<= 400),
 *   +0x0e u16 chanspec[400], +0x32e u8 channel[400], +0xc90 u32 chanspec[400],
 *   +0x12d0 u32 per_chan_info[400] (Broadcom bits: 0x1|0x2 valid hw/sw,
 *   0x4 5 GHz, 0x8 radar, 0x20 passive; only (info & 3) == 3 is listed),
 *   +0x1aa0 u8 is5GHz[400].
 * 0.2.0 left it unsupported and airportd scanned "with channels {()}".
 */
#define YY_CHINFO_SIZE   0x2720
#define YY_CHINFO_MAX    400

IOReturn YYavicAirWiFiLayer::fillChannelsInfo(void *info)
{
    if (info == nullptr)
        return kIOReturnBadArgument;
    IOService *r = radio();
    if (r == nullptr)
        return kIOReturnNotReady;
    static struct yy_radio_chan ch[YY_CHINFO_MAX];
    uint32_t n = YY_CHINFO_MAX;
    IOReturn ret = r->callPlatformFunction(YY_RADIO_FN_CHANNELS, false, ch, &n, nullptr, nullptr);
    if (ret != kIOReturnSuccess) {
        static uint32_t e;
        if (e++ < 3)
            yy_trace("RADIO: channels_info FAILED 0x%x\n", ret);
        return ret;
    }
    uint8_t *b = (uint8_t *)info;
    bzero(b, YY_CHINFO_SIZE);
    b[0x08] = 'T';
    b[0x09] = 'R';
    uint16_t cnt = 0;
    for (uint32_t i = 0; i < n && cnt < YY_CHINFO_MAX; i++) {
        if (!yy_chan_allowed(ch[i].channel, ch[i].band))
            continue;
        bool g5 = ch[i].band == YY_RADIO_BAND_5G;
        uint8_t c = ch[i].channel;
        bool dfs = g5 && c >= 52 && c <= 144;
        uint16_t spec = (uint16_t)(c | 0x1000 | (g5 ? 0xc000 : 0));
        uint32_t pci = 0x3 | (g5 ? 0x4 : 0) | (dfs ? 0x8 : 0) | ((dfs || ch[i].passive) ? 0x20 : 0);
        *(uint16_t *)(b + 0x0e + 2 * cnt) = spec;
        b[0x32e + cnt] = c;
        *(uint32_t *)(b + 0xc90 + 4 * cnt) = spec;
        *(uint32_t *)(b + 0x12d0 + 4 * cnt) = pci;
        b[0x1aa0 + cnt] = g5 ? 1 : 0;
        cnt++;
    }
    *(uint16_t *)(b + 0x0c) = cnt;
    static uint32_t once;
    if (once++ < 3)
        yy_trace("CTRL: channels_info count=%u\n", cnt);
    return kIOReturnSuccess;
}

/*
 * APPLE80211_M_DRIVER_AVAILABLE (55), as AppleBCMWLANCore::bootChipImage
 * posts it once the firmware is up (15.8.1): 0xf8 bytes, +0 version 3,
 * +4 0x20, +8 available 1, +0x10 reason 0xe0822803. IO80211InfraInterface::
 * postMessage hands it to IO80211ControllerMonitor::setDriverAvailable
 * ("isDriverAvailable") and on to WCL. Without it WCL never loads the card
 * capabilities from the driver (WCLConfigManager::loadCardCapbilities), so
 * isHeSupported() stays false.
 */
#define YY_MSG_DRIVER_AVAILABLE 0x37
static void yy_drvavail_timer_action(OSObject *owner, IOTimerEventSource *)
{
    YYavicAirWiFiLayer *c = OSDynamicCast(YYavicAirWiFiLayer, owner);
    if (c != nullptr)
        c->postDriverAvailable();
}

void YYavicAirWiFiLayer::scheduleDriverAvailable(uint32_t ms)
{
    int off = 0;
    if (PE_parse_boot_argn("-yyavicnodrvavail", &off, sizeof(off))) {
        yy_trace("CTRL: driver available skipped (-yyavicnodrvavail)\n");
        return;
    }
    if (fDrvAvailTimer == nullptr) {
        fDrvAvailTimer = IOTimerEventSource::timerEventSource(this, yy_drvavail_timer_action);
        if (fDrvAvailTimer == nullptr ||
            ((IOWorkLoop *)gYYWorkQueue)->addEventSource(fDrvAvailTimer) != kIOReturnSuccess) {
            yy_trace("CTRL: driver available timer FAILED\n");
            OSSafeReleaseNULL(fDrvAvailTimer);
            return;
        }
    }
    fDrvAvailTimer->setTimeoutMS(ms);
}

void YYavicAirWiFiLayer::postDriverAvailable()
{
    if (fNetIf == nullptr)
        return;
    uint8_t ev[0xf8];
    bzero(ev, sizeof(ev));
    uint32_t reason = gYYDrvAvailReason;
    /* boot form (bootChipImage): version 3, 0x20; recovery form
     * (completeFaultReportCallback): version 4, 2 */
    *(uint32_t *)(ev + 0x0) = reason == 0xe0822803 ? 3 : 4;
    *(uint32_t *)(ev + 0x4) = reason == 0xe0822803 ? 0x20 : 2;
    *(uint32_t *)(ev + 0x8) = 1;
    *(uint32_t *)(ev + 0x10) = reason;
    yy_ctrl_postMessage(this, fNetIf, YY_MSG_DRIVER_AVAILABLE, ev, sizeof(ev), true);
    yy_trace("CTRL: posted APPLE80211_M_DRIVER_AVAILABLE (available 1, reason 0x%08x)\n", reason);
}

static void yy_scan_timer_action(OSObject *owner, IOTimerEventSource *)
{
    YYavicAirWiFiLayer *c = OSDynamicCast(YYavicAirWiFiLayer, owner);
    if (c != nullptr)
        c->scanTimerFired();
}

IOReturn YYavicAirWiFiLayer::startScan()
{
    IOService *r = radio();
    if (r == nullptr || fNetIf == nullptr || !fPowerOn)
        return kIOReturnNotReady;
    if (fScanTimer == nullptr) {
        fScanTimer = IOTimerEventSource::timerEventSource(this, yy_scan_timer_action);
        if (fScanTimer == nullptr || ((IOWorkLoop *)gYYWorkQueue)->addEventSource(fScanTimer) != kIOReturnSuccess) {
            yy_trace("SCAN: timer setup FAILED\n");
            OSSafeReleaseNULL(fScanTimer);
            return kIOReturnNoResources;
        }
    }
    if (fScanBusy)
        return kIOReturnSuccess;            /* results of the running scan will be posted */
    /*
     * WCL asks for a scan every ~2.5 s, also while associated. A radio
     * background scan leaves the home channel each time (0.4.4: ~230 ms idle
     * latency) and lets net80211 switch BSS on its own (0.4.4: RUN -> ASSOC
     * after 5 min, link lost). Associated: answer from the node table the
     * radio keeps up to date on the home channel, no radio scan.
     */
    if (fLinked) {
        fScanBusy = true;
        fScanTimer->setTimeoutMS(YY_SCAN_LINKED_MS);
        return kIOReturnSuccess;
    }
    uint32_t state = 0;
    IOReturn ret = r->callPlatformFunction(YY_RADIO_FN_SCAN_START, false, &state, nullptr, nullptr, nullptr);
    fScanBusy = true;
    fScanTimer->setTimeoutMS(YY_SCAN_WAIT_MS);
    static uint32_t n;
    if (n++ < 10)
        yy_trace("SCAN: start ret=0x%x radio state=%u\n", ret, state);
    return kIOReturnSuccess;
}

/* BeaconMetaData (0x40) + IEs for one BSS; returns total length (<= 0x440). */
static uint32_t yy_build_beacon(const struct yy_radio_bss *b, uint8_t *msg, uint16_t spec_override = 0)
{
    uint8_t *ie = msg + 0x40;
    uint32_t il = 0;
    bzero(msg, 0x40 + 0x400);
    /* SSID */
    ie[il++] = 0; ie[il++] = b->ssid_len;
    memcpy(ie + il, b->ssid, b->ssid_len); il += b->ssid_len;
    /* supported (+ extended) rates */
    uint8_t nr = b->nrates > 8 ? 8 : b->nrates;
    if (nr) {
        ie[il++] = 1; ie[il++] = nr;
        memcpy(ie + il, b->rates, nr); il += nr;
    }
    if (b->nrates > 8) {
        ie[il++] = 50; ie[il++] = b->nrates - 8;
        memcpy(ie + il, b->rates + 8, b->nrates - 8); il += b->nrates - 8;
    }
    /* DS parameter set */
    ie[il++] = 3; ie[il++] = 1; ie[il++] = b->channel;
    /* All other elements as received (HT/VHT/HE capabilities and operation,
     * RSN/WPA, WMM, ...): WCL reads the PHY modes and security from them.
     * Whole elements only; SSID/rates/DS are the ones built above. */
    for (uint32_t p = 0; p + 2 <= b->ies_len && p + 2 + b->ies[p + 1] <= b->ies_len;
         p += 2 + b->ies[p + 1]) {
        uint8_t id = b->ies[p], len = b->ies[p + 1];
        if (id == 0 || id == 1 || id == 3 || id == 50)
            continue;
        if (il + 2 + len > 0x400)
            break;
        memcpy(ie + il, b->ies + p, 2 + len); il += 2 + len;
    }
    uint16_t spec = spec_override != 0 ? spec_override
                                       : (uint16_t)(b->channel | 0x1000 | (b->band == YY_RADIO_BAND_5G ? 0xc000 : 0));
    *(uint32_t *)(msg + 0x00) = il;
    *(uint16_t *)(msg + 0x04) = spec;
    memcpy(msg + 0x06, b->ssid, b->ssid_len);
    msg[0x26] = b->ssid_len;
    msg[0x27] = b->channel;
    memcpy(msg + 0x29, b->bssid, 6);
    *(int32_t *)(msg + 0x30) = b->rssi_dbm;
    *(int16_t *)(msg + 0x34) = YY_NOISE_DBM;
    *(uint16_t *)(msg + 0x36) = (uint16_t)(b->rssi_dbm - YY_NOISE_DBM);
    *(uint16_t *)(msg + 0x38) = b->intval;
    *(uint16_t *)(msg + 0x3a) = b->capinfo;
    *(uint32_t *)(msg + 0x3c) = 0x7000;
    return 0x40 + il;
}

void YYavicAirWiFiLayer::scanTimerFired()
{
    static uint8_t msg[0x40 + 0x400];
    fScanBusy = false;
    IOService *r = radio();
    uint32_t n = YY_SCAN_MAX, posted = 0;
    if (fScanBuf == nullptr)
        fScanBuf = (struct yy_radio_bss *)IOMallocZero(sizeof(struct yy_radio_bss) * YY_SCAN_MAX);
    IOReturn cr = kIOReturnNotReady;
    if (r != nullptr && fScanBuf != nullptr)
        cr = r->callPlatformFunction(YY_RADIO_FN_SCAN_COPY, false, fScanBuf, &n, nullptr, nullptr);
    if (cr != kIOReturnSuccess) {
        static uint32_t e;
        if (e++ < 3)
            yy_trace("RADIO: scan copy FAILED 0x%x\n", cr);
    }
    fScanCount = cr == kIOReturnSuccess ? (n > YY_SCAN_MAX ? YY_SCAN_MAX : n) : 0;
    if (cr == kIOReturnSuccess) {
        for (uint32_t i = 0; i < fScanCount; i++) {
            uint32_t len = yy_build_beacon(&fScanBuf[i], msg);
            yy_ctrl_postMessage(this, fNetIf, YY_MSG_WCL_BEACON, msg, len, true);
            posted++;
        }
    }
    uint32_t status = 0;
    yy_ctrl_postMessage(this, fNetIf, YY_MSG_WCL_SCAN_DONE, &status, sizeof(status), true);
    static uint32_t k;
    if (k++ < 20)
        yy_trace("SCAN: posted %u BSS + scan done\n", posted);
}

/* ------------------------------------------------------------- join -- */
/*
 * Sequoia (WCL) join, from AppleBCMWLANCore::setWCL_ASSOCIATE /
 * AppleBCMWLANJoinAdapter::performJoin (15.8.1). apple80211AssocCandidates:
 *   +0x10/+0x14/+0x18/+0x210 auth context, +0x1c u32 SSID length,
 *   +0x20 SSID, +0x40 apple80211_key (+4 key_len, +8 cipher, +0x10 key),
 *   +0xd4 u16 RSN IE length, +0xd6 RSN IE, +0x1e0 u16 flags,
 *   +0x214 candidate count, +0x218 candidates[], 0x30 each:
 *   +0 valid, +4 BSSID, +0x10 AppleChannelSpec.
 * Events posted on success, in AppleBCMWLAN order:
 *   0xd8 link (BSSID, +6 up, +7 0, +8 reason)            NetAdapter::handleLink
 *   0xd4 set-SSID result (u16 status, u16 reason, BSSID, u32, u32)  JoinAdapter::handleSetSSID
 *   0xd5 connect complete (u16 status, u16, 10 x {BSSID, u32, u32})  JoinAdapter::sendConnectComplete
 *   0x52 current channel (apple80211_channel)              BssManager::setCurrentBSS
 */
#define YY_MSG_LINK            0xd8
#define YY_MSG_SET_SSID        0xd4
#define YY_MSG_CONNECT_DONE    0xd5
#define YY_MSG_CUR_CHANNEL     0x52
#define YY_MSG_SUPPLICANT      0x4c
#define YY_JOIN_TICK_MS        500
#define YY_JOIN_TICKS_MAX      40      /* 20 s */

static void yy_join_timer_action(OSObject *owner, IOTimerEventSource *)
{
    YYavicAirWiFiLayer *c = OSDynamicCast(YYavicAirWiFiLayer, owner);
    if (c != nullptr)
        c->joinTimerFired();
}

IOReturn YYavicAirWiFiLayer::radioStatus(struct yy_radio_status *st)
{
    IOService *r = radio();
    if (r == nullptr)
        return kIOReturnNotReady;
    return r->callPlatformFunction(YY_RADIO_FN_STATUS, false, st, nullptr, nullptr, nullptr);
}

IOReturn YYavicAirWiFiLayer::joinStart(const uint8_t *cand)
{
    IOService *r = radio();
    if (r == nullptr || cand == nullptr || !fPowerOn)
        return kIOReturnNotReady;
    uint32_t abi = 0;
    r->callPlatformFunction(YY_RADIO_FN_ABI, false, &abi, nullptr, nullptr, nullptr);
    if (abi < 2) {
        yy_trace("JOIN: radio bridge ABI %u has no join\n", abi);
        return kIOReturnUnsupported;
    }
    if (!fRadioMacSet)
        adoptRadioMac(true);
    struct yy_radio_join j;
    bzero(&j, sizeof(j));
    uint32_t sl = *(const uint32_t *)(cand + 0x1c);
    j.ssid_len = sl > 32 ? 32 : (uint8_t)sl;
    memcpy(j.ssid, cand + 0x20, j.ssid_len);
    uint32_t klen = *(const uint32_t *)(cand + 0x40 + 0x4);
    uint32_t kcipher = *(const uint32_t *)(cand + 0x40 + 0x8);
    if (klen == 32) {
        j.has_pmk = 1;
        memcpy(j.pmk, cand + 0x40 + 0x10, 32);
    }
    uint32_t ncand = *(const uint32_t *)(cand + 0x214);
    if (ncand > 0)
        memcpy(j.bssid, cand + 0x218 + 0x4, 6);
    uint16_t spec = ncand > 0 ? *(const uint16_t *)(cand + 0x218 + 0x10) : 0;
    memcpy(fJoinBssid, j.bssid, 6);
    yy_trace("JOIN: ssid_len=%u cipher=%u key_len=%u pmk=%u ncand=%u bssid=%02x:%02x:%02x:%02x:%02x:%02x spec=0x%x rsnie=%u\n",
             j.ssid_len, kcipher, klen, j.has_pmk, ncand, j.bssid[0], j.bssid[1], j.bssid[2], j.bssid[3],
             j.bssid[4], j.bssid[5], spec, *(const uint16_t *)(cand + 0xd4));
    IOReturn ret = r->callPlatformFunction(YY_RADIO_FN_JOIN, false, &j, nullptr, nullptr, nullptr);
    yy_trace("JOIN: radio join = 0x%x\n", ret);
    bzero(&j, sizeof(j));               /* do not keep the PMK around */
    if (ret != kIOReturnSuccess)
        return ret;
    if (fJoinTimer == nullptr) {
        fJoinTimer = IOTimerEventSource::timerEventSource(this, yy_join_timer_action);
        if (fJoinTimer == nullptr || ((IOWorkLoop *)gYYWorkQueue)->addEventSource(fJoinTimer) != kIOReturnSuccess) {
            OSSafeReleaseNULL(fJoinTimer);
            return kIOReturnNoResources;
        }
    }
    fJoinActive = true;
    fLinked = false;
    fJoinTicks = 0;
    fJoinTimer->setTimeoutMS(YY_JOIN_TICK_MS);
    return kIOReturnSuccess;
}

IOReturn YYavicAirWiFiLayer::joinAbort()
{
    fJoinActive = false;
    if (fJoinTimer != nullptr)
        fJoinTimer->cancelTimeout();
    IOService *r = radio();
    yy_trace("JOIN: abort/leave\n");
    if (r != nullptr)
        r->callPlatformFunction(YY_RADIO_FN_LEAVE, false, nullptr, nullptr, nullptr, nullptr);
    if (fLinked) {
        uint8_t ev[12];
        bzero(ev, sizeof(ev));
        memcpy(ev, fJoinBssid, 6);
        ev[6] = 0;
        *(uint32_t *)(ev + 8) = 0;
        yy_ctrl_postMessage(this, fNetIf, YY_MSG_LINK, ev, sizeof(ev), true);
        fLinked = false;
        linkStateReport(false);
        dataDetach();
    }
    return kIOReturnSuccess;
}

/*
 * apple80211_beacon_msg = BeaconMetaData (0x40) + IEs, same as the 206 scan
 * message (AppleBCMWLANCore::getWCL_BSS_INFO -> getBeaconMsgFromWLBSSInfo);
 * WCLNetManager::updateBss passes a 0x840-byte buffer.
 */
IOReturn YYavicAirWiFiLayer::fillBssInfo(uint8_t *msg)
{
    struct yy_radio_status st;
    if (msg == nullptr)
        return kIOReturnBadArgument;
    if (!fLinked || radioStatus(&st) != kIOReturnSuccess || st.state != YY_RADIO_S_RUN)
        return kIOReturnNotReady;
    struct yy_radio_bss b;
    memcpy(&b, &fLinkBss, sizeof(b));
    if (st.rssi_dbm != 0)
        b.rssi_dbm = st.rssi_dbm;
    yy_build_beacon(&b, msg, yy_chanspec(b.channel, b.band, fLinkBw, fLinkCenter));
    return kIOReturnSuccess;
}

/*
 * apple80211_extended_bss_info, from AppleBCMWLANNetAdapter::getExtendedBssInfo
 * (WCLNetManager::setCurrentBSS passes 0x1e0 bytes and leaves on error):
 *   +0x00 apple80211_rate_set_data: +4 u16 count (<= 15), +8 rates[], 12 bytes
 *         each {u32 version, u32 rate Mbps, u32 flags (1 = basic)}
 *   +0xbc HT, +0xcc VHT, +0xd4 HE MCS sets (left empty: legacy rates only here)
 *   +0xdc u8 MLO connection, +0xdd associated WPA/RSN IE (<= 0x101 bytes)
 */
IOReturn YYavicAirWiFiLayer::fillExtendedBssInfo(uint8_t *d)
{
    if (d == nullptr)
        return kIOReturnBadArgument;
    if (!fLinked)
        return kIOReturnNotReady;
    bzero(d, 0x1e0);
    uint32_t n = fLinkBss.nrates > 15 ? 15 : fLinkBss.nrates;
    *(uint16_t *)(d + 0x4) = (uint16_t)n;
    for (uint32_t i = 0; i < n; i++) {
        uint8_t *r = d + 0x8 + i * 0xc;
        *(uint32_t *)(r + 0x4) = (fLinkBss.rates[i] >> 1) & 0x3f;
        *(uint32_t *)(r + 0x8) = fLinkBss.rates[i] >> 7;
    }
    /* RSN element (id 48) of the BSS, as BCM fills it. */
    const uint8_t *e = fLinkBss.ies;
    for (uint32_t p = 0; p + 2 <= fLinkBss.ies_len && p + 2 + e[p + 1] <= fLinkBss.ies_len;
         p += 2 + e[p + 1]) {
        if (e[p] == 48) {
            uint32_t il = 2u + e[p + 1] > 0x101 ? 0x101 : 2u + e[p + 1];
            memcpy(d + 0xdd, e + p, il);
            break;
        }
    }
    return kIOReturnSuccess;
}

/* WCL leave: only drop the radio link when this layer started a join. */
IOReturn YYavicAirWiFiLayer::leaveNetwork()
{
    if (!fJoinActive && !fLinked)
        return kIOReturnSuccess;
    return joinAbort();
}

void YYavicAirWiFiLayer::postLinkUp(const struct yy_radio_status *st)
{
    /*
     * WCLNetManager::linkUp -> updateBss reads WCL_BSS_INFO right after the
     * link event and leaves the network if it fails, so fix the record now.
     * Same workloop as scanTimerFired, so fScanBuf is stable here.
     */
    bool found = false;
    for (uint32_t i = 0; fScanBuf != nullptr && i < fScanCount; i++) {
        if (memcmp(fScanBuf[i].bssid, st->bssid, 6) == 0) {
            memcpy(&fLinkBss, &fScanBuf[i], sizeof(fLinkBss));
            found = true;
            break;
        }
    }
    if (!found) {
        bzero(&fLinkBss, sizeof(fLinkBss));
        memcpy(fLinkBss.bssid, st->bssid, 6);
        fLinkBss.ssid_len = st->ssid_len > 32 ? 32 : st->ssid_len;
        memcpy(fLinkBss.ssid, st->ssid, fLinkBss.ssid_len);
        fLinkBss.capinfo = 0x0001 | (st->rsn ? 0x0010 : 0);     /* ESS, privacy */
        fLinkBss.intval = 100;
    }
    fLinkBss.channel = st->channel;
    fLinkBss.band = st->band;
    fLinkBss.rssi_dbm = st->rssi_dbm;
    fLinkBw = st->bss_bw <= 3 ? st->bss_bw : 0;
    fLinkCenter = st->center;
    yy_trace("JOIN: width %u MHz center %u\n", 20u << fLinkBw, fLinkCenter);
    yy_trace("JOIN: link BSS record from %s\n", found ? "scan" : "status");

    uint8_t link[12];
    bzero(link, sizeof(link));
    memcpy(link, st->bssid, 6);
    link[6] = 1;                                    /* link up */
    *(uint32_t *)(link + 8) = 0xff;                 /* no down reason */
    yy_ctrl_postMessage(this, fNetIf, YY_MSG_LINK, link, sizeof(link), true);

    uint8_t ss[0x14];
    bzero(ss, sizeof(ss));                          /* status 0 = success */
    memcpy(ss + 4, st->bssid, 6);
    yy_ctrl_postMessage(this, fNetIf, YY_MSG_SET_SSID, ss, sizeof(ss), true);

    uint8_t cc[0xa4];
    bzero(cc, sizeof(cc));                          /* status 0 */
    memcpy(cc + 4, st->bssid, 6);                   /* first candidate record */
    yy_ctrl_postMessage(this, fNetIf, YY_MSG_CONNECT_DONE, cc, sizeof(cc), true);

    apple80211_channel ch;
    ch.version = APPLE80211_VERSION;
    ch.channel = st->channel;
    ch.flags = yy_chan_flags_w(st->channel, st->band, fLinkBw, fLinkCenter);
    yy_ctrl_postMessage(this, fNetIf, YY_MSG_CUR_CHANNEL, &ch, sizeof(ch), true);
    memcpy(fJoinBssid, st->bssid, 6);
    dataAttach();
    linkStateReport(true);
    fLinkRsn = st->rsn != 0;
    fLinkTicks = 0;
    if (fLinkRsn)
        postSupplicantKeyed();
    yy_trace("JOIN: LINK UP posted (ch %u, rssi %d)\n", st->channel, st->rssi_dbm);
}

/* ---------------------------------------------------------- data path -- */
/*
 * RX: the radio hands every received Ethernet frame to yy_radio_rx_hook on
 * its command gate. Skywalk's RxCompletionQueue::enqueuePackets closes this
 * work queue's gate, and the work queue itself calls into the radio gate
 * (status/join), so enqueueing from the hook could deadlock: the hook only
 * queues the mbuf under fRxLock and signals fRxES; rxDrain copies into pool
 * packets and enqueues them on the work queue.
 * TX: the submission queue action (work queue) copies each packet into an
 * mbuf, hands it to the radio (yyRadioTx, not gated) and completes the
 * Skywalk packet right away through the TxCompletionQueue.
 */
static void yy_radio_rx_hook(void *ctx, void *m)
{
    ((YYavicAirWiFiLayer *)ctx)->rxEnqueueFromRadio((mbuf_t)m);
}

static void yy_rx_es_action(OSObject *owner, IOInterruptEventSource *, int)
{
    YYavicAirWiFiLayer *c = OSDynamicCast(YYavicAirWiFiLayer, owner);
    if (c != nullptr)
        c->rxDrain();
}

void YYavicAirWiFiLayer::rxEnqueueFromRadio(mbuf_t m)
{
    if (m == nullptr)
        return;
    IOLock *l = fRxLock;
    if (l == nullptr || fRxES == nullptr) {
        mbuf_freem(m);
        return;
    }
    bool drop = false;
    IOLockLock(l);
    fRxIn++;
    if (fRxQLen >= YY_RXQ_MAX) {
        drop = true;
        fRxDrop++;
    } else {
        mbuf_setnextpkt(m, nullptr);
        if (fRxTail != nullptr)
            mbuf_setnextpkt(fRxTail, m);
        else
            fRxHead = m;
        fRxTail = m;
        fRxQLen++;
    }
    IOLockUnlock(l);
    if (drop)
        mbuf_freem(m);
    else
        fRxES->interruptOccurred(nullptr, nullptr, 0);
}

void YYavicAirWiFiLayer::rxDrain()
{
    uint64_t t0 = mach_absolute_time();
    rxDrainBody();
    fRxAbs += mach_absolute_time() - t0;
}

void YYavicAirWiFiLayer::rxDrainBody()
{
    for (;;) {
        IOLockLock(fRxLock);
        mbuf_t list = fRxHead;
        fRxHead = fRxTail = nullptr;
        fRxQLen = 0;
        IOLockUnlock(fRxLock);
        if (list == nullptr)
            return;
        void *batch[YY_BATCH];
        uint32_t nb = 0;
        while (list != nullptr) {
            mbuf_t m = list;
            list = mbuf_nextpkt(m);
            mbuf_setnextpkt(m, nullptr);
            size_t len = mbuf_pkthdr_len(m);
            void *p = nullptr;
            if (!fDataAttached || fRxCompQ == nullptr || fPool == nullptr || len < 14 || len > YY_SKW_BUFSIZE ||
                yy_skw_pool_allocatePacket(fPool, &p, YY_POOL_NOSLEEP) != kIOReturnSuccess || p == nullptr) {
                fRxDrop++;
                mbuf_freem(m);
                continue;
            }
            /* No source queue: IOSkywalkPacket::completeWithQueue (from the
             * RX completion enqueue) hands a packet back to its source queue,
             * and IOSkywalkRxCompletionQueue has no packetCompletion (0.4.3:
             * panic "default packetCompletion()"); NULL skips that step. */
            IOReturn pr = yy_skw_pkt_prepareWithQueue(p, nullptr, 2, 0);
            if (pr != kIOReturnSuccess || yy_skw_pkt_bufcount(p) == 0) {
                static uint32_t e;
                if (e++ < 5)
                    yy_trace("DATA: rx prepare = 0x%x bufs=%u\n", pr, yy_skw_pkt_bufcount(p));
                fRxDrop++;
                yy_skw_pool_deallocatePacket(fPool, p);
                mbuf_freem(m);
                continue;
            }
            uint8_t *dst = (uint8_t *)yy_skw_pkt_va(p);
            if (dst == nullptr || mbuf_copydata(m, 0, len, dst) != 0 ||
                yy_skw_pkt_setOffLen(p, 0, (uint32_t)len) != kIOReturnSuccess) {
                fRxDrop++;
                yy_skw_pool_deallocatePacket(fPool, p);
                mbuf_freem(m);
                continue;
            }
            mbuf_freem(m);
            batch[nb++] = p;
            if (nb == YY_BATCH || list == nullptr) {
                IOReturn r = yy_skw_rxcomp_enqueue(fRxCompQ, batch, nb, 0);
                if (r == kIOReturnSuccess) {
                    fRxEnq += nb;
                } else {
                    static uint32_t e;
                    if (e++ < 5)
                        yy_trace("DATA: rx enqueue n=%u failed 0x%x\n", nb, r);
                    for (uint32_t i = 0; i < nb; i++)
                        yy_skw_pool_deallocatePacket(fPool, batch[i]);
                    fRxDrop += nb;
                }
                nb = 0;
            }
        }
    }
}

uint32_t YYavicAirWiFiLayer::txSubmit(void *const *packets, uint32_t n)
{
    uint64_t t0 = mach_absolute_time();
    uint32_t r = txSubmitBody(packets, n);
    fTxAbs += mach_absolute_time() - t0;
    return r;
}

uint32_t YYavicAirWiFiLayer::txSubmitBody(void *const *packets, uint32_t n)
{
    /*
     * One mbuf per frame (single cluster, 128 bytes of headroom so net80211
     * prepends the 802.11/LLC header in place) and one direct, batched call
     * into the radio for the whole submission (0.5.2: 18.7 us per packet
     * through callPlatformFunction + one gate entry each).
     */
    IOService *r = fRadio;
    mbuf_t head = nullptr, tail = nullptr;
    uint32_t nm = 0;
    uint64_t t0 = mach_absolute_time();
    for (uint32_t i = 0; i < n; i++) {
        void *p = packets[i];
        if (p == nullptr)
            continue;
        fTxIn++;
        uint32_t len = yy_skw_pkt_len(p);
        uint8_t *src = yy_skw_pkt_bufcount(p) != 0 ? (uint8_t *)yy_skw_pkt_va(p) : nullptr;
        mbuf_t m = nullptr;
        unsigned int chunks = 1;
        if (!fDataAttached || r == nullptr || src == nullptr || len < 14 || len > YY_SKW_BUFSIZE ||
            mbuf_allocpacket(MBUF_DONTWAIT, len + YY_TX_HEADROOM, &chunks, &m) != 0 || m == nullptr) {
            fTxDrop++;
            continue;
        }
        if (mbuf_next(m) != nullptr ||
            mbuf_setdata(m, (uint8_t *)mbuf_datastart(m) + YY_TX_HEADROOM, len) != 0) {
            mbuf_freem(m);
            fTxDrop++;
            continue;
        }
        memcpy(mbuf_data(m), src + yy_skw_pkt_off(p), len);
        mbuf_pkthdr_setlen(m, len);
        mbuf_setnextpkt(m, nullptr);
        if (tail != nullptr)
            mbuf_setnextpkt(tail, m);
        else
            head = m;
        tail = m;
        nm++;
    }
    uint64_t t1 = mach_absolute_time();
    fTxCopyAbs += t1 - t0;
    if (head != nullptr) {
        uint32_t q = yy_radio_tx_batch(r, head);   /* consumes the whole list */
        fTxOk += q;
        fTxDrop += nm - q;
    }
    uint64_t t2 = mach_absolute_time();
    fTxRadioAbs += t2 - t1;
    /* Return every packet to Skywalk (copied out above). */
    for (uint32_t i = 0; i < n; i += YY_BATCH) {
        uint32_t k = n - i < YY_BATCH ? n - i : YY_BATCH;
        IOReturn cr = fTxCompQ != nullptr ? yy_skw_txcomp_enqueue(fTxCompQ, packets + i, k, 0) : kIOReturnNotReady;
        if (cr != kIOReturnSuccess) {
            static uint32_t e;
            if (e++ < 5)
                yy_trace("DATA: tx completion n=%u failed 0x%x, packets back to pool\n", k, cr);
            for (uint32_t j = 0; j < k; j++)
                if (packets[i + j] != nullptr && fPool != nullptr)
                    yy_skw_pool_deallocatePacket(fPool, packets[i + j]);
        }
    }
    fTxCompAbs += mach_absolute_time() - t2;
    static uint64_t next = 1;
    if (fTxIn >= next) {
        yy_trace("DATA: tx in=%llu ok=%llu drop=%llu | rx in=%llu enq=%llu drop=%llu\n",
                 fTxIn, fTxOk, fTxDrop, fRxIn, fRxEnq, fRxDrop);
        next = next < 4096 ? next * 4 : next + 65536;
    }
    return n;
}

void YYavicAirWiFiLayer::dataAttach()
{
    if (fDataAttached || gYYMode != 1 || fRxCompQ == nullptr)
        return;
    IOService *r = radio();
    uint32_t abi = 0;
    if (r != nullptr)
        r->callPlatformFunction(YY_RADIO_FN_ABI, false, &abi, nullptr, nullptr, nullptr);
    if (abi < 3) {
        yy_trace("DATA: radio ABI %u has no data path\n", abi);
        return;
    }
    if (fRxLock == nullptr)
        fRxLock = IOLockAlloc();
    if (fSymTx == nullptr)
        fSymTx = OSSymbol::withCString(YY_RADIO_FN_TX);
    if (fRxES == nullptr) {
        fRxES = IOInterruptEventSource::interruptEventSource(this, yy_rx_es_action);
        if (fRxES != nullptr && ((IOWorkLoop *)gYYWorkQueue)->addEventSource(fRxES) != kIOReturnSuccess)
            OSSafeReleaseNULL(fRxES);
    }
    if (fRxLock == nullptr || fSymTx == nullptr || fRxES == nullptr) {
        yy_trace("DATA: attach resources FAILED\n");
        return;
    }
    fRxES->enable();
    static bool queuesOn;
    if (!queuesOn) {
        yy_skw_rxcomp_enable(fRxCompQ);
        yy_skw_txcomp_enable(fTxCompQ);
        yy_skw_txsub_enable(fTxSubQ);
        queuesOn = true;
        yy_trace("DATA: skywalk queues enabled\n");
    }
    fDataAttached = true;
    gYYDataCtrl = this;
    IOReturn hr = r->callPlatformFunction(YY_RADIO_FN_DATA_HOOK, false, (void *)yy_radio_rx_hook, this, nullptr, nullptr);
    if (hr != kIOReturnSuccess)
        fDataAttached = false;
    yy_trace("DATA: radio data hook attach = 0x%x\n", hr);
    if (fDataAttached) {
        IOReturn dr = yy_skw_txsub_requestDequeue(fTxSubQ, nullptr, 0);
        yy_trace("DATA: tx requestDequeue = 0x%x\n", dr);
    }
}

void YYavicAirWiFiLayer::dataDetach()
{
    if (!fDataAttached)
        return;
    fDataAttached = false;
    IOService *r = radio();
    IOReturn hr = r != nullptr ? r->callPlatformFunction(YY_RADIO_FN_DATA_HOOK, false, nullptr, nullptr, nullptr, nullptr)
                               : kIOReturnNotReady;
    /* The hook ran on the radio gate; after the gated detach nothing new comes. */
    if (fRxLock != nullptr) {
        IOLockLock(fRxLock);
        mbuf_t list = fRxHead;
        fRxHead = fRxTail = nullptr;
        fRxQLen = 0;
        IOLockUnlock(fRxLock);
        if (list != nullptr)
            mbuf_freem_list(list);
    }
    yy_trace("DATA: radio data hook detach = 0x%x (tx ok=%llu drop=%llu, rx enq=%llu drop=%llu)\n", hr,
             fTxOk, fTxDrop, fRxEnq, fRxDrop);
}

IOReturn YYavicAirWiFiLayer::setRadioMac(const uint8_t *mac)
{
    IOService *r = radio();
    uint32_t abi = 0;
    if (r != nullptr)
        r->callPlatformFunction(YY_RADIO_FN_ABI, false, &abi, nullptr, nullptr, nullptr);
    if (abi < 3 || fLinked || fJoinActive || !fPowerOn)
        return kIOReturnNotPermitted;
    IOReturn rr = r->callPlatformFunction(YY_RADIO_FN_SET_MAC, false, (void *)mac, nullptr, nullptr, nullptr);
    if (rr != kIOReturnSuccess)
        return kIOReturnNotPermitted;
    fRadioMacSet = true;                    /* the radio now holds the address WCL asked for */
    /* IO80211MacAddressAgent::updateMacAddress sets the ifnet address only
     * when isInterfaceEnabled(); do it here too, a mismatch would make every
     * received unicast frame look foreign to the stack. */
    if (fNetIf != nullptr) {
        ether_addr ea;
        memcpy(ea.octet, mac, 6);
        IOReturn lr = yy_skw_setLinkLayerAddress(fNetIf, &ea);
        yy_trace("DATA: interface address -> %02x:%02x:%02x:%02x:%02x:%02x = 0x%x\n", mac[0], mac[1], mac[2],
                 mac[3], mac[4], mac[5], lr);
    }
    return kIOReturnSuccess;
}

bool YYavicAirWiFiLayer::adoptRadioMac(bool leaveFirst)
{
    IOService *r = radio();
    uint32_t abi = 0;
    if (r != nullptr)
        r->callPlatformFunction(YY_RADIO_FN_ABI, false, &abi, nullptr, nullptr, nullptr);
    if (abi < 3)
        return false;
    if (leaveFirst)
        r->callPlatformFunction(YY_RADIO_FN_LEAVE, false, nullptr, nullptr, nullptr, nullptr);
    IOReturn rr = r->callPlatformFunction(YY_RADIO_FN_SET_MAC, false, fMac.octet, nullptr, nullptr, nullptr);
    fRadioMacSet = rr == kIOReturnSuccess;
    yy_trace("DATA: radio own address -> %02x:%02x:%02x:%02x:%02x:%02x = 0x%x\n", fMac.octet[0], fMac.octet[1],
             fMac.octet[2], fMac.octet[3], fMac.octet[4], fMac.octet[5], rr);
    return fRadioMacSet;
}

/* As AppleBCMWLANNetAdapter::setLinkUp / setLinkDown: drives the Skywalk link
 * status (en3 active/inactive). */
void YYavicAirWiFiLayer::linkStateReport(bool up)
{
    if (fNetIf == nullptr || gYYMode != 1)
        return;
    bool r = yy_infra_setLinkState(fNetIf, up ? 2 : 1, 0, false, 0);
    yy_trace("DATA: setLinkState(%s) = %d\n", up ? "up" : "down", r);
}

/*
 * WCLNetManager::linkUp arms a 10 s "4wayHsTimeout" for RSN links and
 * assocTimerAction leaves the network ("no key/4way hand shake event") unless
 * a supplicant event (msg 0x4c -> WCLNetManager::handleSupplicantEvent)
 * clears it. The radio finishes the 4-way handshake itself (port_valid), so
 * report it the way AppleBCMWLANCore::handlePSKEvent does for WLC_SUP_KEYED:
 * 0x28 bytes, +0 0xe0820400|6, +4 0xe0822c00|reason 0, +8 0x3f6.
 */
void YYavicAirWiFiLayer::postSupplicantKeyed()
{
    uint8_t ev[0x28];
    bzero(ev, sizeof(ev));
    *(uint32_t *)(ev + 0x0) = 0xe0820406;
    *(uint32_t *)(ev + 0x4) = 0xe0822c00;
    *(uint32_t *)(ev + 0x8) = 0x3f6;
    yy_ctrl_postMessage(this, fNetIf, YY_MSG_SUPPLICANT, ev, sizeof(ev), true);
    yy_trace("JOIN: supplicant KEYED posted\n");
}

void YYavicAirWiFiLayer::postJoinFailed(uint16_t status)
{
    uint8_t ss[0x14];
    bzero(ss, sizeof(ss));
    *(uint16_t *)ss = status;
    memcpy(ss + 4, fJoinBssid, 6);
    yy_ctrl_postMessage(this, fNetIf, YY_MSG_SET_SSID, ss, sizeof(ss), true);
    uint8_t cc[0xa4];
    bzero(cc, sizeof(cc));
    *(uint16_t *)cc = status;
    memcpy(cc + 4, fJoinBssid, 6);
    yy_ctrl_postMessage(this, fNetIf, YY_MSG_CONNECT_DONE, cc, sizeof(cc), true);
    yy_trace("JOIN: FAILED posted (status %u)\n", status);
}

void YYavicAirWiFiLayer::joinTimerFired()
{
    struct yy_radio_status st;
    if (radioStatus(&st) != kIOReturnSuccess) {
        bzero(&st, sizeof(st));
    }
    static uint32_t n;
    if (fJoinActive) {
        if (n++ < 60 && (fJoinTicks % 4) == 0)
            yy_trace("JOIN: tick %u state=%u port=%u ch=%u\n", fJoinTicks, st.state, st.port_valid, st.channel);
        if (st.state == YY_RADIO_S_RUN && st.port_valid) {
            fJoinActive = false;
            fLinked = true;
            postLinkUp(&st);
            fJoinTimer->setTimeoutMS(1000);     /* link watch */
            return;
        } else if (++fJoinTicks >= YY_JOIN_TICKS_MAX) {
            fJoinActive = false;
            postJoinFailed(1);
            IOService *r = radio();
            if (r != nullptr)
                r->callPlatformFunction(YY_RADIO_FN_LEAVE, false, nullptr, nullptr, nullptr, nullptr);
            return;
        }
        fJoinTimer->setTimeoutMS(YY_JOIN_TICK_MS);
        return;
    }
    /* linked: watch for loss of the association */
    if (fLinked && st.state == YY_RADIO_S_RUN)
        fLinkDownTicks = 0;
    /* Debounce: net80211 passes through ASSOC on a reassociation; report a
     * loss only after 3 s out of RUN. */
    if (fLinked && st.state != YY_RADIO_S_RUN && ++fLinkDownTicks < 3) {
        yy_trace("JOIN: radio out of RUN (state %u) %u s\n", st.state, fLinkDownTicks);
        fJoinTimer->setTimeoutMS(1000);
        return;
    }
    if (fLinked && st.state != YY_RADIO_S_RUN) {
        fLinkDownTicks = 0;
        uint8_t ev[12];
        bzero(ev, sizeof(ev));
        memcpy(ev, fJoinBssid, 6);
        ev[6] = 0;
        *(uint32_t *)(ev + 8) = 0;
        yy_ctrl_postMessage(this, fNetIf, YY_MSG_LINK, ev, sizeof(ev), true);
        fLinked = false;
        linkStateReport(false);
        dataDetach();
        yy_trace("JOIN: LINK DOWN (radio state %u)\n", st.state);
        return;
    }
    if (fLinked) {
        /* once more after the link-up processing, in case it ran after the first */
        if (fLinkRsn && ++fLinkTicks == 1)
            postSupplicantKeyed();
        if (fLinkTicks <= 30 ? (fLinkTicks % 5) == 0 : (fLinkTicks % 60) == 0)
        {
            uint64_t rxns, txns, cns, rns, kns;
            absolutetime_to_nanoseconds(fRxAbs, &rxns);
            absolutetime_to_nanoseconds(fTxAbs, &txns);
            absolutetime_to_nanoseconds(fTxCopyAbs, &cns);
            absolutetime_to_nanoseconds(fTxRadioAbs, &rns);
            absolutetime_to_nanoseconds(fTxCompAbs, &kns);
            yy_trace("DATA: t=%us tx in=%llu ok=%llu drop=%llu | rx in=%llu enq=%llu drop=%llu | wq rx=%llums tx=%llums (copy %llu radio %llu comp %llu)\n",
                     fLinkTicks, fTxIn, fTxOk, fTxDrop, fRxIn, fRxEnq, fRxDrop, rxns / 1000000, txns / 1000000,
                     cns / 1000000, rns / 1000000, kns / 1000000);
        }
        fJoinTimer->setTimeoutMS(1000);
    }
}

void YYavicAirWiFiLayer::stop(IOService *provider)
{
    yy_trace("CTRL: stop\n");
    dataDetach();
    gYYDataCtrl = nullptr;
    if (fRxES != nullptr) {
        ((IOWorkLoop *)gYYWorkQueue)->removeEventSource(fRxES);
        OSSafeReleaseNULL(fRxES);
    }
    if (fBsdIf) {
        IONetworkController::detachInterface(fBsdIf, true);
        OSSafeReleaseNULL(fBsdIf);
    }
    if (fNetIf) {
        detachInterface(fNetIf, true);
        OSSafeReleaseNULL(fNetIf);
    }
    IO80211Controller::stop(provider);
    OSSafeReleaseNULL(fIOFaultReporter);
    OSSafeReleaseNULL(fFaultReporter);
    OSSafeReleaseNULL(fCCStream);
    OSSafeReleaseNULL(fCCPipe);
}

IOWorkLoop *YYavicAirWiFiLayer::getWorkLoop() const
{
    return (IOWorkLoop *)gYYWorkQueue;
}

bool YYavicAirWiFiLayer::createWorkQueue()
{
    yy_trace("CTRL: createWorkQueue (have=%d)\n", gYYWorkQueue != nullptr);
    return gYYWorkQueue != nullptr;
}

IO80211WorkQueue *YYavicAirWiFiLayer::getWorkQueue() const
{
    return gYYWorkQueue;
}

IOReturn YYavicAirWiFiLayer::getHardwareAddress(IOEthernetAddress *addr)
{
    memcpy(addr->bytes, fMac.octet, 6);
    return kIOReturnSuccess;
}

IOReturn YYavicAirWiFiLayer::enable(IONetworkInterface *)
{
    yy_trace("CTRL: enable\n");
    return kIOReturnSuccess;
}

IOReturn YYavicAirWiFiLayer::disable(IONetworkInterface *)
{
    yy_trace("CTRL: disable\n");
    return kIOReturnSuccess;
}

bool YYavicAirWiFiLayer::isCommandProhibited(int)
{
    return false;
}

SInt32 YYavicAirWiFiLayer::handleCardSpecific(IO80211SkywalkInterface *, unsigned long cmd, void *, bool)
{
    yy_trace("CTRL: handleCardSpecific cmd=%lu\n", cmd);
    return 0;
}

static IOReturn yy_version(apple80211_version_data *d, const char *s)
{
    if (d == nullptr)
        return kIOReturnBadArgument;
    d->version = APPLE80211_VERSION;
    strlcpy(d->string, s, sizeof(d->string));
    d->string_len = strlen(d->string);
    return kIOReturnSuccess;
}

IOReturn YYavicAirWiFiLayer::getDRIVER_VERSION(IO80211SkywalkInterface *, apple80211_version_data *d)
{
    yy_trace("CTRL: getDRIVER_VERSION\n");
    return yy_version(d, "YYavicWiFi 0.7.0 (MT7922)");
}

IOReturn YYavicAirWiFiLayer::getHARDWARE_VERSION(IO80211SkywalkInterface *, apple80211_version_data *d)
{
    yy_trace("CTRL: getHARDWARE_VERSION\n");
    return yy_version(d, "MediaTek MT7922");
}

IOReturn YYavicAirWiFiLayer::getCARD_CAPABILITIES(IO80211SkywalkInterface *, apple80211_capability_data *d)
{
    yy_trace("CTRL: getCARD_CAPABILITIES\n");
    if (d == nullptr)
        return kIOReturnBadArgument;
    {
        /* 0.6.1 diagnostics: WCL shows no 802.11ax although capabilities[17]
         * bit 7 is set -- which caller, which buffer (0xaa = pre-filled by
         * IO80211 getCARD_CAPABILITIES) */
        static uint32_t k;
        if (k++ < 4) {
            const uint8_t *b = (const uint8_t *)d;
            yy_trace("CAPS: d=%p in:", d);
            for (int i = 0; i < 24; i++)
                yy_trace(" %02x", b[i]);
            yy_trace("\n");
            yy_trace_bt("CAPS");
        }
    }
    d->version = APPLE80211_VERSION;
    memset(d->capabilities, 0, sizeof(d->capabilities));
    /* MT7922 (YYavicMtkWlan): RSN with TKIP/CCMP, short slot/preamble, WME.
     * Byte layout and the extra bytes as AirportItlwm reports them. */
    d->capabilities[0] |= 1 << APPLE80211_CAP_TKIP | 1 << APPLE80211_CAP_AES_CCM;
    d->capabilities[1] |= 1 << (APPLE80211_CAP_SHSLOT - 8) | 1 << (APPLE80211_CAP_SHPREAMBLE - 8);
    d->capabilities[1] |= 1 << (APPLE80211_CAP_WPA1 - 8) | 1 << (APPLE80211_CAP_WPA2 - 8) |
                          1 << (APPLE80211_CAP_TKIPMIC - 8);
    d->capabilities[2] = 0xFF;
    d->capabilities[3] = 0x2B;
    d->capabilities[5] = 0x40;
    d->capabilities[6] = 0x8 | 0x4 | 0x80;
    *(uint16_t *)&d->capabilities[8] = 0x201;
    /* 802.11ax: WCLDeviceConfiguration::isHeSupported() is capabilities[17]
     * bit 7 (15.8.1). Only while the radio has HE on (-yyavicnohe / dbg 33). */
    struct yy_radio_status st;
    if (radioStatus(&st) == kIOReturnSuccess && st.he_enabled)
        d->capabilities[17] |= 0x80;
    yy_trace("CTRL: getCARD_CAPABILITIES he=%u\n", (d->capabilities[17] & 0x80) != 0);
    return kIOReturnSuccess;
}

IOReturn YYavicAirWiFiLayer::getPOWER(IO80211SkywalkInterface *, apple80211_power_data *d)
{
    yy_trace("CTRL: getPOWER\n");
    if (d == nullptr)
        return kIOReturnBadArgument;
    d->version = APPLE80211_VERSION;
    d->num_radios = 1;
    d->power_state[0] = fPowerOn ? APPLE80211_POWER_ON : APPLE80211_POWER_OFF;
    return kIOReturnSuccess;
}

/*
 * Wi-Fi off/on from the menu. WCL sends only SET POWER (no leave): with
 * Apple's driver the firmware going down drops the link. Do the same: report
 * the link down, detach the data path, deauth and power the radio down; on
 * power-on bring it up and let WCL auto-join.
 */
IOReturn YYavicAirWiFiLayer::setPOWER(IO80211SkywalkInterface *, apple80211_power_data *d)
{
    yy_trace("CTRL: setPOWER %u\n", d ? d->power_state[0] : 0xffff);
    if (d == nullptr || d->num_radios == 0)
        return kIOReturnSuccess;
    bool on = d->power_state[0] != APPLE80211_POWER_OFF;
    if (on == (fPowerOn != 0))
        return kIOReturnSuccess;
    IOService *r = radio();
    uint32_t abi = 0;
    if (r != nullptr)
        r->callPlatformFunction(YY_RADIO_FN_ABI, false, &abi, nullptr, nullptr, nullptr);
    if (!on) {
        fPowerOn = 0;
        if (fJoinActive || fLinked)
            joinAbort();                    /* link down event, en3 inactive, data detach, leave */
        if (fScanTimer != nullptr)
            fScanTimer->cancelTimeout();
        fScanBusy = false;
    }
    IOReturn pr = kIOReturnUnsupported;
    if (abi >= 4) {
        uint32_t v = on ? 1 : 0;
        pr = r->callPlatformFunction(YY_RADIO_FN_POWER, false, &v, nullptr, nullptr, nullptr);
    }
    if (on)
        fPowerOn = 1;
    yy_trace("CTRL: radio power %s = 0x%x\n", on ? "on" : "off", pr);
    return kIOReturnSuccess;
}

IOReturn YYavicAirWiFiLayer::getCOUNTRY_CODE(IO80211SkywalkInterface *, apple80211_country_code_data *d)
{
    yy_trace("CTRL: getCOUNTRY_CODE\n");
    if (d == nullptr)
        return kIOReturnBadArgument;
    d->version = APPLE80211_VERSION;
    memset(d->cc, 0, sizeof(d->cc));
    d->cc[0] = 'T';
    d->cc[1] = 'R';
    return kIOReturnSuccess;
}

IOReturn YYavicAirWiFiLayer::setCOUNTRY_CODE(IO80211SkywalkInterface *, apple80211_country_code_data *)
{
    yy_trace("CTRL: setCOUNTRY_CODE\n");
    return kIOReturnSuccess;
}

IOReturn YYavicAirWiFiLayer::setGET_DEBUG_INFO(IO80211SkywalkInterface *, apple80211_debug_command *)
{
    yy_trace("CTRL: setGET_DEBUG_INFO\n");
    return kIOReturnUnsupported;
}

/*
 * 15.8.1: IO80211Controller::findAndAttachToFaultReporter panics with
 * "No ivars->_faultReporter" when this returns null (it never creates one
 * itself). The family only retains the object, so build a CoreCapture fault
 * reporter the same way AppleMultiFunctionManagerLogger does: a data pipe
 * (CCPipeOptions type 1), a CCDataStream on it, then
 * CCFaultReporter::withStreamWorkloop(). Option layouts (CCPipeOptions 0x350,
 * CCStreamOptions 0x358 bytes) are taken from that disassembly.
 */
struct CCPipeOptions;
struct CCStreamOptions;
struct CCPipe {
    static CCPipe *withOwnerNameCapacity(IOService *, const char *, const char *, const CCPipeOptions *);
    void startPipe();
};
struct CCDataStream {
    static CCDataStream *withPipeAndName(CCPipe *, const char *, const CCStreamOptions *);
};
struct CCFaultReporter {
    static CCFaultReporter *withStreamWorkloop(CCDataStream *, IOWorkLoop *);
    void addOwner(const char *);
};
/* The family keeps this object and later calls IO80211FaultReporter vslots on
 * it (IO80211PeerManager -> registerCallbacks @0x120); a bare CCFaultReporter
 * there resolves to IORegistryEntry::copyProperty and faults (0.1.4 panic).
 * AppleBCMWLANBusInterfacePCIe::deferredStart wraps it the same way. */
struct IO80211FaultReporter {
    static IO80211FaultReporter *allocWithParams(CCFaultReporter *);
};
struct CCLogStream {
    static CCLogStream *withPipeAndName(CCPipe *, const char *, const CCStreamOptions *);
};

#define YY_CC_OWNER "com.yyavic.WiFi"

/*
 * 15.8.1: the family uses getLogger() directly as a CCLogStream
 * (IO80211ScanManager::commonInit stores it, createReportersAndLegend calls
 * CCLogStream::shouldLog on it; AppleBCMWLANCore returns its CCLogStream).
 * 0.1.0-0.1.5 returned an opaque OSObject -> fault at [[obj+0x90]+0x58].
 * Built like AppleMultiFunctionManagerLogger's "DriverLogs" pipe:
 * CCPipeOptions type 0 (log pipe), CCStreamOptions type 0 (log stream).
 */
void *YYavicAirWiFiLayer::getLogger() const
{
    static CCLogStream *stream;
    static uint32_t n;
    if (stream == nullptr) {
        uint8_t *po = (uint8_t *)IOMallocZero(0x350);
        uint8_t *so = (uint8_t *)IOMallocZero(0x358);
        if (po != nullptr && so != nullptr) {
            *(uint32_t *)(po + 0x0) = 0;            /* pipe type: 0 = CCLogPipe */
            *(uint32_t *)(po + 0x8) = 1;
            *(uint64_t *)(po + 0x10) = 0x1000;
            *(uint64_t *)(po + 0x18) = 0x666;
            *(uint32_t *)(po + 0x20) = 1000;
            strlcpy((char *)po + 0x24, "YYavicLogs", 0x100);
            strlcpy((char *)po + 0x124, "yyavic", 0x100);
            strlcpy((char *)po + 0x244, "WiFi", 0x100);
            *(uint64_t *)(po + 0x224) = 0x200080000ULL;
            CCPipe *pipe = CCPipe::withOwnerNameCapacity((IOService *)this, YY_CC_OWNER,
                                                         "YYavicLogs", (const CCPipeOptions *)po);
            yy_trace("CTRL: logger: log pipe = %p (%s)\n", pipe,
                     pipe ? ((OSObject *)pipe)->getMetaClass()->getClassName() : "-");
            if (pipe != nullptr) {
                *(uint32_t *)(so + 0x0) = 0;        /* stream type: 0 = log */
                *(uint32_t *)(so + 0x8) = 5;        /* level */
                *(uint32_t *)(so + 0xc) = 1;
                strlcpy((char *)so + 0x58, "yyavic", 0xf0);
                stream = CCLogStream::withPipeAndName(pipe, "YYavicAirWiFiLayer",
                                                      (const CCStreamOptions *)so);
                yy_trace("CTRL: logger: log stream = %p (%s)\n", stream,
                         stream ? ((OSObject *)stream)->getMetaClass()->getClassName() : "-");
                if (stream != nullptr)
                    pipe->startPipe();
                /* the stream retains its pipe */
            }
        }
        if (po)
            IOFree(po, 0x350);
        if (so)
            IOFree(so, 0x358);
    }
    if (n++ < 3)
        yy_trace("CTRL: getLogger #%u -> %p\n", n, stream);
    return stream;
}



void *YYavicAirWiFiLayer::getFaultReporterFromDriver()
{
    static uint32_t n;
    if (fIOFaultReporter != nullptr) {
        if (n++ < 3)
            yy_trace("CTRL: getFaultReporterFromDriver -> %p (cached)\n", fIOFaultReporter);
        return fIOFaultReporter;
    }

    uint8_t *po = (uint8_t *)IOMallocZero(0x350);
    uint8_t *so = (uint8_t *)IOMallocZero(0x358);
    if (po == nullptr || so == nullptr) {
        yy_trace("CTRL: faultReporter: options alloc FAILED\n");
        goto out;
    }

    /* CCPipeOptions: data pipe */
    *(uint32_t *)(po + 0x0) = 1;            /* pipe type: 1 = CCDataPipe */
    *(uint32_t *)(po + 0x4) = 2;
    *(uint64_t *)(po + 0x8) = 2;
    *(uint64_t *)(po + 0x10) = 0x40;
    strlcpy((char *)po + 0x24, "StateSnapshots", 0x100);
    strlcpy((char *)po + 0x124, "0", 0x100);
    strlcpy((char *)po + 0x244, "WiFi", 0x100);
    {
        CCPipe *pipe = CCPipe::withOwnerNameCapacity(this, YY_CC_OWNER, "StateSnapshots",
                                                     (const CCPipeOptions *)po);
        fCCPipe = (OSObject *)pipe;
        yy_trace("CTRL: faultReporter: data pipe = %p (%s)\n", pipe,
                 pipe ? fCCPipe->getMetaClass()->getClassName() : "-");
        if (pipe == nullptr)
            goto out;

        /* CCStreamOptions: data stream */
        *(uint32_t *)(so + 0x0) = 1;        /* stream type: 1 = data */
        *(int64_t *)(so + 0x8) = -1;
        CCDataStream *ds = CCDataStream::withPipeAndName(pipe, "FaultReporter",
                                                         (const CCStreamOptions *)so);
        fCCStream = (OSObject *)ds;
        yy_trace("CTRL: faultReporter: data stream = %p (%s)\n", ds,
                 ds ? fCCStream->getMetaClass()->getClassName() : "-");
        if (ds == nullptr)
            goto out;

        CCFaultReporter *fr = CCFaultReporter::withStreamWorkloop(ds, getWorkLoop());
        fFaultReporter = (OSObject *)fr;
        yy_trace("CTRL: faultReporter = %p (%s)\n", fr,
                 fr ? fFaultReporter->getMetaClass()->getClassName() : "-");
        if (fr == nullptr)
            goto out;
        fr->addOwner(YY_CC_OWNER);
        fr->addOwner("com.apple.iokit.IO80211Family");
        pipe->startPipe();
        yy_trace("CTRL: faultReporter: owners added, pipe started\n");
        IO80211FaultReporter *ifr = IO80211FaultReporter::allocWithParams(fr);
        fIOFaultReporter = (OSObject *)ifr;
        yy_trace("CTRL: IO80211FaultReporter = %p (%s)\n", ifr,
                 ifr ? fIOFaultReporter->getMetaClass()->getClassName() : "-");
    }
out:
    if (po)
        IOFree(po, 0x350);
    if (so)
        IOFree(so, 0x358);
    return fIOFaultReporter;
}
