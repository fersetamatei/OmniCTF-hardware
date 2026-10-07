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

## Flashing the firmware

1. Flash the first firmware to the Arduino.
2. Reboot the Arduino (unplug it and plug it back in).
3. Flash the second firmware the same way.

And now you have a working "bomb".
