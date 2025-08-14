#!/bin/bash

# 小智ESP32 Docker环境快速启动脚本

set -e

COLOR_GREEN='\033[0;32m'
COLOR_BLUE='\033[0;34m'
COLOR_YELLOW='\033[1;33m'
COLOR_NC='\033[0m'

log_info() {
    echo -e "${COLOR_BLUE}[INFO]${COLOR_NC} $1"
}

log_success() {
    echo -e "${COLOR_GREEN}[SUCCESS]${COLOR_NC} $1"
}

log_warning() {
    echo -e "${COLOR_YELLOW}[WARNING]${COLOR_NC} $1"
}

# 检查Docker
check_docker() {
    log_info "检查Docker环境..."
    
    if ! command -v docker &> /dev/null; then
        echo "Docker 未安装，请先安装 Docker Desktop"
        echo "下载地址: https://www.docker.com/products/docker-desktop/"
        exit 1
    fi
    
    if ! docker info &> /dev/null; then
        echo "Docker 未运行，请启动 Docker Desktop"
        exit 1
    fi
    
    log_success "Docker 环境检查通过"
}

# 查找USB设备
find_usb_device() {
    log_info "查找ESP32设备..."
    
    USB_DEVICE=""
    
    if [[ "$OSTYPE" == "darwin"* ]]; then
        # macOS
        for device in /dev/tty.usbserial-* /dev/tty.SLAB_USBtoUART /dev/tty.usbmodem*; do
            if [ -e "$device" ]; then
                USB_DEVICE="$device"
                log_success "找到设备: $USB_DEVICE"
                break
            fi
        done
    else
        # Linux
        for device in /dev/ttyUSB* /dev/ttyACM*; do
            if [ -e "$device" ]; then
                USB_DEVICE="$device"
                log_success "找到设备: $USB_DEVICE"
                break
            fi
        done
    fi
    
    if [ -z "$USB_DEVICE" ]; then
        log_warning "未找到ESP32设备，请检查设备连接"
        log_warning "继续启动容器，稍后可手动指定设备"
    fi
}

# 构建Docker镜像
build_image() {
    log_info "构建Docker镜像..."
    
    if docker images | grep -q "xiaozhi-esp32"; then
        log_info "Docker镜像已存在，跳过构建"
    else
        docker build -t xiaozhi-esp32:latest .
        log_success "Docker镜像构建完成"
    fi
}

# 启动容器
start_container() {
    log_info "启动开发容器..."
    
    # 停止已存在的容器
    docker stop xiaozhi-esp32-dev 2>/dev/null || true
    docker rm xiaozhi-esp32-dev 2>/dev/null || true
    
    DOCKER_ARGS=(
        "--name" "xiaozhi-esp32-dev"
        "-it"
        "--privileged"
        "-v" "$(pwd):/workspace"
        "-v" "esp-idf-cache:/opt/esp-idf-tools"
        "-v" "esp-components-cache:/workspace/managed_components"
        "-v" "esp-build-cache:/workspace/build"
        "-w" "/workspace"
    )
    
    # 添加USB设备
    if [ -n "$USB_DEVICE" ]; then
        DOCKER_ARGS+=("--device" "$USB_DEVICE:$USB_DEVICE")
    fi
    
    # 启动容器
    docker run "${DOCKER_ARGS[@]}" xiaozhi-esp32:latest
}

# VS Code集成提示
show_vscode_info() {
    echo
    echo "=========================================="
    echo "  VS Code Dev Container 使用方法"
    echo "=========================================="
    echo
    echo "1. 安装 VS Code 扩展: 'Dev Containers'"
    echo "2. 在 VS Code 中打开此项目"
    echo "3. 按 Ctrl+Shift+P (Cmd+Shift+P)"
    echo "4. 选择: 'Dev Containers: Reopen in Container'"
    echo
    echo "或者直接运行当前脚本使用 Docker 命令行环境"
    echo
}

# Docker Compose方式
start_with_compose() {
    log_info "使用Docker Compose启动..."
    
    # 动态设置USB设备
    if [ -n "$USB_DEVICE" ]; then
        export ESP32_DEVICE="$USB_DEVICE"
        # 创建临时的docker-compose配置
        cat > docker-compose.override.yml << EOF
version: '3.8'
services:
  xiaozhi-dev:
    devices:
      - "$USB_DEVICE:$USB_DEVICE"
EOF
        log_success "已配置USB设备: $USB_DEVICE"
    fi
    
    docker-compose up -d xiaozhi-dev
    docker-compose exec xiaozhi-dev /bin/bash
}

# 主菜单
show_menu() {
    echo "=========================================="
    echo "  小智ESP32 Docker环境启动"
    echo "=========================================="
    echo
    echo "请选择启动方式:"
    echo "1. Docker Compose (推荐)"
    echo "2. Docker 命令行"
    echo "3. 显示 VS Code 使用说明"
    echo "4. 退出"
    echo
}

# 主函数
main() {
    check_docker
    find_usb_device
    build_image
    
    while true; do
        show_menu
        read -p "请输入选择 [1-4]: " choice
        
        case $choice in
            1)
                start_with_compose
                break
                ;;
            2)
                start_container
                break
                ;;
            3)
                show_vscode_info
                ;;
            4)
                log_info "退出"
                exit 0
                ;;
            *)
                echo "无效选择，请重新输入"
                ;;
        esac
    done
}

main "$@"