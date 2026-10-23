// mtkscan: scan / status / debug tool for the YYavicMtkWlan radio user client
// Copyright (C) 2026 Yusufcan Yavic
// SPDX-License-Identifier: GPL-3.0-or-later
#include <stdio.h>
#include <stdint.h>
#include <string.h>
#include <unistd.h>
#include <stdlib.h>
#include <sys/types.h>
#include <net/ethernet.h>
#include <IOKit/IOKitLib.h>
#include <ClientKit/Common.h>
static io_connect_t conn;
static kern_return_t call(uint32_t sel, int set, void *buf, size_t sz) {
    size_t osz = sz;
    uint32_t s = sel | (set ? IOCTL_MASK : 0);
    if (set) return IOConnectCallStructMethod(conn, s, buf, sz, NULL, NULL);
    return IOConnectCallStructMethod(conn, s, NULL, 0, buf, &osz);
}
int main(int argc, char **argv) {
    io_service_t svc = IOServiceGetMatchingService(kIOMainPortDefault, IOServiceMatching("YYavicMtkWlan"));
    if (!svc) svc = IOServiceGetMatchingService(kIOMainPortDefault, IOServiceMatching("SimpleMtkWlan"));  /* eski ad (geri donus) */
    if (!svc) { fprintf(stderr, "YYavicMtkWlan servisi yok\n"); return 1; }
    kern_return_t kr = IOServiceOpen(svc, mach_task_self(), 0, &conn);
    if (kr != KERN_SUCCESS) { fprintf(stderr, "IOServiceOpen: 0x%x\n", kr); return 2; }
    struct ioctl_driver_info di = {0}; di.version = IOCTL_VERSION;
    kr = call(IOCTL_80211_DRIVER_INFO, 0, &di, sizeof(di));
    printf("driver_info kr=0x%x fw=%s drv=%s if=%s\n", kr, di.fw_version, di.driver_version, di.bsd_name);
    if (argc > 2 && !strcmp(argv[1], "dbg")) {
        struct ioctl_dbg d = { IOCTL_VERSION };
        d.op = (unsigned)strtoul(argv[2], NULL, 0);
        d.a = argc > 3 ? (unsigned)strtoul(argv[3], NULL, 16) : 0;
        d.b = argc > 4 ? (unsigned)strtoul(argv[4], NULL, 16) : 0;
        kr = call(IOCTL_80211_DBG, 1, &d, sizeof(d));
        struct ioctl_dbg r = {0};
        kern_return_t k2 = call(IOCTL_80211_DBG, 0, &r, sizeof(r));
        printf("dbg op=%u kr=0x%x/0x%x ret=0x%x : %s\n", d.op, kr, k2, r.ret, r.out);
        return 0;
    }
    if (argc > 1 && !strcmp(argv[1], "scan")) {
        struct ioctl_scan sc = { IOCTL_VERSION };
        kr = call(IOCTL_80211_SCAN, 0, &sc, sizeof(sc));
        printf("scan tetiklendi kr=0x%x, %d sn bekleniyor...\n", kr, argc > 2 ? atoi(argv[2]) : 8);
        sleep(argc > 2 ? atoi(argv[2]) : 8);
    }
    if (argc > 2 && !strcmp(argv[1], "join")) {
        /* Parola komut satirina degil, gizli istemden (getpass) alinir: kayit/gecmise girmez. */
        char prompt[96]; snprintf(prompt, sizeof(prompt), "\"%s\" icin Wi-Fi parolasi (bos = acik ag): ", argv[2]);
        char *pw = getpass(prompt);
        struct ioctl_join jn; memset(&jn, 0, sizeof(jn));
        jn.version = IOCTL_VERSION; jn.nwid.version = IOCTL_VERSION; jn.wpa_key.version = IOCTL_VERSION;
        jn.nwid.len = (unsigned)strlen(argv[2]); strncpy(jn.nwid.nwid, argv[2], NWID_LEN - 1);
        jn.wpa_key.len = (unsigned)strlen(pw); strncpy(jn.wpa_key.key, pw, WPA_KEY_LEN - 1);
        kr = call(IOCTL_80211_JOIN, 1, &jn, sizeof(jn));
        memset(&jn, 0, sizeof(jn)); memset(pw, 0, strlen(pw));
        printf("join istegi kr=0x%x\n", kr);
        for (int t = 0; t < 30; t++) {
            struct ioctl_state st2 = { IOCTL_VERSION, 0 };
            call(IOCTL_80211_STATE, 0, &st2, sizeof(st2));
            struct ioctl_sta_info si; memset(&si, 0, sizeof(si)); si.version = IOCTL_VERSION;
            call(IOCTL_80211_STA_INFO, 0, &si, sizeof(si));
            printf("t=%2ds state=%d ch=%u rssi=%d rate=%u ssid=\"%s\"\n", t, st2.state, si.channel, si.rssi, si.rate, si.ssid);
            if (st2.state == MTK80211_S_RUN) break;
            sleep(1);
        }
        return 0;
    }
    if (argc > 1 && !strcmp(argv[1], "status")) {
        struct ioctl_state st3 = { IOCTL_VERSION, 0 };
        call(IOCTL_80211_STATE, 0, &st3, sizeof(st3));
        struct ioctl_sta_info si; memset(&si, 0, sizeof(si)); si.version = IOCTL_VERSION;
        kr = call(IOCTL_80211_STA_INFO, 0, &si, sizeof(si));
        printf("state=%d (0=init 1=scan 2=auth 3=assoc 4=run) sta_info kr=0x%x ch=%u rssi=%d rate=%u ssid=\"%s\" bssid=%02x:%02x:%02x:%02x:%02x:%02x\n",
               st3.state, kr, si.channel, si.rssi, si.rate, si.ssid, si.bssid[0], si.bssid[1], si.bssid[2], si.bssid[3], si.bssid[4], si.bssid[5]);
        return 0;
    }
    int n = 0;
    for (int i = 0; i < 400; i++) {
        struct ioctl_network_info ni; memset(&ni, 0, sizeof(ni));
        kr = call(IOCTL_80211_SCAN_RESULT, 0, &ni, sizeof(ni));
        if (kr == kIONoScanResult || kr != KERN_SUCCESS) { if (kr != kIONoScanResult) printf("scan_result kr=0x%x\n", kr); break; }
        char ssid[33]; memcpy(ssid, ni.ssid, 32); ssid[32] = 0;
        n++;
        printf("%3d ch%-3u rssi %4d  %02x:%02x:%02x:%02x:%02x:%02x  rsn=%u akm=0x%x cipher=0x%x  \"%s\"\n", n, ni.channel, ni.rssi,
               ni.bssid[0], ni.bssid[1], ni.bssid[2], ni.bssid[3], ni.bssid[4], ni.bssid[5], ni.rsn_protos, ni.rsn_akms, ni.rsn_ciphers, ssid);
    }
    printf("toplam %d ag\n", n);
    struct ioctl_state st = { IOCTL_VERSION, 0 };
    kr = call(IOCTL_80211_STATE, 0, &st, sizeof(st));
    printf("state kr=0x%x state=%d\n", kr, st.state);
    return 0;
}
