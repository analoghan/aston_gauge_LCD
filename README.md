# Ultimate Gauge Board - Aston Martin Edition

A high-performance automotive gauge display system built on ESP32 with LVGL graphics library. Features real-time CAN bus data visualization with smooth animations and multi-screen support.

## Features

- **Multi-Screen Display**: 5 configurable screens showing different vehicle parameters
- **Status Icons**: Visual indicators for cruise control, traction control, launch control, 2-step, exhaust bypass, and peak recall
- **Dual Trip Meters**: Two independent trip meters with separate tracking and reset
- **Physical Button Controls**: TCA9554 GPIO inputs for screen change, trip reset, and trip switch
- **CAN Bus Integration**: Real-time data from vehicle CAN network (500kbps)
- **Max Value Recall**: Track and display maximum values for all sensors (per-screen reset)
- **Persistent Storage**: Odometer and trip values saved to NVS flash
- **Thread-Safe Architecture**: FreeRTOS tasks for reliable concurrent operation
- **Smooth Animations**: 60Hz refresh rate with optimized LVGL rendering
- **Robust Error Handling**: Automatic CAN bus recovery and watchdog monitoring
- **Color-Coded Warnings**: Dynamic color changes based on sensor thresholds
- **Custom Fonts**: Aston Martin branded fonts for authentic styling

## Hardware Requirements

- ESP32-S3 microcontroller
- ST7701 display (480x1504 resolution, rotated 90°)
- TCA9554PWR I2C GPIO expander (for button inputs)
- CAN transceiver (connected to GPIO 4/5)
- I2C peripherals

## Physical Controls (TCA9554 GPIO)

| Pin | Function | Action |
|-----|----------|--------|
| P5  | Screen Change | Cycle through 5 display modes |
| P6  | Trip Reset | Reset currently displayed trip meter |
| P7  | Trip Switch | Toggle between Trip 1 and Trip 2 |
| P8  | Reserved | Available for future use |

All inputs use internal pull-ups and trigger on falling edge (pull to ground). Screen change, trip reset, and trip switch are GPIO-only (no CAN equivalent).

## Screens

All screens display odometer and trip meter values at the bottom.

### Screen 0: Engine Vitals
- Coolant Temperature (°F) - Blue when cold (<100°F), Red when hot (≥210°F)
- Oil Pressure (PSI) - Red when low (<20 PSI)

### Screen 1: Air/Fuel Ratio
- Left Bank AFR - Green (12-16), Red (out of range)
- Right Bank AFR - Green (12-16), Red (out of range)

### Screen 2: Pressure & Speed
- MAP Pressure (PSI)
- Speed (MPH)

### Screen 3: Fuel System
- Low Side Fuel Pressure (PSI)
- Direct Injection Fuel Pressure (PSI)

### Screen 4: Fuel & Electrical
- Ethanol Percentage (%)
- Battery Voltage (V)

## Trip Meters

- **Dual Independent Trip Meters**: Track two separate trips simultaneously
- **Trip 1**: Default on startup, ideal for tank-to-tank tracking
- **Trip 2**: Secondary trip for journey-specific tracking
- **Both track continuously**: Distance accumulates on both trips regardless of which is displayed
- **Individual Reset**: Only the currently displayed trip is reset
- **Persistent Storage**: Both trip values saved to NVS flash every 10 seconds
- **Switch Control**: Toggle between Trip 1 and Trip 2 via P7 button

## Status Icons

Six status icons display on the right side of the screen, driven directly from M1 ECU native CAN messages:

| Icon | CAN ID | Byte | Bit | Display Behavior |
|------|--------|------|-----|------------------|
| Cruise Control | 0x6A8 | buf[3] | full byte | Shows for 2s on startup, or while active (500ms timeout) |
| Traction Control | 0x64E | buf[3] | bit 4 | Shows for 2s on startup, or while active (500ms timeout) |
| Launch Control | 0x64E | buf[3] | bit 5 | Shows for 2s on startup, or while active (500ms timeout) |
| 2-Step | 0x650 | buf[7] | bit 6 | Shows for 2s on startup, or while active (500ms timeout) |
| Exhaust Bypass | 0x6A8 | buf[5] | full byte | Shows for 2s on startup, or while active (500ms timeout) |
| Peak Recall | 0x178 | buf[1] | bit 7 | Short press: shows max values while held + 2s. Hold 3s: clears max for current screen |
| Clear Peak Recall | 0x178 | buf[1] | bit 7 (3s hold) | ClearPeakRecall icon shown when max values are cleared |

All status icons hide 500ms after the last active CAN message. The Peak Recall icon dynamically swaps between two images based on context (recall vs clear).

## CAN Message IDs

All messages are in the 0x600-0x6FF range. A hardware acceptance filter on the ESP32 TWAI controller rejects everything outside this range before it reaches software, significantly reducing CPU load on a busy automotive CAN bus.

### M1 ECU Input Messages (read by gauge)

