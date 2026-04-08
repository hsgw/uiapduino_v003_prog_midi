# MIDI Blink Example for CH32V006

This example demonstrates basic MIDI UART communication with LED blink feedback on the CH32V006 microcontroller.

## Features

- Receives MIDI messages via UART at 31250 baud (standard MIDI speed)
- Blinks an LED when MIDI messages are received
- Supports running status and all standard MIDI message types
- Uses DMA for efficient UART reception

## Hardware Requirements

- CH32V006 development board or custom circuit
- LED with current-limiting resistor (e.g., 330Ω)
- MIDI IN circuit (optocoupler-based)
- MIDI OUT circuit (optional, for testing transmission)

## Pin Configuration

| Pin | Function | Description |
|-----|----------|-------------|
| PC0 | LED Output | Connect LED (anode) through resistor to ground |
| PD5 | UART1 TX | MIDI OUT (via driver circuit) |
| PD6 | UART1 RX | MIDI IN (via optocoupler) |

## Wiring Diagram

### LED Connection
```
PC0 ----[330Ω]----|>|---- GND
                 LED
```

### MIDI IN Connection
Use a standard 6N138 or similar optocoupler circuit connected to PD6.

### MIDI OUT Connection
Use a standard MIDI driver circuit (e.g., 74HC04 or transistor) connected to PD5.

## Building

```bash
cd examples/midi_blink
make
```

To build for CH32V003 instead:
```bash
make TARGET_MCU=CH32V003
```

## Flashing

```bash
make flash
```

Or manually with WCH-LinkE:
```bash
openocd -f interface/wch-link.cfg -f target/ch32v003.cfg -c "program midi_blink.elf verify reset exit"
```

## Usage

1. Flash the firmware to your CH32V006
2. Connect a MIDI source to the MIDI IN port
3. The LED on PC0 will blink whenever a MIDI message is received
4. Real-time messages (0xF8-0xFF) will trigger immediate blinks
5. Channel and system messages will blink when fully received

## How It Works

1. **UART Initialization**: The UART is configured for 31250 baud with DMA circular buffer
2. **MIDI Parsing**: The main loop processes incoming bytes and detects complete MIDI messages
3. **LED Feedback**: When a valid MIDI message is detected, the LED blinks for ~100ms
4. **Running Status**: Full MIDI running status protocol is supported for efficient transmission

## Supported MIDI Messages

- Note On/Off (0x80-0x9F)
- Polyphonic Key Pressure (0xA0-0xAF)
- Control Change (0xB0-0xBF)
- Program Change (0xC0-0xCF)
- Channel Pressure (0xD0-0xDF)
- Pitch Bend (0xE0-0xEF)
- System Common messages (MTC, Song Position, Song Select, Tune Request)
- Real-time messages (Timing Clock, Start, Stop, Continue, Active Sensing, System Reset)

## Troubleshooting

**LED not blinking:**
- Check MIDI IN circuit wiring
- Verify MIDI source is sending data
- Confirm UART RX pin (PD6) is receiving data with an oscilloscope

**LED stays on:**
- Check LED wiring and current-limiting resistor
- Verify PC0 pin configuration

**No MIDI messages detected:**
- Verify baud rate is correct (31250 for MIDI)
- Check that MIDI messages are properly formatted
- Ensure running status is handled correctly by your MIDI source

## License

This example is provided as-is for testing and development purposes.
