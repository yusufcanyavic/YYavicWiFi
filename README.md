# YYavicWiFi — native Wi-Fi 6 for MediaTek MT7922 on macOS

**Developer:** Yusufcan Yaviç (Yusufcan Yavic) — YYavic

YYavicWiFi is a single macOS kernel extension that makes a **MediaTek MT7922**
PCIe card (AMD sells MT7922-based modules as **RZ616**) work as a **native
macOS Wi-Fi card**: it
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
- Bluetooth works natively through macOS itself: the kext sets it up, plus BlueToolFixup and a one-time firmware copy (see "Bluetooth" below)

## Supported hardware

| Card | PCI ID | Status |
|---|---|---|
| MediaTek MT7922 | `14c3:7922` | **Tested** — the developer's card (AzureWave, subsystem `1a3b:5911`) |
| AMD RZ616 modules that report `14c3:0616` | `14c3:0616` | Matched since **0.7.1**, **untested** — Linux `mt7921` drives this ID as an MT7922 (MT7922 firmware); the kext does the same |
| MediaTek MT7921 / MT7921K | `14c3:7961` / `14c3:0608` | Matched, **untested**; stays at 80 MHz like Linux |

The test machine's IDs: Wi-Fi PCI **`14c3:7922`** (chip id register `0x7922`),
Bluetooth USB **`13D3:3585`** (AzureWave). "RZ616" is a product name: the
developer has not had a card with the ID `0616`. Reports from `0616` owners
(working or not, with the `mtkscan status` output) are welcome in the issues.

**Find your IDs:**

```sh
ioreg -l | grep -o 'pci14c3,[0-9a-f]*' | sort -u          # Wi-Fi: pci14c3,7922 or pci14c3,616
ioreg -p IOUSB -l -w0 | grep -E '"(idVendor|idProduct|USB Product Name)"'   # Bluetooth (decimal)
```

or System Information → PCI / USB. The kext matches by PCI ID, then uses the
chip id register (`0x7922` = MT7922, as Linux `is_mt7922()`), so no ID
spoofing, boot-arg or plist edit is needed for the IDs above.

## Requirements

- **macOS 15.8.1 (build 24H32) only.** The native layer is built against the
  exact IO80211Family/IOSkywalkFamily class layouts of this build. On any other
  macOS build the layer **disables itself** at boot (log: `YYavicWiFi: native
  Wi-Fi layer disabled, unsupported macOS`) instead of panicking; the boot-arg
  `-yyavicnoverify` skips this check (only for testers, a wrong layout panics).
- A card from "Supported hardware" above.
- Your country set with the boot-arg `yyavic-cc=XX` (see "Country and channels").
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

## Country and channels

Set your country with the boot-arg **`yyavic-cc=XX`** (ISO 3166 two-letter
code, e.g. `yyavic-cc=DE`, `yyavic-cc=US`). The radio then uses that region's
channels, and the code is passed to the firmware and to macOS. Without it the
kext uses only the channels allowed in all of the regions below.

| Region | Countries | 2.4 GHz | 5 GHz |
|---|---|---|---|
| ETSI | EU, `IS` `LI` `NO` `CH` `GB` `TR` | 1–13 | 36–64, 100–140 |
| FCC | `US` `PR` | 1–11 | 36–64, 100–144, 149–165 |
| Japan | `JP` | 1–13 | 36–64, 100–140 |
| China | `CN` | 1–13 | 36–64, 149–165 |
| Default (no or other country) | — | 1–11 | 36–64 |

Channels 52–144 are DFS channels: macOS sees them as such, and the radio's scan is
passive (it sends no probe requests). Other countries are not in the
table yet and get the default set (the code still goes to the firmware).
Pull requests that add countries are welcome, with a source (e.g. the Linux
`wireless-regdb` entry). Using channels that are not allowed where you are is
your responsibility.

Versions up to 0.7.0 always used the Turkish channel list (the developer's
country); 0.7.1 made it configurable. To keep that list, use `yyavic-cc=TR`.

Check the active country: `./mtkscan dbg 37 0` (see Diagnostics).

## Boot arguments

| Boot-arg | Effect |
|---|---|
| `yyavic-cc=XX` | Country / regulatory region (see above); default: common channels only |
| `-yyavicnobt` | Do not set the Bluetooth NVRAM values (see "Bluetooth") |
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
- Built-in channel rules cover the regions in "Country and channels" only.
- `14c3:0616` (RZ616), MT7921 and MT7921K are matched but untested.
- No sleep/wake support, no 6 GHz, no AirDrop/AWDL, no Personal Hotspot (AP).

