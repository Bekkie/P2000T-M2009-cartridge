# P2000T M2009 Cartridge

Firmware and development tools for connecting a Philips P2000T to network services through modem-cartridge hardware.

## Project

| PlatformIO environment | Hardware | Purpose |
| --- | --- | --- |
| `m2009-emulator` | Raspberry Pi Pico W (RP2040) | Emulates the Miniware M2009 autodial/auto-answer modem and provides network access for the P2000T. |
| `viditel-modem-emulator` | ESP32 Dev Module | Emulates a serial V.24 Viditel modem and connects the P2000T to Viewdata and other network services. |
| `ftpclient` | Native build tools | Assembles the P2000T FTP client cassette image and generates download pages used by the M2009 firmware. |

The embedded projects share code in `lib/shared_files/`. Board-specific pin assignments and build definitions are in `lib/shared_files/config.h` and `platformio.ini`.

## Build

Install Python 3.10 or later and PlatformIO Core:

```powershell
python -m pip install --upgrade platformio
```

Run commands from this directory, where `platformio.ini` is located. To build all default environments:

```powershell
pio run
```

The M2009 firmware and FTP client build scripts invoke `tools/z80asm.exe` to assemble `ftpclient/ftp-client.asm`. This bundled executable is for Windows, so those environments currently require Windows. The Viditel firmware can be built separately:

```powershell
pio run -e viditel-modem-emulator
```

The FTP client environment can be built with:

```powershell
pio run -e ftpclient
```

The generated cassette image is written to `ftpclient/ftp-client.cas`; generated M2009 download-page data is written to `m2009-emulator/download_pages.h`.

## Firmware outputs

PlatformIO writes build products under `.pio/build/`:

- `viditel-modem-emulator/firmware.bin` for the ESP32.
- `m2009-emulator/firmware.uf2` for the Raspberry Pi Pico W.

The GitHub Actions workflow is configured to attach these firmware files to tagged releases. On the M2009 emulator, the Wi-Fi configuration network is named `M2009-Config-Portal`. The serial monitor speed is 115200 baud.

## Repository layout

- `m2009-emulator/` - Pico W M2009 firmware and network handling.
- `viditel-modem-emulator/` - ESP32 Viditel modem firmware.
- `ftpclient/` - P2000T Z80 FTP client source and cassette image.
- `lib/shared_files/` - Code shared by the modem firmware targets.
- `test-server/` - Local P2000T FTP and socket test servers.
- `tools/` - Build and download-page generation scripts, plus the Z80 assembler executable.
- `.github/workflows/` - GitHub Actions build and release workflow.

## Documentation

- [FTP client and server](../docs/ftp-client-and-server.md)
- [Viewdata server](../docs/viewdata-server.md)
- [M2009 modem emulator](../docs/m2009-modem-emulator.md)
- [Viditel modem emulator](../docs/viditel-modem-emulator.md)

## License

Unless a source file contains its own license notice, this project is licensed under the GNU General Public License, version 3 only. Files with an explicit MIT license notice retain that license. `test-server/myftpserver.py` currently carries an "All rights reserved" notice and is not relicensed by this file; clarify its license with the copyright holder before redistributing it. See [LICENSE](LICENSE) and the notices in the relevant source files.

## Contributing

Bug reports and improvements are welcome. Please include the target hardware, PlatformIO environment, build output, and steps to reproduce when reporting a problem. Keep pull requests focused and update the changelog when user-visible behavior changes.
