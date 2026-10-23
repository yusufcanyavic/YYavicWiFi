//
//  Copyright (C) 2026 Yusufcan Yavic
//  SPDX-License-Identifier: GPL-3.0-or-later
//
//
//  YYavicAirWiFiLayer.hpp
//  YYavicAirWiFiLayer - native macOS Wi-Fi (IO80211/Skywalk) layer for MediaTek MT7922
//
//  Copyright (C) 2026 Yusufcan Yavic
//
//  This program is free software: you can redistribute it and/or modify
//  it under the terms of the GNU General Public License as published by
//  the Free Software Foundation, either version 3 of the License, or
//  (at your option) any later version.
//
//  This program is distributed in the hope that it will be useful,
//  but WITHOUT ANY WARRANTY; without even the implied warranty of
//  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
//  GNU General Public License for more details.
//
//  Milestone 2a ("Wi-Fi gorunuyor"): only the macOS-facing layer, no radio
//  driver. Every call macOS makes into the layer is recorded in the
//  IORegistry property "yy-trace" so the required surface can be mapped.
//

#ifndef YYavicAirWiFiLayer_hpp
#define YYavicAirWiFiLayer_hpp

#ifndef __IO80211_TARGET
#define __IO80211_TARGET __MAC_11_0
#endif
#include <IOKit/80211/apple80211_ioctl.h>
#include "IO80211_15_8_1.h"
#include <IOKit/pci/IOPCIDevice.h>
#include <IOKit/IOTimerEventSource.h>
#include <IOKit/IOInterruptEventSource.h>
#include <IOKit/IOLocks.h>
#include <sys/kpi_mbuf.h>
#include <IOKit/network/IOEthernetInterface.h>
#include "yy_radio.h"

extern "C" void yy_trace(const char *fmt, ...) __printflike(1, 2);

/* Record the first few calls of every not-yet-implemented method. */
#define YY_STUB(name) do { static uint32_t _yy_n; if (_yy_n++ < 3) yy_trace("STUB %s #%u\n", name, _yy_n); } while (0)

class YYavicAirWiFiLayerInterface;

/* Owns the PCI function, MSI, power management and the Wi-Fi work queue. */
class YYavicAirWiFiLayerPCI : public IOService {
    OSDeclareDefaultStructors(YYavicAirWiFiLayerPCI)
public:
    IOService *probe(IOService *provider, SInt32 *score) override;
    bool start(IOService *provider) override;
    void stop(IOService *provider) override;
    IOWorkLoop *getWorkLoop() const override;
    IOReturn setPowerState(unsigned long state, IOService *device) override;
    IOReturn setProperties(OSObject *properties) override;
    void registerService(IOOptionBits options = 0) override;
    void armNow(int mode, const char *by);

    bool fArmed;

    IOPCIDevice *fPci;
};

