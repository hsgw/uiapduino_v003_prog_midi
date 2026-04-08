# MIDI Blink Example for UIAPduino v006

This example demonstrates basic MIDI UART communication with LED blink feedback on the UIAPduino v006.

## Features

- Receives MIDI messages via UART at 31250 baud (standard MIDI speed)
- Blinks an LED and echoback midi messages when MIDI messages are received
- Supports running status and all standard MIDI message types
- Uses DMA for efficient UART reception

## Building

```bash
cd examples/midi_echoback
make build
```

## Supported MIDI Messages

- Note On/Off (0x80-0x9F)
- Polyphonic Key Pressure (0xA0-0xAF)
- Control Change (0xB0-0xBF)
- Program Change (0xC0-0xCF)
- Channel Pressure (0xD0-0xDF)
- Pitch Bend (0xE0-0xEF)
- System Common messages (MTC, Song Position, Song Select, Tune Request)
- Real-time messages (Timing Clock, Start, Stop, Continue, Active Sensing, System Reset)

## License

This example is provided as-is for testing and development purposes.
