# CLAUDE.md

This file provides guidance to Claude Code (claude.ai/code) when working with code in this repository.

## Project Overview

This is the Xiaozhi ESP32 AI Chatbot project - a multilingual voice interaction device that supports 70+ ESP32 development boards. It's an MCP-based chatbot that integrates streaming ASR, LLM, and TTS for real-time voice interaction.

## Core Architecture

### Main Components
- **Audio System**: Handles voice input/output with codec support (ES8311, ES8374, ES8388, etc.)
- **Display System**: Supports various displays (OLED/LCD) with LVGL GUI framework
- **Communication**: Dual protocol support (WebSocket or MQTT+UDP)
- **Board Abstraction**: Unified interface supporting 70+ different ESP32 boards
- **MCP Server**: Device control through Model Context Protocol
- **Multi-language**: 20+ language support with localized audio assets

### Key Directories
- `main/` - Core application code
- `main/boards/` - Board-specific implementations (70+ supported boards)
- `main/audio/` - Audio codecs and processors
- `main/display/` - Display drivers and UI
- `main/protocols/` - Communication protocols (WebSocket/MQTT)
- `scripts/` - Build and utility scripts
- `partitions/` - Flash partition configurations

## Common Development Commands

### Building and Flashing
```bash
# Build the project
idf.py build

# Flash to device
idf.py flash

# Monitor serial output
idf.py monitor

# Build for specific board (using release script)
python scripts/release.py [board-name]

# Generate language configuration
python scripts/gen_lang.py --language zh-CN --output main/assets/lang_config.h
```

### Board Configuration
The project uses a sophisticated board selection system through CMake configurations. Each board has:
- Board-specific source files in `main/boards/[board-name]/`
- `config.h` for hardware pin mappings
- `config.json` for build configurations

### Audio Asset Management
```bash
# Convert MP3 to OGG for assets
./scripts/mp3_to_ogg.sh

# Run audio debug server
python scripts/audio_debug_server.py
```

## Architecture Details

### Board Inheritance Hierarchy
```
Board (base class)
├── WifiBoard - Wi-Fi enabled boards
├── Ml307Board - 4G cellular boards  
└── DualNetworkBoard - Boards supporting both Wi-Fi and 4G
```

### MCP Integration
The project implements MCP (Model Context Protocol) for device control:
- `McpServer` class handles tool registration and execution
- Common tools: volume control, screen brightness, battery status
- Board-specific tools can be added for custom hardware features

### Audio Pipeline
1. **Wake Word Detection** - ESP-SR based offline wake word detection
2. **Audio Capture** - I2S interface with various codec support
3. **Audio Processing** - Optional AFE (Audio Front-End) processing
4. **Communication** - OPUS codec for efficient transmission
5. **Playback** - Text-to-speech audio playback

### Display System
- LVGL 9.2.2 based GUI framework
- Support for SPI LCD panels (ST7789, ILI9341, etc.)
- OLED display support
- Multi-language font rendering
- Emoji and icon support

### Communication Protocols
- **WebSocket**: Direct connection for real-time communication
- **MQTT+UDP**: Hybrid protocol for reliable messaging with low-latency audio

## Development Guidelines

### Adding New Board Support
1. Create directory in `main/boards/[board-name]/`
2. Implement board class inheriting from `WifiBoard`/`Ml307Board`
3. Define hardware configuration in `config.h`
4. Add build configuration in `config.json`
5. Register board using `DECLARE_BOARD` macro

### Audio Codec Integration
- Implement `AudioCodec` interface
- Configure I2S parameters in board config
- Add codec-specific initialization in board class
- Support both input and output sample rate configuration

### Display Integration
- Implement `Display` interface (LCD/OLED variants available)
- Configure SPI/I2C parameters for display communication
- Set up LVGL with appropriate fonts and color depth
- Configure backlight control if available

### Localization
- Audio assets stored in `main/assets/locales/[language-code]/`
- Language configuration generated from JSON files
- Support for 20+ languages with proper font rendering

### MCP Tool Development
- Extend `McpServer::AddCommonTools()` for device-wide tools
- Add board-specific tools in board initialization
- Follow MCP specification for tool parameter definitions
- Implement proper error handling and return values

## Important Configuration Files

- `sdkconfig.defaults` - Default ESP-IDF configuration
- `CMakeLists.txt` - Main build configuration (current version: 1.8.7)
- `main/Kconfig.projbuild` - Project-specific configuration options
- `partitions/v1/` - Flash partition layouts for different flash sizes

## Key Dependencies

- ESP-IDF 5.4+ required
- LVGL 9.2.2 for GUI
- ESP-SR for wake word detection
- Various ESP32 series chips supported (ESP32, ESP32-S3, ESP32-C3, ESP32-C6, ESP32-P4)

This project follows Google C++ coding style and uses MIT license for open source development.