/* The IO80211 infrastructure (station) interface macOS talks to. */
class YYavicAirWiFiLayerInterface : public IO80211InfraProtocol {
    OSDeclareDefaultStructors(YYavicAirWiFiLayerInterface)
public:
    /* IO80211InfraInterface::init() allocates the infra ivars (+0x118);
     * IO80211SkywalkInterface::init(IOService*, ether_addr*) does not. */
    bool initInfra() { return init(); }
    IOSkywalkEthernetInterface::RegistrationInfo *yyRegInfo;
    /* implemented (no longer stubs) */
    virtual IOReturn getOP_MODE(apple80211_opmode_data *) override;
    virtual IOReturn getSUPPORTED_CHANNELS(apple80211_sup_channel_data *) override;
    virtual IOReturn getHW_SUPPORTED_CHANNELS(apple80211_sup_channel_data *) override;
    virtual IOReturn setWCL_SCAN_REQ(apple80211ScanRequest *) override;
    virtual IOReturn getCHANNELS_INFO(apple80211_channels_info *) override;
    virtual IOReturn getCOUNTRY_CHANNELS_INFO(apple80211_channels_info *) override;
    virtual IOReturn setWCL_ASSOCIATE(apple80211AssocCandidates *) override;
    virtual IOReturn setWCL_JOIN_ABORT(void *) override;
    virtual IOReturn getCHANNEL(apple80211_channel_data *) override;
    virtual IOReturn getRSSI(apple80211_rssi_data *) override;
    virtual IOReturn getRATE(apple80211_rate_data *) override;
    virtual IOReturn getROAM_PROFILE(apple80211_roam_profile_all_bands *) override;
    virtual IOReturn setMacAddress(ether_addr &) override;
    virtual IOReturn getWCL_LOW_LATENCY_INFO(apple80211_low_latency_info *) override;
    virtual IOReturn setWCL_LEAVE_NETWORK(apple80211_leave_network *) override;
    virtual IOReturn getWCL_BSS_INFO(apple80211_beacon_msg *) override;
    virtual IOReturn getWCL_TRAFFIC_COUNTERS(apple80211_wcl_traffic_counters *) override;
    virtual IOReturn getWCL_EXTENDED_BSS_INFO(apple80211_extended_bss_info *) override;
    virtual IOReturn getMCS(apple80211_mcs_data *) override;
    virtual IOReturn getMCS_VHT(apple80211_mcs_vht_data *) override;
    virtual IOReturn getNSS(apple80211_nss_data *) override;
    virtual IOReturn getMAX_NSS_FOR_AP(apple80211_btcoex_max_nss_for_ap_data *) override;
#include "YYavicAirWiFiLayerInterface_stubs.inc"
};

/*
 * Classic BSD interface (legacy mode). Same scheme as AirportItlwm on
 * Sonoma/Sequoia: data goes through an IOEthernetInterface created by the
 * controller; once it is attached to BSD its name/unit are copied to the
 * IO80211 Skywalk interface, which is then published and bound to the ifnet
 * (prepareBSDInterface) so airportd finds a Wi-Fi interface.
 */
class YYavicAirWiFiLayerBSDInterface : public IOEthernetInterface {
    OSDeclareDefaultStructors(YYavicAirWiFiLayerBSDInterface)
public:
    bool initWithSkywalk(IONetworkController *controller, YYavicAirWiFiLayerInterface *sky);
    IOReturn attachToDataLinkLayer(IOOptionBits options, void *parameter) override;
    void detachFromDataLinkLayer(IOOptionBits options, void *parameter) override;
    IOService *getProvider() const override;

    YYavicAirWiFiLayerInterface *fSky;
    bool fAttached;
};

/* The IO80211Controller. */
class YYavicAirWiFiLayer : public IO80211Controller {
    OSDeclareDefaultStructors(YYavicAirWiFiLayer)
public:
    bool init(OSDictionary *props) override;
    IOService *probe(IOService *provider, SInt32 *score) override;
    IONetworkInterface *createInterface() override;
    bool configureInterface(IONetworkInterface *netif) override;
    UInt32 outputPacket(mbuf_t m, void *param) override;
    IOReturn selectMedium(const IONetworkMedium *medium) override;
    bool createMediumTables();

