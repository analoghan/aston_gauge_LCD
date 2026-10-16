# Ultimate Gauge Board - Aston Martin Edition

An automotive gauge display built on the ESP32-S3 with the LVGL graphics library. It shows live data from a MoTeC M1 ECU over CAN, with a boot animation, five data screens, status icons, ECU warnings, peak recall, and an odometer with two trip meters.

## Features

- **Data screens**: five screens of two values each, cycled by button or CAN keypad, plus a cruise control screen that appears automatically
- **Status icons**: cruise control, traction control, launch control, 2-step, exhaust bypass, and peak recall
- **ECU warnings**: the display turns red and names the highest-priority active M1 warning
- **Peak recall**: shows the maximum value of each signal; hold to clear the peaks for the current screen
- **Odometer and two trip meters**: distance integrated from vehicle speed and saved to NVS flash
- **Color-coded values**: thresholds per value (for example, red for low oil pressure)
- **Missing-data indication**: values show `--` when their CAN data stops arriving
- **CAN robustness**: bus-off recovery, RX queue overflow handling, and watchdog logging

## Hardware

- ESP32-S3 with OPI PSRAM
- ST7701 960×960 RGB panel. The UI renders a 240×960 strip, offset on the panel in `LVGL_Driver.cpp`
- TCA9554PWR I2C GPIO expander (I2C on GPIO 15 SDA / 7 SCL) for the display reset and chip select, plus button inputs
- CAN transceiver on GPIO 5 (TX) / GPIO 4 (RX), 500 kbps

## Controls

### Physical buttons (TCA9554 inputs)

| Pin | Function | Action |
|-----|----------|--------|
| P5  | Screen change | Next screen |
| P6  | Trip reset | Reset the trip meter currently shown |
| P7  | Trip switch | Toggle between Trip 1 and Trip 2 |
| P8  | Reserved | Not used |

Inputs trigger on a falling edge (pulled to ground) and are polled every 50 ms. A failed I2C read is skipped rather than treated as a button press.

### CAN keypad (0x17C, byte 1)

| Bit | Function |
|-----|----------|
| 4 | Next screen (rising edge, so one screen per press) |
| 5 | Previous screen (rising edge) |
| 6 | Peak recall (held = 1). A press shows peaks while held plus 2 s. Holding for 3 s clears the peaks for the current screen |

## Screens

Every screen also shows the odometer and the selected trip meter. The last screen used is restored on boot.

| # | Left | Right |
|---|------|-------|
| 0 | Coolant temp (°F). Blue < 100, red ≥ 210 | Oil pressure (PSI). Red < 20 |
| 1 | AFR bank 1. Green 12–16, red outside | AFR bank 2. Green 12–16, red outside |
| 2 | Boost (PSI, MAP minus ambient pressure) | Speed (MPH) |
| 3 | Low-side fuel pressure (PSI). Red < 40 | Injector duty (%). Red ≥ 85 |
| 4 | Ethanol content (%) | Battery voltage (V) |
| 5 | Cruise engaged: On (green) / Off | Cruise set speed (MPH) |

**Cruise screen (5):**
- You can only select it while cruise is enabled (0x661 `Cruise_Enable` = Cruise Control). Otherwise screen cycling skips it.
- The gauge switches to it automatically when a set speed first appears (`Cruise_Speed_Display` goes from 0 to non-zero). You can still navigate away while cruise stays set.
- If cruise is disabled, or 0x661 stops arriving, while it is shown, the gauge returns to the last regular screen.
- It is never saved as the boot screen.

Boost uses the M1's `Ambient_Pressure` (0x64A) when the ECU sends a plausible value (60–110 kPa). Otherwise it falls back to a fixed 105 kPa (`AMBIENT_FALLBACK_KPA`).

If a value gets no CAN update for 500 ms, it shows `--` in white. During peak recall, `--` means that signal has never been received.

## ECU Warnings (0x64C)

When any warning bit is set, the screen background turns dark red and both gauges show the highest-priority warning. Warnings clear 500 ms after the last 0x64C message.

Priority, highest first:
1. Knock
2. Oil pressure
3. Fuel pressure
4. Coolant temperature
5. Oil temperature
6. Coolant pressure
7. Crankcase pressure
8. Engine speed

## Status Icons

| Icon | CAN ID | Signal | Behavior |
|------|--------|--------|----------|
| Cruise Control | 0x6A8 | B3 (non-zero) | Shown while active (500 ms timeout) |
| Traction Control | 0x64E | B3 bit 4 | Shown while active (500 ms timeout) |
| Launch Control | 0x64E | B3 bit 5 | Shown while active (500 ms timeout) |
| 2-Step | 0x650 | B7 bit 6 | Shown while active (500 ms timeout) |
| Exhaust Bypass | 0x6A8 | B5 (non-zero) | Shown while active (500 ms timeout) |
| Peak Recall | 0x17C | B1 bit 6 | Shown during peak recall. Swaps to the "clear" image after a 3 s hold |

All icons are shown for 2 s after the boot screen finishes.

## CAN Messages

The TWAI controller accepts all IDs. Filtering is done in software in `receive_can_task()`. Byte positions follow the M1 DBCs (`M1 General 0x640 0x650 0x670 Ver5.dbc` and `M1_Cruise_0x661_v1 (Match Master Channel List).dbc`, big-endian / Motorola order).

