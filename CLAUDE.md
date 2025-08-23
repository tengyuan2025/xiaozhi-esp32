# CLAUDE.md

This file provides guidance to Claude Code (claude.ai/code) when working with code in this repository.

## Project Overview

Xiaozhi ESP32 is a multilingual AI chatbot device supporting 70+ ESP32 development boards. It implements real-time voice interaction through streaming ASR, LLM, and TTS with MCP (Model Context Protocol) integration for device control.

## Build Commands

```bash
# Standard build workflow
idf.py build                     # Build firmware
idf.py flash                     # Flash to device
idf.py monitor                   # Monitor serial output
idf.py flash monitor             # Flash and monitor in one step
idf.py clean                     # Clean build files

# Board-specific build
python scripts/release.py [board-name]  # Build for specific board and create release zip

# Utility commands
idf.py merge-bin                 # Merge binary files
idf.py flash_id                  # Check device information
idf.py erase_flash               # Erase device flash

# Development scripts
python scripts/gen_lang.py --language zh-CN --output main/assets/lang_config.h  # Generate language config
python scripts/audio_debug_server.py                                            # Run audio debug server
./scripts/mp3_to_ogg.sh                                                        # Convert MP3 to OGG for assets
```

## Architecture Overview

### Component Hierarchy
```
Application Layer
├── Application (main/application.cc) - Core application logic, state management
├── MCP Server (main/mcp_server.cc) - Device control protocol implementation
└── Board Manager - Hardware abstraction layer

Hardware Abstraction
├── Board Base Classes
│   ├── WifiBoard - Wi-Fi enabled boards
│   ├── Ml307Board - 4G cellular boards  
│   └── DualNetworkBoard - Wi-Fi + 4G boards
└── 70+ Board Implementations (main/boards/*/board.cc)

Communication Layer
├── Protocols
│   ├── WebSocket (main/protocols/websocket_protocol.cc)
│   └── MQTT+UDP (main/protocols/mqtt_protocol.cc)
└── Audio Codec: OPUS compression

Audio Pipeline
├── Wake Word (ESP-SR) → VAD → Audio Capture (I2S)
├── Audio Processing (AFE optional)
└── Codec Support (ES8311, ES8374, ES8388, ES7210, etc.)

Display System  
├── LVGL 9.2.2 GUI Framework
├── Display Drivers (LCD/OLED)
└── Multi-language Font Rendering
```

### Key Design Patterns

- **Factory Pattern**: Board creation via `DECLARE_BOARD` macro registration
- **Observer Pattern**: Event system for audio, network, and display events
- **State Machine**: Application states (INIT, CONNECTING, CONNECTED, RECORDING, etc.)
- **Plugin Architecture**: MCP tools dynamically registered per board

## Adding New Board Support

1. Create board directory: `main/boards/[board-name]/`
2. Implement board class in `board.cc`:
   ```cpp
   class YourBoard : public WifiBoard {
       void Initialize() override;
       void RegisterBoardDependentCommonTools() override;
   };
   DECLARE_BOARD(YourBoard);
   ```
3. Define hardware pins in `config.h`
4. Create build configuration `config.json`:
   ```json
   {
     "board_type": "your-board",
     "chip": "ESP32-S3",
     "psram": "8MB",
     "flash": "16MB"
   }
   ```
5. Update `main/CMakeLists.txt` to include board files

## Common Development Tasks

### Testing Audio Pipeline
```bash
# Start audio debug server to test audio transmission
python scripts/audio_debug_server.py

# Monitor device logs for audio events
idf.py monitor | grep -E "audio|codec|i2s"
```

### Debugging Connection Issues
- Check WebSocket/MQTT connection in monitor: `idf.py monitor | grep -E "ws|mqtt|connect"`
- Verify Wi-Fi credentials in NVS storage
- Test server connectivity with audio debug server

### Memory Optimization
- Current version: 1.8.7 (CMakeLists.txt)
- Monitor heap usage: Look for "Free heap" in serial output
- Partition layouts in `partitions/v1/` for different flash sizes

## Configuration Systems

### Build Configuration
- `sdkconfig.defaults` - ESP-IDF SDK configuration
- `main/Kconfig.projbuild` - Project-specific options (languages, OTA URL)
- Board-specific `config.json` - Hardware capabilities

### Runtime Configuration
- NVS storage for Wi-Fi credentials and server settings
- Language selection from 20+ supported languages
- Audio parameters (sample rates, codecs) per board

## MCP Tool Development

Tools are registered in `McpServer::AddCommonTools()` and board-specific initialization:
```cpp
// Common tools (all boards)
- volume_get/set - Audio volume control
- screen_brightness_get/set - Display brightness
- battery_level_get - Battery status
- gpio_get/set - GPIO control

// Board-specific tools
- motor_control - Motor operations
- led_control - LED patterns
- sensor_read - Sensor data
```

## Important Files

- `main/application.cc` - Core application state machine
- `main/board.cc` - Base board class and common functionality
- `main/mcp_server.cc` - MCP protocol implementation
- `main/display/display.cc` - Display abstraction
- `main/audio/audio_codec.cc` - Audio codec interface
- `scripts/release.py` - Build automation for all boards

## Dependencies

- ESP-IDF 5.4+ (required)
- LVGL 9.2.2 (GUI framework)
- ESP-SR (wake word detection)
- ESP32 chip variants: ESP32, ESP32-S3, ESP32-C3, ESP32-C6, ESP32-P4

## Code Style

- Google C++ coding style
- MIT License
- No hardcoded secrets or API keys
- Prefer editing existing files over creating new ones