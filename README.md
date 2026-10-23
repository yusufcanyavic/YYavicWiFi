# YYavicWiFi — native Wi-Fi 6 for MediaTek MT7922 on macOS

**Developer:** Yusufcan Yaviç (Yusufcan Yavic) — YYavic

YYavicWiFi is a single macOS kernel extension that makes a **MediaTek MT7922**
PCIe card (often sold as AMD RZ616) work as a **native macOS Wi-Fi card**: it
shows up in System Settings and the menu bar like an Apple card, uses macOS's
own Wi-Fi stack (IO80211 / Skywalk, airportd, Keychain, auto-join) and needs
no extra app.

It consists of two parts linked into one kext:

- **Radio** (`YYavicMtkWlan`): the MT7922 driver (firmware, DMA, 802.11
  state machine, Wi-Fi 6). Built on zxystd's macOS Wi-Fi framework and
  OpenBSD's `mwx` and `net80211`.
- **Native layer** (`YYavicAirWiFiLayer`): an `IO80211Controller` /
  `IO80211InfraInterface` / Skywalk data path written for macOS Sequoia, so the
  card is driven by macOS's WCL (Wi-Fi Control Layer) like Apple's own drivers.

The radio started from laobamac's **SimpleMtkWlan**, an early MT7921/MT7922
port of OpenBSD's `mwx` that loaded firmware, scanned and authenticated but
could not associate or pass data. Association, the data path,
802.11n/ac/ax, 160 MHz, A-MSDU, all performance work and the complete native
macOS Wi-Fi layer were developed in YYavicWiFi by Yusufcan Yaviç.

> **Status: experimental.** Tested on one machine and one access point (see
> below). Back up your EFI before installing and keep a working fallback.

## Measured result

- **Machine:** Tongfang-chassis R9T Slayer laptop, AMD Ryzen 9 8945HX,
  SMBIOS MacBookPro16,2, macOS **15.8.1 (24H32)**, OpenCore.
- **Access point:** Wi-Fi 6 router, 5 GHz channel 56, 160 MHz, WPA2-PSK.
- **Internet:** an ISP line of **1 Gbit/s download / 50 Mbit/s upload**. All
  speed results below were measured on this line, so downloads are capped
  near 1 Gbit/s and uploads at 50 Mbit/s by the ISP, not by the driver.

| Driver version | Mode | Speedtest (Chrome) down / up |
|---|---|---|
| 0.5.3 | 802.11ac, 80 MHz | 430 / 47 Mbit/s |
| 0.6.0 | 802.11ax (HE), 80 MHz | 576 / 47 |
| 0.6.4 | + BA 256, A-MSDU | 760 / 47 |
| **0.7.0** | **802.11ax, 2x2, 160 MHz** (link 2402 Mbit/s) | **913–920 / 46–47** |

The same machine on Windows reaches about 900 Mbit/s on this line. Upload is
limited by the 50 Mbit/s ISP line: faster uploads have not been measured.

## Features

- Native macOS Wi-Fi: network list, password dialog, Keychain, auto-join,
  Wi-Fi on/off, private Wi-Fi address, link details in System Information
