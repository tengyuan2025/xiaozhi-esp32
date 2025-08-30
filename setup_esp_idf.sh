#!/bin/bash

# ESP-IDF Auto Setup Script for macOS
# ESP-IDF 自动安装配置脚本

set -e

# Colors for output
RED='\033[0;31m'
GREEN='\033[0;32m'
YELLOW='\033[1;33m'
BLUE='\033[0;34m'
NC='\033[0m' # No Color

# Configuration
ESP_IDF_VERSION="v5.4"  # Recommended version for this project
ESP_IDF_DIR="$HOME/esp/esp-idf"
TOOLS_DIR="$HOME/esp"

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

# Function to check system requirements
check_system() {
    print_info "Checking system requirements..."
    
    # Check if running on macOS
    if [[ "$(uname)" != "Darwin" ]]; then
        print_error "This script is designed for macOS. For other systems, please refer to ESP-IDF documentation."
        exit 1
    fi
    
    # Check if Homebrew is installed
    if ! command -v brew &> /dev/null; then
        print_error "Homebrew is required but not installed."
        print_info "Please install Homebrew first: /bin/bash -c \"\$(curl -fsSL https://raw.githubusercontent.com/Homebrew/install/HEAD/install.sh)\""
        exit 1
    fi
    
    print_success "System check passed"
}

# Function to install prerequisites
install_prerequisites() {
    print_info "Installing prerequisites..."
    
    # Install required packages
    brew_packages=(
        "cmake"
        "ninja"
        "dfu-util"
        "python3"
        "git"
        "wget"
        "libusb"
        "pkg-config"
    )
    
    for package in "${brew_packages[@]}"; do
        if brew list "$package" &>/dev/null; then
            print_info "$package is already installed"
        else
            print_info "Installing $package..."
            brew install "$package"
        fi
    done
    
    print_success "Prerequisites installed"
}

# Function to download and install ESP-IDF
install_esp_idf() {
    print_info "Installing ESP-IDF $ESP_IDF_VERSION..."
    
    # Create ESP tools directory
    mkdir -p "$TOOLS_DIR"
    cd "$TOOLS_DIR"
    
    # Check if ESP-IDF already exists
    if [[ -d "$ESP_IDF_DIR" ]]; then
        print_warning "ESP-IDF directory already exists at $ESP_IDF_DIR"
        read -p "Do you want to remove it and reinstall? (y/N): " -n 1 -r
        echo
        if [[ $REPLY =~ ^[Yy]$ ]]; then
            rm -rf "$ESP_IDF_DIR"
        else
            print_info "Using existing ESP-IDF installation"
            cd "$ESP_IDF_DIR"
            git checkout "$ESP_IDF_VERSION"
            git pull
            return 0
        fi
    fi
    
    # Clone ESP-IDF
    print_info "Cloning ESP-IDF repository..."
    git clone -b "$ESP_IDF_VERSION" --recursive https://github.com/espressif/esp-idf.git
    
    cd "$ESP_IDF_DIR"
    
    print_success "ESP-IDF downloaded successfully"
}

# Function to install ESP-IDF tools
install_esp_tools() {
    print_info "Installing ESP-IDF tools..."
    
    cd "$ESP_IDF_DIR"
    
    # Install ESP-IDF tools
    ./install.sh esp32,esp32s3,esp32c3,esp32c6,esp32p4
    
    print_success "ESP-IDF tools installed"
}

# Function to setup environment
setup_environment() {
    print_info "Setting up environment..."
    
    # Create environment setup script
    cat > "$HOME/.esp_idf_env" << 'EOF'
# ESP-IDF Environment Setup
export IDF_PATH="$HOME/esp/esp-idf"
alias get_idf='. $IDF_PATH/export.sh'
alias idf='idf.py'

# Auto-source ESP-IDF if in xiaozhi-esp32 project directory
if [[ -f "CMakeLists.txt" ]] && [[ -d "main" ]] && grep -q "xiaozhi" CMakeLists.txt 2>/dev/null; then
    if ! command -v idf.py &> /dev/null; then
        echo "Auto-sourcing ESP-IDF environment for xiaozhi project..."
        . $IDF_PATH/export.sh
    fi
fi
EOF

    # Add to shell profiles
    for profile in ~/.bashrc ~/.bash_profile ~/.zshrc; do
        if [[ -f "$profile" ]] && ! grep -q ".esp_idf_env" "$profile"; then
            echo "" >> "$profile"
            echo "# ESP-IDF Environment" >> "$profile"
            echo "source \$HOME/.esp_idf_env" >> "$profile"
            print_info "Added ESP-IDF environment to $profile"
        fi
    done
    
    print_success "Environment setup completed"
}

