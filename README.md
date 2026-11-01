# LoRa serial modem for Ebyte E32-868T30 / E32-868T20 / E32-433T20

*[Version française](README.fr.md)*

Replacement firmware for Ebyte E32 modules (STM8L151 + SX1276/SX1278): a transparent
UART ↔ LoRa modem with pairing (bind), acknowledgements and retransmissions.

## Firmware images

| Module | Image | Band | Output power |
|---|---|---|---|
| E32-868T30 | `modem.ihx` | 869.525 MHz | SX1276 at 14 dBm into the external PA, ~27 dBm (≈ 500 mW, not measured) |
| E32-868T20 | `modem_t20.ihx` | 869.525 MHz | SX1276 alone at 20 dBm (100 mW), chip maximum |
| E32-433T20 | `modem_433t20.ihx` | 434.000 MHz | SX1278 alone at 20 dBm (100 mW) |

All three were tested in pairs: 100 % delivery in every stress test, including a 2 KB burst.

## Features

- UART 9600 baud 8N1 (PA2 = TX, PA3 = RX)
- LoRa SF7, 250 kHz, CR 4/5
  - 868: 869.525 MHz, European sub-band allowing 10 % duty cycle / 500 mW
  - 433: 434.000 MHz, away from the crowded 433.92 MHz
- Packets up to 120 bytes, acknowledgement + up to 8 retransmissions, duplicate suppression
- Listen-before-talk with random backoff
- UART input buffer 1 KB, output 512 B; AUX (PD0) goes low when the input buffer is nearly full
- Measured throughput: ~600 bytes/s

Boot banner (after 5 s):

```
Yolo TMTC lora 868 RX/TX 500mW 9600 - SX127x v0x12 OK - NET 680D
```

## LED (M0 = PC5) and button (M1 = PC6)

| State | LED |
|---|---|
| Normal boot | 4 slow blinks |
| Packet sent / received | short flash |
| Bind mode | fast blinking |
| Bind successful | solid until reboot |

**Button bind:** power up both modules with M1 held to GND. They exchange their IDs, store a
common network number in EEPROM, then the LED stays solid. Reboot both modules.

**Software bind:** send `+++BIND` on its own (no line ending) to the UART of both modules,
less than 60 s apart. Each replies `BIND OK - NET xxxx` and switches to the new network
immediately, no reboot needed. With no answer within 60 s it prints `BIND : abandon` and
keeps its previous network.

## Power supply

Transmit current peaks (≈ 120 mA for the T20, ≈ 500 mA or more for the T30) exceed what the
3.3 V output of many USB-serial adapters can deliver (the CP2102 in particular): the module
resets and the USB port drops. Power the module from 5 V (the adapter's 5V pin for the T20,
a supply of at least 1 A for the T30), with a common ground and a 100–470 µF capacitor close
to VCC.

## Building

[SDCC](https://sdcc.sourceforge.net/) 4.5:

```
sdcc -mstm8 --opt-code-size --out-fmt-ihx -o modem.ihx main.c                                 # E32-868T30
sdcc -mstm8 --opt-code-size --out-fmt-ihx -DMODEL_T20 -o modem_t20.ihx main.c                 # E32-868T20
sdcc -mstm8 --opt-code-size --out-fmt-ihx -DMODEL_T20 -DBAND_433 -o modem_433t20.ihx main.c   # E32-433T20
```

## Flashing (ST-Link V2, SWIM)

Wiring: SWIM and RST on the two small pads of the module, plus GND and 3.3 V.

```
openocd -f interface/stlink.cfg -c "adapter speed 363" -f target/stm8l.cfg \
  -c "reset_config srst_only connect_assert_srst" \
  -c "init; halt; load_image modem.ihx; reset run; shutdown"
```

Flashing succeeded only if the output ends with `shutdown command invoked`. On a module still
running its factory firmware, the first attempt often stops after `target halted`: just run
it again.

Both modules of a pair must run the same firmware version. A T20 and a T30 on the same band
can talk to each other (only the power differs). A 433 module cannot talk to an 868 module.

## Files

- `main.c` — modem firmware
- `modem.ihx` — compiled image for the 868T30
- `modem_t20.ihx` — compiled image for the 868T20
- `modem_433t20.ihx` — compiled image for the 433T20
- `uartprobe.c` — test firmware used to find the UART pins
- `tools/stress.ps1` — stress test between two COM ports (PowerShell)

## Limitations

- The firmware does not enforce the 10 % duty-cycle limit.
- The 433T20 image transmits at 100 mW, above the 10 mW European limit for the 433 MHz band.
- Two simultaneous bursts in both directions exceed the radio throughput: use flow control
  (AUX → CTS) or throttle the data rate on the host side.
- The pinout was derived from a T30's factory firmware; AUX on PD0 is not verified. The
  868T20 and 433T20 work with the same pinout.