- Wi-Fi 6 (802.11ax / HE), 2 spatial streams, HE-MCS 0–11
- 20/40/80/**160 MHz** (160 MHz only on MT7922); 5 GHz tested, 2.4 GHz see limitations
- A-MPDU in both directions (RX BlockAck window 256), RX A-MSDU (up to 11454-byte MPDUs)
- LDPC, STBC, SU/MU beamformee (as advertised by Linux mt7921)
- WPA2-Personal (CCMP, hardware encryption)
- Bluetooth works natively through macOS itself with the files in `Bluetooth/` (see "Bluetooth" below)

## Requirements

- **macOS 15.8.1 (build 24H32) only.** The native layer is built against the
  exact IO80211Family/IOSkywalkFamily class layouts of this build. On any other
  macOS build the layer **disables itself** at boot (log: `YYavicWiFi: native
  Wi-Fi layer disabled, unsupported macOS`) instead of panicking; the boot-arg
  `-yyavicnoverify` skips this check (only for testers, a wrong layout panics).
- A MediaTek **MT7922** card (PCI `14c3:7922`). The kext also matches MT7921
  (`14c3:7961`) and MT7921K (`14c3:0608`) — **untested**; on those it stays at
  80 MHz like Linux.
- OpenCore. Tested settings: `SecureBootModel = Disabled`, SIP partly disabled
  (`csr-active-config = 0x00000A43`). Other combinations are untested.
- Remove/disable any other driver for this card (itlwm/AirportItlwm-style MTK
  ports, older YYavicMtkWlan/SimpleMtkWlan kexts).

## Installation (OpenCore)

1. Back up your EFI.
2. Copy `Kext/YYavicWiFi.kext` to `EFI/OC/Kexts/`.
3. Add it to `config.plist` → `Kernel` → `Add`:

   ```xml
   <dict>
       <key>Arch</key><string>Any</string>
       <key>BundlePath</key><string>YYavicWiFi.kext</string>
       <key>Comment</key><string>YYavicWiFi (MT7922 native Wi-Fi)</string>
       <key>Enabled</key><true/>
       <key>ExecutablePath</key><string>Contents/MacOS/YYavicWiFi</string>
       <key>MaxKernel</key><string></string>
       <key>MinKernel</key><string></string>
       <key>PlistPath</key><string>Contents/Info.plist</string>
   </dict>
   ```

4. Reboot. About 20–30 seconds after the desktop appears the native layer
   starts and Wi-Fi shows up in the menu bar. Join your network as usual.

The native interface is `en3`-style (a new Wi-Fi port). The radio also
publishes an inactive Ethernet-type interface; that is expected.

### Uninstall / fallback

Remove the `Kernel → Add` entry (or set `Enabled` to false) and reboot.

If a boot goes wrong while the native layer starts, the next boot skips it
automatically (NVRAM guard `yyavic-arm-guard`). To force the radio-only mode,
boot with `-yyavicoff`.

## Boot arguments

| Boot-arg | Effect |
|---|---|
| `-yyavicoff` | Do not start the native layer (radio only, no macOS Wi-Fi) |
| `-yyavicnohe` | Disable 802.11ax (connect as 802.11ac) |
| `-yyavicnodrvavail` | Do not post the boot-time "driver available" event |
| `-yyavicnoverify` | Skip the macOS 15.8.1 check (testers only, may panic) |

## Known limitations

- **WPA3-only** networks and **802.1X / Enterprise** (eduroam, company
  networks) are not supported yet.
- Roaming between access points of the same network is incomplete (may
  disconnect and reconnect).
- 2.4 GHz, open and hidden networks have not been re-tested since the Wi-Fi 6
  work.
- Uploads above 50 Mbit/s have not been measured.
- System Information shows "802.11ac" and "80MHz" for the current network even
  when the link is 802.11ax / 160 MHz (cosmetic; the transmit rate, e.g. 2402,
  is correct). The cause is understood (macOS defers loading the card
  capabilities); the fix is not safe yet.
- The channel list is fixed to 2.4 GHz 1–13 and 5 GHz 36–64, 100–140.
- No sleep/wake support, no 6 GHz, no AirDrop/AWDL, no Personal Hotspot (AP).

## Bluetooth

The MT7922's Bluetooth is a separate USB device and is **not** driven by
`YYavicWiFi.kext`. On macOS 15 USB Bluetooth adapters are run by macOS's own
`bluetoothd`, which already has a MediaTek MT7922 path — but it only takes it
for the USB ID `0489:E0D8`, needs the MediaTek firmware file in
`/etc/bluetool/`, and skips USB adapters entirely when it believes the Mac has
an internal controller. The files in `Bluetooth/` and the steps below make
macOS use that path natively.

**Tested on the test machine:** Bluetooth on, System Information shows
`Chipset: SRS_7922`, `Transport: USB`; phone pairing works and survives
reboots. **Not tested:** Bluetooth audio (headphones), Handoff / Continuity.
AirDrop is not supported (it needs Wi-Fi AWDL).

### Setup

1. **Kexts** — add to `EFI/OC/Kexts` and `Kernel → Add` (Lilu first):
   - [Lilu](https://github.com/acidanthera/Lilu) (tested 1.7.3)
   - `BlueToolFixup.kext` from [BrcmPatchRAM](https://github.com/acidanthera/BrcmPatchRAM) (tested 2.7.2)
   - `Bluetooth/MtkBtIdMask.kext` (included)

2. **Check your card's Bluetooth USB ID** — System Information → USB → the
   MediaTek / `Wireless_Device` entry → Vendor ID and Product ID.
   - `0x13D3` / `0x3585` (AzureWave, the tested card): use `MtkBtIdMask.kext` as it is.
   - `0x0489` / `0xE0D8`: you don't need `MtkBtIdMask.kext`.
   - Anything else: edit `MtkBtIdMask.kext/Contents/Info.plist` and set
     `idVendor` / `idProduct` (the matching keys, **decimal**) to your ID,
     e.g. `0x13D3` = `5075`, `0x3585` = `13701`. Leave the
     `IOProviderMergeProperties` values (`1161` / `57560` = `0489:E0D8`) as they are.

3. **Firmware** — copy the MediaTek Bluetooth firmware where `bluetoothd`
   looks for it (needs your password):

   ```sh
   sudo mkdir -p /etc/bluetool
   sudo cp Bluetooth/BT_RAM_CODE_MT7922_1_1_hdr.bin /etc/bluetool/
   ```

4. **NVRAM** — in `config.plist`, `NVRAM → Add →
   7C436110-AB2A-4BBB-A880-FE41995C9F82` add:

   | Key | Type | Value |
   |---|---|---|
   | `bluetoothInternalControllerInfo` | Data | `0000000000000000000000000000` (14 zero bytes) |
   | `bluetoothExternalDongleFailed` | Data | `00` |

   and add both key names to `NVRAM → Delete →
   7C436110-AB2A-4BBB-A880-FE41995C9F82`, so OpenCore sets them on every boot.

5. **USB map** — the Bluetooth device's USB port must be included in your
   USB port map (as an internal port).

6. Reboot and check System Information → Bluetooth: `Chipset: SRS_7922`,
   `Transport: USB`, `State: On`.

### Troubleshooting

- **Bluetooth off / not available, log repeats `HCI initialization failed`:**
  `MtkBtIdMask.kext` is not loaded or does not match your USB ID (step 2).
- **No Bluetooth at all, no errors:** the NVRAM values from step 4 are missing.
  To test without rebooting:
  `sudo nvram bluetoothInternalControllerInfo=%00%00%00%00%00%00%00%00%00%00%00%00%00%00`
  — `bluetoothd` picks it up within about 30 seconds.
- Check the state any time with `system_profiler SPBluetoothDataType`.

## Building from source

Requirements: macOS with Xcode (full Xcode, not only the Command Line Tools).
The MacKernelSDK (acidanthera) copy is included in `Source/YYavicMtkWlan/MacKernelSDK`.

```sh
cd Source
DEVELOPER_DIR=/Applications/Xcode.app/Contents/Developer YY_VERSION=0.7.0 ./build-yyavic.sh release
# result: Source/build-release/YYavicWiFi.kext
```

`build-yyavic.sh` builds the radio with its Xcode project, the native layer
with `YYavicAirWiFiLayer/build.sh`, and links both into one kext with one
merged `Info.plist`.

## Diagnostics

`Tools/mtkscan/mtkscan.c` talks to the radio's user client:

```sh
clang -I Source/YYavicMtkWlan/include -framework IOKit -framework CoreFoundation \
      -o mtkscan Tools/mtkscan/mtkscan.c
./mtkscan status          # link state, channel, RSSI
./mtkscan dbg 33 0        # 802.11ax state and received HE rates
./mtkscan dbg 35 0        # AP channel width (VHT operation) and our 160 MHz state
./mtkscan dbg 34 0        # A-MSDU / TX kick counters
```

When reporting a problem, include the macOS build, the `mtkscan status`
output and any `/Library/Logs/DiagnosticReports/Kernel-*.panic` file.

## License

- The native layer (`Source/YYavicAirWiFiLayer`), the build script, the
  tools and all changes by Yusufcan Yavic: **GPL-3.0-or-later** (see
  `LICENSE`). Changed upstream files carry a "Reworked by Yusufcan Yavic"
  notice at the top that lists what was added; their original notices stay
  in force.
- The radio base (`Source/YYavicMtkWlan`) keeps its upstream licenses: the
  GPL notices in its source files (GPL-2.0-or-later), its `LICENSE`, and the
  BSD/ISC notices of the OpenBSD code. Those notices are authoritative for
  their files.
- **MediaTek firmware** — embedded in the kext (`include/FwBinary.cpp`) and
  the Bluetooth firmware `Bluetooth/BT_RAM_CODE_MT7922_1_1_hdr.bin` (from
  linux-firmware) — is not covered by the GPL; it is redistributed under
  MediaTek's terms, see `LICENCE.mediatek`.
- `Bluetooth/MtkBtIdMask.kext` (plist only): GPL-3.0-or-later.
- MacKernelSDK is under the Apple Public Source License, see its `LICENSE.txt`.

See `CREDITS.md` for the people whose work this builds on.