# Function to verify installation
verify_installation() {
    print_info "Verifying installation..."
    
    # Source ESP-IDF environment
    source "$ESP_IDF_DIR/export.sh"
    
    # Check if idf.py is available
    if command -v idf.py &> /dev/null; then
        local version=$(idf.py --version 2>/dev/null | head -n1)
        print_success "ESP-IDF installed successfully: $version"
    else
        print_error "Installation verification failed"
        return 1
    fi
    
    # Test with a simple command
    print_info "Testing ESP-IDF tools..."
    if idf.py --help &> /dev/null; then
        print_success "ESP-IDF tools are working correctly"
    else
        print_warning "ESP-IDF tools might have issues"
    fi
}

# Function to create USB driver setup instructions
setup_usb_drivers() {
    print_info "Setting up USB drivers for ESP32..."
    
    # Check if CH340/CP2102 drivers are needed
    print_info "Common ESP32 USB-to-Serial chips require drivers:"
    print_info "1. CH340/CH341 - Usually works without additional drivers on macOS"
    print_info "2. CP2102/CP2104 - Download from Silicon Labs if needed"
    print_info "3. FTDI - Usually built into macOS"
    
    # Create a helper script for USB issues
    cat > "$HOME/esp/usb_troubleshoot.sh" << 'EOF'
#!/bin/bash
echo "ESP32 USB Troubleshooting"
echo "========================"
echo "1. Check connected devices:"
ls /dev/cu.* 2>/dev/null || echo "No USB devices found"
echo ""
echo "2. Common ESP32 device patterns:"
echo "   - /dev/cu.usbserial-*"
echo "   - /dev/cu.SLAB_USBtoUART*"
echo "   - /dev/cu.usbmodem*"
echo ""
echo "3. If no devices found:"
echo "   - Check USB cable (use data cable, not charging-only)"
echo "   - Install CP2102 driver if needed: https://www.silabs.com/developers/usb-to-uart-bridge-vcp-drivers"
echo "   - Try different USB port"
echo "   - Check if device is in bootloader mode"
EOF
    
    chmod +x "$HOME/esp/usb_troubleshoot.sh"
    
    print_info "Created USB troubleshooting script at: $HOME/esp/usb_troubleshoot.sh"
    print_success "USB driver setup completed"
}

# Function to show next steps
show_next_steps() {
    print_success "ESP-IDF installation completed!"
    print_info ""
    print_info "Next steps:"
    print_info "1. Restart your terminal or source the environment:"
    print_info "   source ~/.zshrc  # or ~/.bashrc"
    print_info ""
    print_info "2. Navigate to your xiaozhi-esp32 project and build:"
    print_info "   cd $(pwd)"
    print_info "   ./build_and_flash.sh --protocol doubao --clean --monitor"
    print_info ""
    print_info "3. If you encounter USB device issues, run:"
    print_info "   $HOME/esp/usb_troubleshoot.sh"
    print_info ""
    print_info "Useful commands:"
    print_info "  get_idf                    # Source ESP-IDF environment manually"
    print_info "  idf.py menuconfig          # Configure project"
    print_info "  idf.py build              # Build project"
    print_info "  idf.py flash monitor      # Flash and monitor"
}

# Main installation function
main() {
    print_info "ESP-IDF Automatic Setup for xiaozhi-esp32"
    print_info "=========================================="
    
    check_system
    install_prerequisites
    install_esp_idf
    install_esp_tools
    setup_environment
    setup_usb_drivers
    verify_installation
    show_next_steps
}

# Parse command line arguments
while [[ $# -gt 0 ]]; do
    case $1 in
        -v|--version)
            ESP_IDF_VERSION="$2"
            shift 2
            ;;
        -h|--help)
            echo "ESP-IDF Setup Script"
            echo "Usage: $0 [OPTIONS]"
            echo ""
            echo "Options:"
            echo "  -v, --version VERSION    Specify ESP-IDF version (default: $ESP_IDF_VERSION)"
            echo "  -h, --help              Show this help"
            exit 0
            ;;
        *)
            print_error "Unknown option: $1"
            exit 1
            ;;
    esac
done

# Run main installation
main