    /* radio bridge + Sequoia (WCL) scan */
    IOService *radio();
    IOReturn fillChannels(apple80211_sup_channel_data *d);
    IOReturn fillChannelsInfo(void *info);
    IOReturn startScan();
    void scanTimerFired();
    void scheduleDriverAvailable(uint32_t ms);
    void postDriverAvailable();
    /* join (3b) */
    IOReturn joinStart(const uint8_t *cand);
    IOReturn joinAbort();
    IOReturn leaveNetwork();
    IOReturn radioStatus(struct yy_radio_status *st);
    void joinTimerFired();
    void postLinkUp(const struct yy_radio_status *st);
    void postJoinFailed(uint16_t status);
    IOReturn fillBssInfo(uint8_t *msg);
    IOReturn fillExtendedBssInfo(uint8_t *d);
    /* data path (4): Skywalk <-> radio */
    void dataAttach();
    void dataDetach();
    void rxEnqueueFromRadio(mbuf_t m);
    void rxDrain();
    void rxDrainBody();
    uint32_t txSubmitBody(void *const *packets, uint32_t n);
    uint32_t txSubmit(void *const *packets, uint32_t n);
    void linkStateReport(bool up);
    bool adoptRadioMac(bool leaveFirst);
    IOReturn setRadioMac(const uint8_t *mac);
    bool fRadioMacSet;
    uint32_t fLinkDownTicks;
    uint8_t fLinkBw, fLinkCenter;     /* operating width (0=20..3=160) / center channel at link up */
    IOLock *fRxLock;
    mbuf_t fRxHead, fRxTail;
    uint32_t fRxQLen;
    IOInterruptEventSource *fRxES;
    const OSSymbol *fSymTx;
    bool fDataAttached;
    uint64_t fRxIn, fRxEnq, fRxDrop, fTxIn, fTxOk, fTxDrop;
    uint64_t fRxAbs, fTxAbs;          /* perf: work queue time in rxDrain / txSubmit */
    uint64_t fTxCopyAbs, fTxRadioAbs, fTxCompAbs;   /* 0.6.6: txSubmit split */
    void postSupplicantKeyed();
    uint32_t fLinkTicks;
    bool fLinkRsn;
    struct yy_radio_bss fLinkBss;       /* BSS we are linked to, fixed at link up */
    IOTimerEventSource *fJoinTimer;
    uint32_t fJoinTicks;
    bool fJoinActive;
    bool fLinked;
    uint8_t fJoinBssid[6];
    IOService *fRadio;
    IOTimerEventSource *fScanTimer;
    IOTimerEventSource *fDrvAvailTimer;
    struct yy_radio_bss *fScanBuf;
    uint32_t fScanCount;
    bool fScanBusy;
    bool start(IOService *provider) override;
    void stop(IOService *provider) override;
    IOWorkLoop *getWorkLoop() const override;
    virtual bool createWorkQueue() override;
    virtual IO80211WorkQueue *getWorkQueue() const override;
    IOReturn getHardwareAddress(IOEthernetAddress *addr) override;
    IOReturn enable(IONetworkInterface *netif) override;
    IOReturn disable(IONetworkInterface *netif) override;

    /* IO80211Controller pure virtuals (15.8.1) */
    virtual bool isCommandProhibited(int) override;
    virtual SInt32 handleCardSpecific(IO80211SkywalkInterface *, unsigned long, void *, bool) override;
    virtual IOReturn getDRIVER_VERSION(IO80211SkywalkInterface *, apple80211_version_data *) override;
    virtual IOReturn getHARDWARE_VERSION(IO80211SkywalkInterface *, apple80211_version_data *) override;
    virtual IOReturn getCARD_CAPABILITIES(IO80211SkywalkInterface *, apple80211_capability_data *) override;
    virtual IOReturn getPOWER(IO80211SkywalkInterface *, apple80211_power_data *) override;
    virtual IOReturn setPOWER(IO80211SkywalkInterface *, apple80211_power_data *) override;
    virtual IOReturn getCOUNTRY_CODE(IO80211SkywalkInterface *, apple80211_country_code_data *) override;
    virtual IOReturn setCOUNTRY_CODE(IO80211SkywalkInterface *, apple80211_country_code_data *) override;
    virtual IOReturn setGET_DEBUG_INFO(IO80211SkywalkInterface *, apple80211_debug_command *) override;
    virtual void *getLogger() const override;
    virtual void *getFaultReporterFromDriver() override;

    YYavicAirWiFiLayerInterface *fNetIf;
    YYavicAirWiFiLayerBSDInterface *fBsdIf;
    bool setupSkywalkDatapath();
    void *fPool;                /* IOSkywalkPacketBufferPool (tx + rx) */
    void *fTxSubQ;              /* IOSkywalkTxSubmissionQueue */
    void *fTxCompQ;             /* IOSkywalkTxCompletionQueue */
    void *fRxCompQ;             /* IOSkywalkRxCompletionQueue */
    OSObject *fCCPipe;          /* CoreCapture data pipe behind the fault reporter */
    OSObject *fCCStream;        /* CCDataStream "FaultReporter" */
    OSObject *fFaultReporter;   /* CCFaultReporter */
    OSObject *fIOFaultReporter; /* IO80211FaultReporter wrapping it, handed to the family */
    ether_addr fMac;
    uint32_t fPowerOn;
};

#endif /* YYavicAirWiFiLayer_hpp */
