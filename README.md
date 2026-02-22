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

All inputs use internal pull-ups and trigger on falling edge (pull to ground).

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
- **Switch Control**: Toggle between Trip 1 and Trip 2 via P7 button or CAN 0x559

## Status Icons

Six status icons display on the right side of the screen:

| Icon | Trigger | Display Behavior |
|------|---------|------------------|
| Cruise Control | CAN 0x560 B0=1 | Shows for 2s on startup, or while CAN active (500ms timeout) |
| Traction Control | CAN 0x560 B1=1 | Shows for 2s on startup, or while CAN active (500ms timeout) |
| Launch Control | CAN 0x560 B2=1 | Shows for 2s on startup, or while CAN active (500ms timeout) |
| 2-Step | CAN 0x560 B3=1 | Shows for 2s on startup, or while CAN active (500ms timeout) |
| Exhaust Bypass | CAN 0x560 B4=1 | Shows for 2s on startup, or while CAN active (500ms timeout) |
| Peak Recall | CAN 0x556 active | Shows PeakRecall icon for 2s on startup or during max recall |
| Clear Peak Recall | CAN 0x557 active | Shows ClearPeakRecall icon for 2s when clearing max values |

All icons (except Peak Recall variants) hide 500ms after last CAN message received. The Peak Recall icon dynamically swaps between two images based on context (recall vs clear).

## CAN Message IDs

| ID    | Description | Data Bytes |
|-------|-------------|------------|
| 0x550 | Trip meter reset | B0: 1 = reset currently displayed trip |
| 0x551 | Engine temps/pressure | B0: Coolant temp (+40°F offset), B1: Oil pressure |
| 0x552 | Fuel & electrical | B0: Ethanol %, B1: Battery volts |
| 0x553 | AFR data | B0: Left AFR, B1: Right AFR |
| 0x554 | Pressure & speed data | B0: MAP, B1: Speed (MPH) |
| 0x555 | Fuel pressure | B0: Low side, B1: High side |
| 0x556 | Max value recall | Display max values for 2 seconds |
| 0x557 | Reset max values | Clear max values for currently displayed screen only |
| 0x558 | Screen change | B0: 1 = cycle through screens |
| 0x559 | Trip switch | B0: 1 = toggle between Trip 1 and Trip 2 |
| 0x560 | Status icons | B0: Cruise, B1: TCS, B2: Launch, B3: 2-Step, B4: Exhaust Bypass |

**Note**: All CAN controls have physical button equivalents on TCA9554 P5-P7

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
