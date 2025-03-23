# ATmega8535 Gate Controller: H-Bridge Relay Motor Driver

Designed to replace an overly complex gate control board from the 90s that eventually died. This project uses an overly complex microcontroller instead, and outlines an opto-isolated motor control system based around an ATmega8535, a 4-relay H-Bridge, and the gate's existing limit switches. The microcontroller stores open/close state in EEPROM (to persist after power loss), and a change in direction is triggered via a single momentary input. There is optional logging output via UART.

The 250ms decoupling delay matches the timing of the signal from the Merlin remote receiver. I was originally using a capacitor, but expansion / contraction from cold nights during summer still seemed to cause false triggers.

I was intending to use an ATTiny85, but ended up using an ATmega8535 as I have a surplus of them and for the additional IO, as I intend to add a hall sensor in the future to handle the gate potentially hitting things such as vehicles or people. I used parts I had on hand, so some components are not ideal. For example, I had an excess of blue LEDs, and no logic level MOSFETs.

Please note that I am not a qualified electronics engineer. If you decide to follow this project, it's probably worth having a suitable Fire Extinguisher nearby. PRs are very welcome.

---

## Hardware Summary

| Component                 | Qty | Notes                                         |
| ------------------------- | --- | --------------------------------------------- |
| ATmega8535 (DIP)          | 1   | Main MCU                                      |
| 5V 1A PSU                 | 1   | Powers relays, optos, MCU. 2A would be safer. |
| 4 × 4N25 Optocouplers     | 4   | Isolate MCU from relay control                |
| 4 × BC337 Transistors     | 4   | Switch relay coils                            |
| 4 × SRD-05VDC-SL-C Relays | 4   | SPDT 5V relays for full H-bridge              |
| 3 × Blue LEDs             | 3   | Power, Gate Opening, Gate Closing indicators  |
| 3 × Resistors             | 3   | 1kΩ for power LED, 330Ω for others            |
| 4 × 1N4007 Diodes         | 4   | Flyback protection for relay coils            |
| 1 × Momentary Switch      | 1   | Gate toggle input                             |
| 0.1µF Cap + 10µF Cap      | 2   | Decoupling for VCC                            |

---

## Power and Decoupling

- 5V supply powers the MCU, relays, and LEDs.
- Add a 0.1µF ceramic capacitor and 10µF electrolytic capacitor across 5V and GND.
- ATmega pin 10 = VCC, pin 11 = GND.
- Add a 0.1µF cap between AREF (pin 32) and GND (pin 31).

---

## Pinout

### ATmega8535 (DIP-40)

| Pin | Label   | Function                        |
| --- | ------- | ------------------------------- |
| 1   | PB0     | Relay K1 Control (Motor A +19V) |
| 2   | PB1     | Relay K2 Control (Motor A GND)  |
| 3   | PB2     | Relay K3 Control (Motor B +19V) |
| 4   | PB3     | Relay K4 Control (Motor B GND)  |
| 5–8 | PB4–PB7 | Unused / Reserved for ISP       |
| 9   | RESET   | 10kΩ pull-up + ISP              |
| 10  | VCC     | +5V                             |
| 11  | GND     | GND                             |
| 14  | PD0     | UART RX (not used)              |
| 15  | PD1     | UART TX (optional)              |
| 16  | PD2     | Momentary Button Input          |
| 18  | PD4     | LED: Gate Opening               |
| 19  | PD5     | LED: Gate Closing               |
| 20  | PD6     | Spare I/O                       |
| 21  | PD7     | Spare I/O                       |
| 30  | AVCC    | +5V (tie to VCC)                |
| 31  | GND     | GND (analog)                    |
| 32  | AREF    | 0.1µF to GND                    |

---

## Relay H-Bridge Wiring

Each relay has:

- COM → Motor terminal
- NO → +19V
- NC → GND

### Motor Terminal A

- Relay K1 COM → Motor A
- K1 NO → +19V
- K1 NC → GND

- Relay K2 COM → Motor A
- K2 NO → GND
- K2 NC → +19V

### Motor Terminal B

- Relay K3 COM → Motor B
- K3 NO → +19V
- K3 NC → GND

- Relay K4 COM → Motor B
- K4 NO → GND
- K4 NC → +19V

---

## Relay Driver Circuit (x4)

Each relay is switched using:

- MCU Pin → 220Ω → 4N25 LED anode
- 4N25 cathode → GND
- 4N25 transistor: Collector → +5V, Emitter → BC337 base
- 10kΩ pull-down on BC337 base
- BC337: Collector → Relay coil –, Emitter → GND
- Relay coil + → +5V
- 1N4007 flyback diode across relay coil

---

## Button Input

- Use PD2 (Pin 16)
- Pull-up enabled in software
- Momentary switch connects PD2 to GND
- Detect button held for 250–500ms to toggle state

---

## EEPROM State

- Use EEPROM to store:
  - `0 = Gate Closed`
  - `1 = Gate Open`
- Write state on change
- Read on startup to restore state

---

## LED Indicators

| Function | Pin | Resistor | Behavior                  |
| -------- | --- | -------- | ------------------------- |
| Power    | VCC | 1kΩ      | Always ON                 |
| Opening  | PD4 | 330Ω     | ON when motor opens gate  |
| Closing  | PD5 | 330Ω     | ON when motor closes gate |

---

## Software Behavior

1. On startup, read EEPROM.
2. If button pressed ≥250ms:
   - If gate is closed: set to open (energize K1 & K4)
   - If gate is open: set to closed (energize K2 & K3)
3. Activate relays for 30 seconds
   - OR stop early if motor cutoff is detected (via endstops)
4. Save state to EEPROM

> Ensure dead time (e.g. 100ms) between direction change:

```c
// Pseudocode
turn_all_relays_off();
_delay_ms(100);
turn_relays_for_direction();
```

---

## ISP Programming Header (6-Pin)

| ISP Pin | Signal | ATmega Pin  |
| ------- | ------ | ----------- |
| 1       | MISO   | PB6 (Pin 7) |
| 2       | VCC    | 5V          |
| 3       | SCK    | PB7 (Pin 8) |
| 4       | MOSI   | PB5 (Pin 6) |
| 5       | RESET  | Pin 9       |
| 6       | GND    | GND         |

---

## Best Practices

- Use flyback diodes on every relay coil
- Use delay when switching direction
- Limit LED currents to <10mA
- Use internal pull-up for button
- Add decoupling caps near MCU
- Store last state in EEPROM
- Default state = Gate Closed
- Note that *my* gate motors have built in endstops that prevent them from opening or closing too far.
