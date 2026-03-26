# HoriGC_Trimmed.SFC — Reverse Engineering Notes

Decompilation of a Hori GameCube controller test cartridge for the SNES.

## What it is

A factory/QA test ROM distributed on a custom SNES cartridge by Hori. It tests every input on a GameCube controller connected to SNES controller port 1. The SNES does not natively support GC controllers — the cartridge bit-bangs the GC serial protocol in software via the `JOYSER0` register (`0x4016`).

## ROM details

| Field | Value |
|-------|-------|
| File | `HoriGC_Trimmed.SFC` |
| Size | 512 KB (LoROM) |
| Reset vector | `0x8000` |
| CPU | WDC 65C816, native mode |
| PPU mode | Mode 1 (4bpp BG1/2, 2bpp BG3, sprites) |
| SNES header | Non-standard / blank (test cart, no retail header) |

## How it works

### GC controller protocol

The GameCube controller uses a single-wire bidirectional serial protocol at ~200 kHz. The SNES doesn't have hardware support for this, so the cartridge bit-bangs it:

- `JOYSER0` (`0x4016`) bit 0 drives the data line low/high
- Reading `JOYSER0` bit 0 samples the controller's response
- NOP instructions and short delay loops provide the required bit timing
- A full read takes ~8 bytes × 8 bits per frame

The host sends a 3-byte poll command; the controller replies with 8 bytes:

| Byte | Contents |
|------|----------|
| 0 | Buttons: A, B, X, Y, Start |
| 1 | Buttons: L, R, Z, D-Up, D-Down, D-Right, D-Left |
| 2 | Main stick X (0x00–0xFF) |
| 3 | Main stick Y (0x00–0xFF) |
| 4 | C-stick X (0x00–0xFF) |
| 5 | C-stick Y (0x00–0xFF) |
| 6 | L trigger analog (0x00–0xFF) |
| 7 | R trigger analog (0x00–0xFF) |

Bits arrive LSB-first and inverted (active-low line), so the firmware reverses bit order and XORs each byte with `0xFF` after reading.

### Analog calibration

On startup the screen reads **PLEASE CONNECT CONTROLLER**. The user holds **A + Z** for 10 frames; the current raw stick values are saved as calibration centers. All subsequent axis readings are reported relative to those centers:

```
relative = raw - center + 0x80
```

This maps the resting position to `0x80`, full deflection to `0x00` or `0xFF`.

### Test sequence (state machine)

The firmware runs a 16-state machine, one state per frame:

| State | Description |
|-------|-------------|
| 0 | Load background tilemap, reset all variables |
| 1 | Wait for A+Z hold → save calibration centers |
| 2 | Sequential button press test (A, B, X, Y, Start, D-pad ×4, L, R, Z, stick ×4, C-stick ×4) |
| 3 | Main stick: reach all four cardinal zones |
| 4 | L/R analog triggers: reach high and low ends |
| 5 | Main stick: reach full range on both axes |
| 6 | C-stick: reach all four cardinal zones |
| 7 | (reuses axis range handler) |
| 8 | C-stick: reach full range on both axes |
| 9 | L/R triggers: squeeze fully past threshold |
| 10 | Z button: hold for 5 frames |
| 11 | C-stick Y: reach upper and lower extremes |
| 12 | C-stick X: reach left and right extremes |
| 13 | All tests passed — show result screen |
| 14 | Show main display layer |
| 15 | Show controller outline overlay (BG2 blend) |

A timeout resets the test to state 0 if **A+Z** is not held for 30 consecutive frames after calibration.

### Display

- **BG1** (4bpp): Main UI — button labels, axis readouts
- **BG2** (4bpp): GC controller outline image, composited as a color-math overlay in the final state
- **BG3** (2bpp, high priority): Text / UI chrome
- **Sprites**: Joystick position indicators (dot moves with stick deflection)
- **Hex readout**: All 6 analog values displayed as two-digit hex, updated every frame
- **Button highlights**: Each button's tilemap region switches palette when pressed

### Memory layout (after init)

| VRAM range | Contents |
|------------|----------|
| `0x0000–0x3FFF` | 4bpp tile graphics — controller buttons |
| `0x4000–0x5FFF` | 4bpp tile graphics — controller body |
| `0x6000–0x7FFF` | 2bpp tile graphics — UI / text |
| `0x7000–0x77FF` | BG1 tilemap (main display, 32×28) |
| `0x7400–0x7BFF` | BG3 tilemap (UI text) |
| `0x7800–0x7FFF` | BG2 tilemap (background) |
| `0x7C00–0x7FFF` | BG2 overlay tilemap (controller outline) |