| ID | Signals used | Scaling |
|----|--------------|---------|
| 0x17C | Keypad: next / previous screen, peak recall | bit flags (see Controls) |
| 0x640 | Inlet_Manifold_Pressure (B2-3) | ×0.1 kPa absolute |
| 0x641 | Fuel_Pressure_Sensor (B4-5), Fuel_Injector_Primary_Duty_Cycle (B6) | ×0.1 kPa, ×1 % |
| 0x644 | Engine_Oil_Pressure (B6-7) | ×0.1 kPa |
| 0x649 | Coolant_Temperature (B0), ECU_Battery_Voltage (B5) | ×1 −40 °C, ×0.1 V |
| 0x64A | Ambient_Pressure (B6-7, signed) | ×0.1 kPa |
| 0x64C | Warning flags (B5 bits 0–7, B6 bit 7) | bit flags |
| 0x64E | Traction control (B3 bit 4), launch control (B3 bit 5) | bit flags |
| 0x650 | 2-step (B7 bit 6) | bit flag |
| 0x651 | Exhaust_Lambda_Bank_1 (B2), Exhaust_Lambda_Bank_2 (B3) | ×0.01 λ (×14.7 for AFR) |
| 0x659 | Vehicle_Speed (B4-5) | ×0.1 km/h |
| 0x661 | Cruise_Enable (B0 bits 7-6), Cruise_State_On (B0 bit 5), Cruise_Speed_Display (B1 bits 3-0 + B2) | enum, flag, ×0.1 km/h |
| 0x670 | Fuel_Composition (B5) | ×1 % |
| 0x6A8 | Cruise control (B3), exhaust bypass (B5) | non-zero = active |

## Odometer and Trip Meters

- Distance is integrated once a second from the average of 10 speed samples (0.1 mph resolution). It is stored in hundredths of a mile.
- Both trips accumulate all the time. A reset only clears the trip currently shown.
- Values are saved to NVS every 10 s when they change, and immediately after a trip reset. All flash writes go through one task.
- Defaults for a fresh board are set in `load_persistent_data()`.

## Architecture

| Task | Core | Role |
|------|------|------|
| `loop()` (Arduino) | 1 | LVGL (~60 Hz), display updates (10 Hz), buttons, icons, warnings |
| `RX_CAN` | 0 | Receives and decodes CAN into the shared `signals[]` array |
| `Init_CAN` | 1 | Starts the TWAI driver 5 s after boot, then exits |
| `Odometer` | 0 | Integrates distance from speed |
| `Save_NVS` | 0 | Writes the odometer, trips, and screen to flash |
| `Watchdog` | 0 | Logs a frozen main loop or missing CAN traffic |

All LVGL calls happen in the main loop. Other tasks hand off work through flags or shared state: `signals[]` is protected by `display_data_mutex`, and the odometer counters by `odometer_mux`.

## Building

### Requirements
- Arduino IDE (or `arduino-cli`) with the ESP32 Arduino core (built against 4.0.0-alpha1)
- Board: ESP32S3 Dev Module, PSRAM: OPI PSRAM
- LVGL 9.x (built against 9.6.0)
- `lv_conf.h` in your Arduino `libraries/` folder with:
  - `LV_COLOR_DEPTH 16`
  - `LV_FONT_MONTSERRAT_28` and `LV_FONT_MONTSERRAT_48` enabled (fallback fonts for glyphs the custom fonts don't include, such as space)
  - `#define LV_FONT_CUSTOM_DECLARE LV_FONT_DECLARE(aston_28) LV_FONT_DECLARE(aston_48)`

### Compile and upload
1. Open `Ultimate_Gauge_Board_AST_Animated_V2.ino`
2. Select the board and PSRAM settings above
3. Upload

## File Structure

```
.
├── Ultimate_Gauge_Board_AST_Animated_V2.ino  # App: CAN decoding, screen table, odometer, tasks
├── Screens.cpp/h                              # LVGL screen layout, styles, boot animation
├── CANBus_Driver.cpp/h                        # TWAI (CAN) setup
├── Display_ST7701.cpp/h                       # ST7701 panel init and backlight
├── LVGL_Driver.cpp/h                          # LVGL display, draw buffers, flush
├── I2C_Driver.cpp/h                           # I2C helpers
├── TCA9554PWR.cpp/h                           # GPIO expander driver
├── aston_28.c, aston_48.c                     # Custom fonts (compiled; must stay in the sketch root)
├── fonts/                                     # Font source (Optima Roman.ttf)
├── images/                                    # LVGL image assets (.h) and source images
├── M1 General 0x640 0x650 0x670 Ver5.dbc      # M1 CAN definitions
└── M1_Cruise_0x661_v1 (...).dbc               # M1 cruise control CAN definitions
```

## Customization

### Changing or adding a screen
All screens are defined in the `SCREENS` table near the top of the `.ino`. Each side lists its title, signal, scale function, number of decimals, and color rule. A side can also give a `text` function to show words instead of a number (see the cruise screen). A screen can give an `available` function to make it selectable only some of the time. The number of screens is taken from the table.

To show a new CAN value:
1. Add an entry to `enum SignalId`
2. Decode it in `receive_can_task()` with `update_signal()`
3. Reference it from a `SCREENS` entry

### Thresholds and colors
Edit the color lambdas in `SCREENS`, for example `(v > 0 && v < 20) ? COLOR_RED : COLOR_WHITE` for oil pressure.

### Fonts and styles
Edit `init_styles()` in `Screens.cpp`. Text is rotated 90° with `lv_style_set_transform_angle`. To regenerate a font, use the `lv_font_conv` options recorded in the header of each font file.

## License

MIT License - see LICENSE for details.
