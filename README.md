(WIP: DO NOT USE)

# usb prog-midi interface for UIAPduino v006

This project provides firmware for the UIAPduino v006 board, leveraging the RV003USB platform. It offers both a USB programmer functionality and MIDI capabilities, allowing for flexible device interaction.

The UIAPduino v006 board features a CH32V003 for USB connectivity. By flashing this firmware to the CH32V003, you can add USB-MIDI functionality in addition to programming capabilities for the CH32V006.

## Features

- USB Connectivity via RV003USB
- RVSWDIO Programming Interface
- MIDI Device Support
- Configurable modes (Programmer/MIDI)

## Tips

- **DO NOT USE PC0 on v003 before disable reset on v006**
- Disable RESET pin of v006 (https://github.com/cnlohr/rv003usb/blob/master/bootloader/Makefile#L17)
  1. connect programmer to v006
  2. `minichlink -w +a55adf20 option`

## Pin Assignments

The following pin assignments are used for the uiapduino v003 board:

- **Mode Selection (PIN_MODE_SWITCH):** `PC3`
  - If `PC3` is pulled HIGH (e.g., connected to 3.3V or left floating with internal pull-up enabled), the device starts in **MIDI Mode**.
  - If `PC3` is pulled LOW (e.g., connected to GND), the device starts in **Programmer Mode**.

### Programmer Mode (RVSWDIO)

- **SWIO (Data):** `PC1`
- **SWIO (Clock):** `PC2`
  - **Note:** These pins are remapped due to `RVBB_REMAP` being enabled in the firmware. There is a 5.1k pull-up resistor required on `PD2` for these lines.
- **SWCLK:** `PC5` (for target devices like V2xx, V3xx, X0xx)
- **Target Power Control (PIN_TARGETPOWER):** `PD2`
  - `PD2` is used to switch the power to the target device.

### MIDI Mode (UART)

- **Baud Rate:** `31250 bps` (Standard MIDI Baud Rate)
- **MIDI TX:** `PD6` (Output)
- **MIDI RX:** `PD5` (Input, with internal pull-up)

### Other

- **Status LED:** `PC0` (Optional, connected to a status LED)

## Usage

The uiapduino v0.0.3 operates in two main modes: Programmer Mode and MIDI Mode. The mode is determined at startup by the state of `PIN_MODE_SWITCH` (`PC3`).

### Mode Selection at Startup

Connect `PC3` to HIGH or LOW before powering on the device to select the desired mode.

### Programmer Mode

This mode makes the uiapduino v0.0.3 compatible with the RVSWDIO programmer.

1.  **Select Mode:** Ensure `PC3` is LOW when powering on the device.
2.  **Flash Device:** The device can be flashed using a compatible RVSWDIO programmer.

### MIDI Mode

1.  **Select Mode:** Ensure `PC3` is HIGH when powering on the device.
2.  **Ensure Firmware Configuration:** Verify that the firmware has been compiled and flashed with MIDI mode enabled (as configured in `usb_config.h` and built into the firmware).
3.  **Connect via USB:** Connect the uiapduino board to your computer using a USB cable.
4.  **System Recognition:** Your operating system should automatically detect the device as a MIDI input/output.
5.  **Use with Software:** Open your Digital Audio Workstation (DAW) or MIDI software and select the newly recognized MIDI device from its input/output preferences.

## License

This project is licensed under the MIT License - see the `LICENSE` file for details.
Copyright (c) 2026 Takuya Urakawa (@hsgw 5z6p.com)

## Third-Party Licenses

This project incorporates code from the following third-party projects, each licensed under the MIT License:

- **rv003usb:**
  - Copyright (c) 2023 CNLohr
  - Full license text available in `rv003usb/LICENSE`.

- **ch32fun:**
  - Copyright (c) 2023-2024 CNLohr <lohr85@gmail.com>, et. al.
  - Copyright (c) 2021 Nanjing Qinheng Microelectronics Co., Ltd.
  - Copyright (c) 2023-2024 E. Brombaugh
  - Copyright (c) 2023-2024 A. Mandera
  - Copyright (c) 2005-2020 Rich Felker, et al.
  - Copyright (c) 2013,2014 Michal Ludvig <michal@logix.cz>
  - Full license text available in `rv003usb/ch32fun/LICENSE`.
