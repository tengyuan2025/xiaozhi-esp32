#!/bin/bash

# Xiaozhi ESP32 Build and Flash Script
# 小智AI聊天机器人编译和刷写脚本

set -e  # Exit on any error

# Colors for output
RED='\033[0;31m'
GREEN='\033[0;32m'
YELLOW='\033[1;33m'
BLUE='\033[0;34m'
NC='\033[0m' # No Color

# Script configuration
SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
PROJECT_DIR="$SCRIPT_DIR"

# Default values
BOARD_TYPE=""
PROTOCOL_TYPE=""
CLEAN_BUILD=false
MONITOR_AFTER_FLASH=false
FLASH_PORT=""
BAUDRATE="921600"

# Function to print colored output
print_info() {
    echo -e "${BLUE}[INFO]${NC} $1"
}

print_success() {
    echo -e "${GREEN}[SUCCESS]${NC} $1"
}

print_warning() {
    echo -e "${YELLOW}[WARNING]${NC} $1"
}

print_error() {
    echo -e "${RED}[ERROR]${NC} $1"
}

# Function to show help
show_help() {
    cat << EOF
Xiaozhi ESP32 Build and Flash Script

Usage: $0 [OPTIONS]

Options:
    -b, --board BOARD          Specify board type (e.g., bread-compact-wifi)
    -p, --protocol PROTOCOL    Specify protocol type (websocket|mqtt|doubao)
    -c, --clean               Clean build before compiling
    -m, --monitor             Start monitor after flashing
    -P, --port PORT           Specify flash port (e.g., /dev/ttyUSB0)
    -B, --baudrate RATE       Specify baudrate (default: 921600)
    -h, --help                Show this help message

Examples:
    $0 --board bread-compact-wifi --protocol doubao --clean --monitor
    $0 -b esp-box-3 -p websocket -c -m -P /dev/ttyUSB0
    $0 --clean --monitor  # Use default configurations

Supported Board Types:
    - bread-compact-wifi           面包板新版接线（WiFi）
    - bread-compact-wifi-lcd       面包板新版接线（WiFi）+ LCD
    - bread-compact-ml307          面包板新版接线（ML307 AT）
    - esp-box-3                    ESP BOX 3
    - lichuang-dev                 立创·实战派ESP32-S3开发板
    - m5stack-cores3               M5Stack CoreS3
    - ... and 70+ other boards

Supported Protocol Types:
    - websocket    Original WebSocket protocol
    - mqtt         MQTT + UDP protocol  
    - doubao       Doubao end-to-end voice protocol
EOF
}

# Function to check ESP-IDF environment
check_esp_idf() {
    print_info "Checking ESP-IDF environment..."
    
    if ! command -v idf.py &> /dev/null; then
        print_error "ESP-IDF not found. Please install and source ESP-IDF environment:"
        echo "  1. Install ESP-IDF: https://docs.espressif.com/projects/esp-idf/en/latest/esp32/get-started/"
        echo "  2. Source the environment: . \$HOME/esp/esp-idf/export.sh"
        exit 1
    fi
    
    # Check ESP-IDF version
    IDF_VERSION=$(idf.py --version 2>/dev/null | head -n1 || echo "Unknown")
    print_info "ESP-IDF Version: $IDF_VERSION"
    
    # Verify we're in the right directory
    if [[ ! -f "CMakeLists.txt" ]] || [[ ! -d "main" ]]; then
        print_error "Not in a valid ESP-IDF project directory"
        print_error "Please run this script from the project root directory"
        exit 1
    fi
    
    print_success "ESP-IDF environment OK"
}

