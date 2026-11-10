# Keep talking and everybody explodes - OmniCTF 2026 Finals

Hardware hacking challenge from the finals phase of OmniCTF 2026. This guide shows you how to build the "bomb" at home.

Full writeup: https://mapleleaf.ro/keep-talking-and-everybody-explodes-writeup-omnictf-2026-finals/

## What you need

| Qty | Part | Link |
|-----|------|------|
| 1x | 4x4 matrix keypad (female pin connector) | [Optimus Digital](https://www.optimusdigital.ro/en/touch-sensors/470-4x4-matrix-keyboard-with-female-pin-connector.html) |
| 1x | Passive buzzer (3.3V / 3V) | [Optimus Digital](https://www.optimusdigital.ro/ro/audio-buzzere/12247-buzzer-pasiv-de-33v-sau-3v.html?search_query=buzzer) |
| 1x | 1602 LCD with I2C interface (yellow-green backlight) | [Optimus Digital](https://www.optimusdigital.ro/en/lcds/62-1602-lcd-with-i2c-interface-and-yellow-green-backlight.html) |
| 2x | 6x6x6 push button | [Optimus Digital](https://www.optimusdigital.ro/ro/butoane-i-comutatoare/1119-buton-6x6x6.html) |
| 1x | Arduino UNO R3 | |
| - | Some male-male wires and some female-male wires | |
| 1x | The box (STL files in this repo) | |

## Wiring

| Arduino UNO | addon |
|---|---|
| `5V` | LCD1602 `VCC` |
| `3.3V` | Buzzer `+` |
| `GND` | Common ground between: LCD GND + both buttons + the ground for the 3 wires |
| `A0` | Buton random message |
| `A1` | Buton `+1 second` |
| `A2` | Fir RED - **correct wire** |
| `A3` | Fir BLUE - wrong wire |
| `A4` | LCD `SDA` |
| `A5` | LCD `SCL` |
| `D2` | Keypad `R1` / pin 5 |
| `D3` | Keypad `R2` / pin 6 |
| `D4` | Keypad `R3` / pin 7 |
| `D5` | Keypad `R4` / pin 8 |
| `D6` | Keypad `C1` / pin 4 |
| `D7` | Keypad `C2` / pin 3 |
| `D8` | Keypad `C3` / pin 2 |
| `D9` | Keypad `C4` / pin 1 |
| `D10` | Buzzer `-` trough a `330Ω` resistor |
| `D11` | Fir YELLOW - Wrong wire |

## Flashing the firmware

1. Flash the first firmware to the Arduino.
2. Reboot the Arduino (unplug it and plug it back in).
3. Flash the second firmware the same way.

And now you have a working "bomb".