| ID (hex) | ID (dec) | Signals Used | Scaling |
|----------|----------|--------------|---------|
| 0x640 | 1600 | Inlet_Manifold_Pressure (B0-1) | ×0.1 kPa → PSI |
| 0x641 | 1601 | Fuel_Pressure_Sensor (B4-5) | ×0.1 kPa → PSI |
| 0x644 | 1604 | Engine_Oil_Pressure (B4-5) | ×0.1 kPa → PSI |
| 0x649 | 1609 | Coolant_Temperature (B0), ECU_Battery_Voltage (B5) | ×1 -40°C→°F, ×0.1 V |
| 0x64E | 1614 | TCS (B3 bit 4), Launch_Control (B3 bit 5) | bit flags |
| 0x650 | 1616 | 2-Step (B7 bit 6) | bit flag |
| 0x651 | 1617 | Exhaust_Lambda_Bank_1 (B2), Exhaust_Lambda_Bank_2 (B3) | ×0.01 LA → ×14.7 AFR |
| 0x653 | 1619 | ~~Fuel_Pressure_Direct_B1~~ (removed - DI ECU only) | — |
| 0x659 | 1625 | Vehicle_Speed (B4-5) | ×0.1 km/h → MPH |
| 0x670 | 1648 | Fuel_Composition (B5) | ×1 % |
| 0x6A8 | 1704 | Cruise_Control (B3), Exhaust_Bypass (B5) | full byte flags |

### Gauge Control Messages

| ID    | Description | Data Bytes |
|-------|-------------|------------|
| 0x178 | Peak recall button | B1 bit 7: 1 = held. Short press = show max, hold 3s = clear max for current screen |

## Architecture

### Task Structure
- **Core 0**: Main loop with LVGL timer handler and display updates
- **Core 1**: CAN receive task and initialization tasks
- **Watchdog**: System health monitoring

### Key Optimizations
- Value change detection to skip redundant LVGL updates
- 10Hz rate limiting for display updates
- Thread-safe mutex-protected data structures
- Reusable LVGL style objects
- Static text for constant strings
- Disabled scrolling on containers

## Building

### Prerequisites
- Arduino IDE or PlatformIO
- ESP32 board support
- Required libraries:
  - LVGL (v8.x)
  - ESP32 TWAI driver

### Configuration
1. Update CAN bus speed in `CANBus_Driver.h` if needed (default: 500kbps)
2. Adjust display settings in `Display_ST7701.h`
3. Configure I2C pins in `I2C_Driver.h`

### Compile and Upload
```bash
# Using Arduino IDE
1. Open Ultimate_Gauge_Board_AST_Animated_V2.ino
2. Select ESP32 board
3. Upload

# Using PlatformIO
pio run -t upload
```

## File Structure

```
.
├── Ultimate_Gauge_Board_AST_Animated_V2.ino  # Main application
├── CANBus_Driver.cpp/h                        # CAN bus interface
├── Display_ST7701.cpp/h                       # Display driver
├── I2C_Driver.cpp/h                           # I2C communication
├── LVGL_Driver.cpp/h                          # LVGL initialization
├── TCA9554PWR.cpp/h                           # GPIO expander
├── Screens.cpp/h                              # UI screen definitions
└── images/                                    # Image assets
    ├── AstonLogo.h
    ├── CruiseControl.h
    ├── tcs.h
    ├── flag.h
    ├── TwoStep.h
    ├── ExhaustBypass.h
    ├── PeakRecall.h
    ├── ClearPeakRecall.h
    ├── MotecLogo.h
    └── jake.h
```

## Performance

- **Loop frequency**: ~60Hz (16ms)
- **Display update rate**: 10Hz (100ms)
- **CAN message processing**: Up to 1000+ msgs/sec
- **TCA9554 polling**: 50ms (20Hz)
- **Odometer calculation**: 1Hz with 10-sample averaging
- **Persistent storage**: Auto-save every 10 seconds if changed
- **Memory**: ~4KB stack per task

## Safety Features

- Automatic CAN bus error recovery
- Queue overflow detection and handling
- Watchdog monitoring for system freezes
- 500ms timeout resets values to 0 when no CAN data received
- Thread-safe data access with mutexes
- Debounced button inputs (50ms polling with edge detection)
- Persistent storage prevents data loss on power cycle
- Per-screen max value reset prevents accidental data loss

## Customization

### Adding New Screens
1. Add screen object to `Screens.h`
2. Create init function in `Screens.cpp`
3. Update `cycle_screens()` in main file
4. Add CAN message handler if needed

### Changing Colors/Fonts
Edit `init_styles()` in `Screens.cpp` to modify:
- Font sizes (`style_label_title`, `style_label_value`)
- Colors (currently white on black)
- Text rotation (currently 90°)

### Adjusting Thresholds
Modify warning thresholds in processing functions:
- `process_coolant_temp()`: Temperature ranges
- `process_oil_press()`: Pressure limits
- `process_left_afr()` / `process_right_afr()`: AFR ranges

## License

MIT License - See LICENSE file for details

## Contributing

Contributions welcome! Please:
1. Fork the repository
2. Create a feature branch
3. Make your changes
4. Submit a pull request

## Acknowledgments

- LVGL graphics library
- ESP32 Arduino framework
- Aston Martin for inspiration