# Function to detect connected ESP32 devices
detect_esp32_devices() {
    print_info "Detecting ESP32 devices..."
    
    # Common ESP32 USB device patterns
    local devices=()
    
    # Linux/macOS device detection
    if [[ -d "/dev" ]]; then
        # Look for common ESP32 USB-to-serial chips
        for pattern in "/dev/ttyUSB*" "/dev/ttyACM*" "/dev/cu.usbserial*" "/dev/cu.SLAB_USBtoUART*" "/dev/cu.usbmodem*"; do
            for device in $pattern; do
                if [[ -c "$device" ]] 2>/dev/null; then
                    devices+=("$device")
                fi
            done
        done
    fi
    
    if [[ ${#devices[@]} -eq 0 ]]; then
        print_warning "No ESP32 devices detected automatically"
        print_info "Please connect your ESP32 device and ensure drivers are installed"
        return 1
    elif [[ ${#devices[@]} -eq 1 ]]; then
        FLASH_PORT="${devices[0]}"
        print_success "Found ESP32 device: $FLASH_PORT"
    else
        print_info "Multiple devices found:"
        for i in "${!devices[@]}"; do
            echo "  $((i+1)). ${devices[i]}"
        done
        
        while true; do
            read -p "Select device number (1-${#devices[@]}): " choice
            if [[ "$choice" =~ ^[0-9]+$ ]] && [[ "$choice" -ge 1 ]] && [[ "$choice" -le ${#devices[@]} ]]; then
                FLASH_PORT="${devices[$((choice-1))]}"
                break
            else
                print_error "Invalid selection. Please enter a number between 1 and ${#devices[@]}."
            fi
        done
        print_success "Selected device: $FLASH_PORT"
    fi
}

# Function to configure project
configure_project() {
    print_info "Configuring project..."
    
    # Create a temporary sdkconfig if we have specific configurations
    local config_changed=false
    
    # Board type configuration
    if [[ -n "$BOARD_TYPE" ]]; then
        print_info "Configuring board type: $BOARD_TYPE"
        # Convert board name to config format
        local board_config="CONFIG_BOARD_TYPE_$(echo "$BOARD_TYPE" | tr '[:lower:]' '[:upper:]' | tr '-' '_')=y"
        
        # Add board configuration
        if ! grep -q "$board_config" sdkconfig.defaults 2>/dev/null; then
            print_info "Adding board configuration to build"
            # This would require more complex sdkconfig manipulation
            # For now, inform user to configure manually
            print_warning "Please configure board type manually using 'idf.py menuconfig'"
        fi
    fi
    
    # Protocol type configuration  
    if [[ -n "$PROTOCOL_TYPE" ]]; then
        print_info "Configuring protocol: $PROTOCOL_TYPE"
        case "$PROTOCOL_TYPE" in
            "websocket")
                print_info "Using WebSocket protocol (default)"
                ;;
            "mqtt")
                print_info "Using MQTT protocol"
                ;;
            "doubao")
                print_info "Using Doubao end-to-end voice protocol"
                print_warning "Please ensure Doubao protocol is selected in menuconfig"
                ;;
            *)
                print_warning "Unknown protocol type: $PROTOCOL_TYPE"
                ;;
        esac
    fi
    
    print_success "Project configuration completed"
}

# Function to build project
build_project() {
    print_info "Building project..."
    
    cd "$PROJECT_DIR"
    
    if [[ "$CLEAN_BUILD" == true ]]; then
        print_info "Cleaning previous build..."
        idf.py clean
    fi
    
    # Build the project
    print_info "Compiling firmware..."
    if idf.py build; then
        print_success "Build completed successfully"
        
        # Show build information
        if [[ -f "build/project_description.json" ]]; then
            local project_name=$(jq -r '.project_name // "xiaozhi"' build/project_description.json 2>/dev/null || echo "xiaozhi")
            local idf_target=$(jq -r '.target // "unknown"' build/project_description.json 2>/dev/null || echo "unknown")
            print_info "Project: $project_name"
            print_info "Target: $idf_target"
        fi
        
        # Show binary size
        if [[ -f "build/xiaozhi.bin" ]]; then
            local size=$(ls -lh build/xiaozhi.bin | awk '{print $5}')
            print_info "Firmware size: $size"
        fi
    else
        print_error "Build failed"
        exit 1
    fi
}

# Function to flash firmware
flash_firmware() {
    print_info "Flashing firmware to device..."
    
    if [[ -z "$FLASH_PORT" ]]; then
        if ! detect_esp32_devices; then
            print_error "No ESP32 device found for flashing"
            read -p "Enter device port manually (e.g., /dev/ttyUSB0): " FLASH_PORT
            if [[ -z "$FLASH_PORT" ]]; then
                print_error "No port specified, aborting flash"
                exit 1
            fi
        fi
    fi
    
    print_info "Flashing to port: $FLASH_PORT"
    print_info "Baudrate: $BAUDRATE"
    
    # Flash the firmware
    if idf.py -p "$FLASH_PORT" -b "$BAUDRATE" flash; then
        print_success "Firmware flashed successfully!"
    else
        print_error "Flash failed"
        exit 1
    fi
}

# Function to start monitor
start_monitor() {
    print_info "Starting serial monitor..."
    print_info "Press Ctrl+] to exit monitor"
    
    if [[ -n "$FLASH_PORT" ]]; then
        idf.py -p "$FLASH_PORT" monitor
    else
        idf.py monitor
    fi
}

# Main function
main() {
    print_info "Xiaozhi ESP32 Build and Flash Script"
    print_info "======================================"
    
    # Check environment
    check_esp_idf
    
    # Configure project
    configure_project
    
    # Build project
    build_project
    
    # Flash firmware
    flash_firmware
    
    # Start monitor if requested
    if [[ "$MONITOR_AFTER_FLASH" == true ]]; then
        start_monitor
    else
        print_success "All operations completed successfully!"
        print_info "To monitor the device, run: idf.py monitor"
        if [[ -n "$FLASH_PORT" ]]; then
            print_info "Or: idf.py -p $FLASH_PORT monitor"
        fi
    fi
}

# Parse command line arguments
while [[ $# -gt 0 ]]; do
    case $1 in
        -b|--board)
            BOARD_TYPE="$2"
            shift 2
            ;;
        -p|--protocol)
            PROTOCOL_TYPE="$2"
            shift 2
            ;;
        -c|--clean)
            CLEAN_BUILD=true
            shift
            ;;
        -m|--monitor)
            MONITOR_AFTER_FLASH=true
            shift
            ;;
        -P|--port)
            FLASH_PORT="$2"
            shift 2
            ;;
        -B|--baudrate)
            BAUDRATE="$2"
            shift 2
            ;;
        -h|--help)
            show_help
            exit 0
            ;;
        *)
            print_error "Unknown option: $1"
            show_help
            exit 1
            ;;
    esac
done

# Run main function
main