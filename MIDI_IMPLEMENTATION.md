# Serial MIDI Implementation Notes

This document details the implementation of serial MIDI communication for the v003_prog_midi project.

## Overview
The device can operate in two modes: USB-MIDI or RVSWDIO Programmer, selected by the state of the PC3 pin at startup. **In MIDI mode, the device is recognized by the host computer as a USB-MIDI device, enabling bidirectional conversion between UART MIDI data and USB MIDI data.** This section focuses on the MIDI mode's serial (UART) communication aspects.

## Serial MIDI (UART) Handling:

### 1. Data Flow:
   - **UART Receive -> Memory Buffer (DMA):** Incoming MIDI data from UART is buffered using DMA in circular mode. This offloads CPU during reception.
   - **Memory Buffer -> USB Transmit (CPU):** When a complete MIDI message is detected (via UART IDLE line detection or buffer threshold), the CPU reads the buffered data and transmits it via USB using the `rv003usb` library.

### 2. Key Components & Functions:
   - **`PIN_MODE_SWITCH` (PC3):** Determines the startup mode (MIDI vs Programmer).
   - **`is_midi_mode` (uint8_t):** Global flag indicating the current USB mode.
   - **`midi_receive(uint8_t * msg)`:** Processes incoming MIDI messages from the UART buffer. It parses the message (e.g., Note On/Off) and updates the TIM2 period for sound generation.
   - **`midi_send(uint8_t * msg, uint8_t len)`:** Prepares MIDI messages to be sent over USB.
   - **`scan_keyboard()`:** Scans GPIO pins (PC2-PC5) for keyboard input and generates MIDI Note On/Off messages.
   - **`tim2_init()` / `tim2_set_period()`:** Configures Timer 2 to generate PWM signals based on received MIDI note data for sound output.
   - **DMA Configuration for UART RX:**
     - Enabled in `main()` function (or a dedicated initialization function).
     - Uses Circular Mode for continuous buffering.
     - Triggered by UART RX data availability.
   - **UART IDLE Line Detection:**
     - Used to detect the end of a MIDI message, signaling the CPU to process the buffered data.

### 3. Design Considerations:
   - **DMA for efficient buffering:** Offloads CPU during UART reception.
   - **CPU intervention for USB transmission:** Required due to the software USB implementation.
   - **MIDI message framing:** Handled by a combination of buffer thresholds and UART IDLE detection.
   - **Timers for MIDI output:** TIM2 is used to generate varying frequencies for sound.
   - **Mode Selection:** Currently uses PC3 pin at startup. This physical switch/pin dependency and the TIM2 buzzer functionality are specific to this implementation and might be removed or changed in future revisions.

---
**Note:** This implementation draws heavily from the `rv003usb/testing/demo_midi` example but is integrated into the main firmware for dual-mode operation.
