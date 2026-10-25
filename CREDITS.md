# Credits

**YYavicWiFi** is developed by **Yusufcan Yaviç (Yusufcan Yavic, YYavic)**:
native IO80211/Skywalk layer, single-kext packaging, Wi-Fi 6 (802.11ax),
160 MHz, A-MSDU, BlockAck 256, asynchronous TX, performance and stability work.

It builds on open-source work whose copyright notices and license headers are
kept in the source files:

- **zxystd (钟先耀)** - the macOS Wi-Fi driver framework, compatibility layer
  and OpenBSD `net80211` port (itlwm / AirportItlwm) the radio uses.
- **OpenBSD `mwx`** - Claudio Jeker; with code and definitions from MediaTek
  Inc., Lorenzo Bianconi, Felix Fietkau, Stefan Sperling, Damien Bergamini
  (ISC license, see `hal_mwx/MtkMwx.cpp`, `hal_mwx/if_mwxreg.h`).
- **OpenBSD / NetBSD / FreeBSD** - `net80211`, crypto and support code.
- **Linux `mt76` / `mt7921`** (MediaTek, Felix Fietkau, Lorenzo Bianconi and
  contributors) - reference for Wi-Fi 6 capabilities, firmware command layouts,
  ring sizes and the 160 MHz channel context.
- **Linux `btusb`** (Bluetooth USB driver contributors) - the list of MT7922 /
  MT7922A Bluetooth USB IDs the kext masks.
- **acidanthera** - MacKernelSDK; Lilu and BlueToolFixup (BrcmPatchRAM), used
  for Bluetooth (not included, see README).
- **MediaTek Inc.** - MT7922 Wi-Fi and Bluetooth firmware (`LICENCE.mediatek`).
- **laobamac** - SimpleMtkWlan, the early MT7921/MT7922 macOS port of
  OpenBSD `mwx` the radio started from (firmware loading, scan,
  authentication).