## Bluetooth

The MT7922's Bluetooth is a separate USB device. On macOS 15, USB Bluetooth
adapters are run by macOS's own `bluetoothd`, which already has a MediaTek
MT7922 path, but only for the USB ID `0489:E0D8`, with the MediaTek firmware
in `/etc/bluetool/`, and only when it believes the Mac has no internal
controller. Since **0.7.2** `YYavicWiFi.kext` takes care of the first and
the last point itself:

- it makes the MT7922 Bluetooth USB IDs known to Linux (`btusb`: 27 IDs,
  e.g. `13D3:3585`, `13D3:3610`, `0489:E0E2`, `0489:E0F6`, `04CA:3804`,
  `0E8D:223C`, `35F5:7922`) appear as `0489:E0D8`;
- on an MT7922 card it sets the NVRAM values `bluetoothInternalControllerInfo`
  (14 zero bytes) and `bluetoothExternalDongleFailed` (`00`) at boot, only
  when they are missing or different. Boot-arg `-yyavicnobt` turns this off.

**Tested on the test machine:** Bluetooth on, System Information shows
`Chipset: SRS_7922`, `Transport: USB`; phone pairing works and survives
reboots. **Not tested:** Bluetooth audio (headphones), Handoff / Continuity,
other Bluetooth IDs than `13D3:3585`. AirDrop is not supported (it needs Wi-Fi
AWDL).

### Setup

1. **Kexts** — in `EFI/OC/Kexts` and `Kernel → Add` (Lilu before BlueToolFixup):
   - `YYavicWiFi.kext` (you already have it for Wi-Fi)
   - [Lilu](https://github.com/acidanthera/Lilu) (tested 1.7.3)
   - `BlueToolFixup.kext` from [BrcmPatchRAM](https://github.com/acidanthera/BrcmPatchRAM)
     (tested 2.7.2) — **required**: without it macOS shows a placeholder
     Broadcom controller and Bluetooth stays off (tested).

2. **Firmware, once** — in Terminal, from the release folder:

   ```sh
   sudo sh Bluetooth/setup-bluetooth.sh
   ```

   It copies `BT_RAM_CODE_MT7922_1_1_hdr.bin` to `/etc/bluetool/` (and does
   nothing if it is already there).

3. **USB map** — the Bluetooth device's USB port must be included in your
   USB port map (as an internal port).

4. Reboot and check System Information → Bluetooth: `Chipset: SRS_7922`,
   `Transport: USB`, `State: On`.

**Updating from 0.7.0 / 0.7.1:** `MtkBtIdMask.kext` and the two
`bluetooth…` entries in `NVRAM → Add` are no longer needed; you can remove
them (keeping them does no harm).

**Your Bluetooth ID is not in the list?** (System Information → USB → the
MediaTek / `Wireless_Device` entry.) Use `Bluetooth/MtkBtIdMask.kext`: set
`idVendor` / `idProduct` (the matching keys, **decimal**) to your ID, e.g.
`0x13D3` = `5075`, `0x3585` = `13701`, leave the `IOProviderMergeProperties`
values (`1161` / `57560` = `0489:E0D8`) as they are, and add it to
`Kernel → Add`. Please also report the ID on GitHub so it can be added.

### Troubleshooting

- Check what the kext did: `ioreg -l | grep yyavic-bt` — e.g.
  `bluetoothInternalControllerInfo=written …` (first boot) or `=present`.
- **Chipset `BCM_4350C2`, Transport `UART`, Bluetooth off:** BlueToolFixup
  (or Lilu) is not loaded.
- **Bluetooth off / not available, log repeats `HCI initialization failed`:**
  your Bluetooth USB ID is not masked (see above), or the firmware file is
  missing (step 2).
- Check the state any time with `system_profiler SPBluetoothDataType`.

## Building from source

Requirements: macOS with Xcode (full Xcode, not only the Command Line Tools).
The MacKernelSDK (acidanthera) copy is included in `Source/YYavicMtkWlan/MacKernelSDK`.

```sh
cd Source
DEVELOPER_DIR=/Applications/Xcode.app/Contents/Developer YY_VERSION=0.7.2 ./build-yyavic.sh release
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
./mtkscan dbg 37 0        # country, region and number of active channels
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
- `Bluetooth/MtkBtIdMask.kext` (plist only) and `Bluetooth/setup-bluetooth.sh`: GPL-3.0-or-later.
- MacKernelSDK is under the Apple Public Source License, see its `LICENSE.txt`.

See `CREDITS.md` for the people whose work this builds on.
