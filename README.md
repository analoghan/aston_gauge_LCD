# Ultimate Gauge Board - Aston Martin Edition

A high-performance automotive gauge display system built on ESP32 with LVGL graphics library. Features real-time CAN bus data visualization with smooth animations and multi-screen support.

## Features

- **Multi-Screen Display**: 4 configurable screens showing different vehicle parameters
- **CAN Bus Integration**: Real-time data from vehicle CAN network (500kbps)
- **Thread-Safe Architecture**: FreeRTOS tasks for reliable concurrent operation
- **Smooth Animations**: 60Hz refresh rate with optimized LVGL rendering
- **Robust Error Handling**: Automatic CAN bus recovery and watchdog monitoring
- **Color-Coded Warnings**: Dynamic color changes based on sensor thresholds

## Hardware Requirements

- ESP32 microcontroller
- ST7701 display (1504x480 resolution)
- TCA9554PWR I2C GPIO expander
- CAN transceiver (connected to GPIO 4/5)
- I2C peripherals

## Screens

### Screen 1: Engine Vitals
- Coolant Temperature (°F)
- Oil Pressure (PSI)

### Screen 2: Air/Fuel Ratio
- Left Bank AFR
- Right Bank AFR

### Screen 3: Pressure Monitoring
- MAP Pressure
- Coolant Pressure

### Screen 4: Fuel System
- Low Side Fuel Pressure
- Direct Injection Fuel Pressure

## CAN Message IDs

| ID    | Description | Data Bytes |
|-------|-------------|------------|
| 0x551 | Engine temps/pressure | B0: Coolant temp, B1: Oil pressure |
| 0x552 | Screen change trigger | B0: 1 = cycle screens |
| 0x553 | AFR data | B0: Left AFR, B1: Right AFR |
| 0x554 | Pressure data | B0: MAP, B1: Coolant pressure |
| 0x555 | Fuel pressure | B0: Low side, B1: High side |

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
    ├── MotecLogo.h
    └── jake.h
```

## Performance

- **Loop frequency**: ~60Hz (16ms)
- **Display update rate**: 10Hz (100ms)
- **CAN message processing**: Up to 1000+ msgs/sec
- **Memory**: ~4KB stack per task

## Safety Features

- Automatic CAN bus error recovery
- Queue overflow detection and handling
- Watchdog monitoring for system freezes
- Debounced screen changes (500ms minimum)
- Thread-safe data access with mutexes

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