| WRAM range | Contents |
|------------|----------|
| `0x0000–0x00C8` | Direct page variables (state, axis values, DMA scratch) |
| `0x00C9–0x02E8` | OAM shadow buffer (128 sprites × 4 bytes + 32 size bytes) |
| `0x02E9–0x12E8` | BG1 tilemap shadow (DMA'd to VRAM each frame) |

## Key functions

| Function | ROM address | Description |
|----------|-------------|-------------|
| `Reset` | `0x8000` | CPU init, stack/DP setup, jump to `Main_Init` |
| `Main_Init` | `0x837C` | Full hardware init, DMA all assets, start loop |
| `Main_Loop` | `0x84B4` | 60 Hz main loop with state dispatch |
| `Read_GC_Controller` | `0x95CB` | Bit-bang GC poll, decode 8 bytes, calibrate |
| `SPC700_Upload` | `0x8319` | Standard SNES APU upload handshake |
| `Init_PPU_Registers` | `0x8172` | Zero PPU/DMA registers, clear WRAM |
| `Init_OAM_Buffer` | `0x8114` | Park all sprites off-screen at X=232 |
| `DMA_OAM_Upload` | `0x8136` | DMA OAM shadow → PPU OAM |
| `DMA_VRAM_Write2` | `0x80B6` | DMA ROM → VRAM (two-register mode) |
| `DMA_WRAM_Write2` | `0x80E5` | DMA ROM → WRAM via WMDATA |
| `DMA_CGRAM_Write` | `0x808C` | DMA ROM → CGRAM (palette) |
| `Update_Button_Display` | `0x924A` | Set tilemap palette per button state |
| `Set_Tilemap_Palette` | `0x93D0` | Patch palette bits in a tilemap rect |
| `Copy_Tilemap_Rect` | `0x82A4` | Blit tile data into tilemap rect |
| `Clear_Tilemap_Rect` | `0x82E1` | Fill tilemap rect with tile 0 |
| `Display_Analog_Values` | `0x9408` | Write all 6 analog values as hex digits |
| `Write_Hex_Byte_To_Tilemap` | `0x98FA` | Write one byte as two hex digit tiles |
| `WaitVBlank` | `0x8163` | Spin on RDNMI bit 7 |
| `Timeout_Counter` | `0x8623` | Auto-reset on 30-frame no-input timeout |
| `Clear_Axis_Vars` | `0x923A` | Zero 48-byte test result array |
| `Draw_Connect_Screen` | `0x9810` | Draw "PLEASE CONNECT CONTROLLER" |

## Building a converter to use with this ROM

Because this ROM handles the GC protocol entirely in software on the cartridge CPU, **no microcontroller is needed** in the converter. The ROM already speaks GC natively over `JOYSER0`. The converter is a passive wiring adapter with one level shifter.

### The problem: voltage mismatch

| Signal | SNES (5V logic) | GC controller (3.3V logic) |
|--------|-----------------|---------------------------|
| Data line | 5V high | 3.3V high |
| Power | +5V | 3.3V |

Connecting a 3.3V GC controller data line directly to a 5V SNES output would overvoltage and eventually damage the controller. The data line needs a bidirectional level shifter.

### Parts

- 1× **TXB0101** (or similar single-channel bidirectional level shifter) — e.g. TI TXB0101DCKR, or the SparkFun/Adafruit breakout boards
- 1× **1kΩ resistor** (pull-up on GC data line to 3.3V)
- 1× **3.3V LDO regulator** — e.g. AMS1117-3.3 or LD1117-3.3 (TO-92 or SOT-223)
- 1× **100nF decoupling capacitor** (across the LDO output)
- 1× **SNES controller plug** (male, to plug into the console)
- 1× **GC controller socket** (female, to accept the controller)
- Short lengths of hookup wire

All parts are available from the usual electronics suppliers (Mouser, Digikey, LCSC) for under $5 total.

### Wiring

```
SNES port                              GC socket
(male plug)                            (female)

Pin 1  +5V ──────────────────────── Pin 1  +5V (rumble motor, optional)
         │
         └── LDO IN
             LDO OUT ──┬── 100nF ── GND
                       ├─────────── Pin 6  +3.3V (controller logic supply)
                       └── 1kΩ ──── Pin 2  Data (pull-up to 3.3V)

Pin 3  Latch ─┐
              ├─── TXB0101 A side
Pin 4  Data  ─┘    TXB0101 B side ── Pin 2  Data (bidirectional)
               TXB0101 VCCA ──────── +5V
               TXB0101 VCCB ──────── +3.3V (from LDO)
               TXB0101 OE   ──────── +5V (always enabled)

Pin 7  GND ──────────────────────── Pin 3  GND
                                    Pin 4  GND
                                    Pin 7  GND (shield)
```

SNES pin 2 (Clock) is left unconnected — it is only used by the standard hardware shift-register protocol which this ROM bypasses entirely.

### Notes

- **Only SNES controller port 1** works. The ROM reads `JOYSER0` (`0x4016`) which is wired to port 1 only.
- The TXB0101 is truly bidirectional with no direction-control pin, which suits the GC open-drain single-wire protocol where both host and controller pull the line low at different times. Avoid unidirectional shifters (e.g. 74HCT245) which would block the controller's reply.
- The **1kΩ pull-up** to 3.3V on GC pin 2 is required. The GC protocol is open-drain — neither side ever drives the line high, so without a pull-up the line can never return to idle-high between bits.
- The GC controller **logic supply is on Pin 6**, not Pin 5. Pin 5 is unused. Connecting Pin 6 to GND would short the supply rail and destroy the controller.
- Rumble (Pin 1) is optional — this ROM does not activate it. Leave Pin 1 unconnected if you don't need rumble.

### Completed converter

Plug the SNES end into controller port 1, plug the GC controller into the socket, and power on the SNES with the cartridge inserted. The ROM will immediately begin bit-banging the GC protocol and the test sequence will start.

## Tools used

- **radare2 6.1.2** — disassembly and function analysis (`-a snes -m 0x8000`)
