#!/bin/bash

# 小智ESP32开发环境一键安装脚本
# 支持 macOS 和 Linux

set -e

COLOR_RED='\033[0;31m'
COLOR_GREEN='\033[0;32m'
COLOR_YELLOW='\033[1;33m'
COLOR_BLUE='\033[0;34m'
COLOR_NC='\033[0m' # No Color

# 日志函数
log_info() {
    echo -e "${COLOR_BLUE}[INFO]${COLOR_NC} $1"
}

log_success() {
    echo -e "${COLOR_GREEN}[SUCCESS]${COLOR_NC} $1"
}

log_warning() {
    echo -e "${COLOR_YELLOW}[WARNING]${COLOR_NC} $1"
}

log_error() {
    echo -e "${COLOR_RED}[ERROR]${COLOR_NC} $1"
}

# 检查系统类型
detect_os() {
    if [[ "$OSTYPE" == "darwin"* ]]; then
        OS="macos"
        log_info "检测到 macOS 系统"
    elif [[ "$OSTYPE" == "linux-gnu"* ]]; then
        OS="linux"
        log_info "检测到 Linux 系统"
    else
        log_error "不支持的操作系统: $OSTYPE"
        exit 1
    fi
}

# 检查依赖
check_dependencies() {
    log_info "检查系统依赖..."
    
    # 检查Python
    if ! command -v python3 &> /dev/null; then
        log_error "Python 3 未安装，请先安装 Python 3.8+"
        exit 1
    fi
    
    # 检查Git
    if ! command -v git &> /dev/null; then
        log_error "Git 未安装，请先安装 Git"
        exit 1
    fi
    
    # 检查curl/wget
    if ! command -v curl &> /dev/null && ! command -v wget &> /dev/null; then
        log_error "curl 或 wget 未安装"
        exit 1
    fi
    
    log_success "系统依赖检查通过"
}

# 安装ESP-IDF
install_esp_idf() {
    log_info "开始安装 ESP-IDF..."
    
    IDF_PATH="$HOME/esp-idf"
    
    if [ -d "$IDF_PATH" ]; then
        log_warning "ESP-IDF 已存在，跳过下载"
    else
        log_info "下载 ESP-IDF v5.4..."
        git clone --recursive --depth 1 --branch v5.4 \
            https://github.com/espressif/esp-idf.git "$IDF_PATH"
    fi
    
    cd "$IDF_PATH"
    
    # 安装工具链
    log_info "安装 ESP32-S3 工具链..."
    ./install.sh esp32s3
    
    # 设置环境变量
    if ! grep -q "source $IDF_PATH/export.sh" ~/.bashrc; then
        echo "# ESP-IDF" >> ~/.bashrc
        echo "source $IDF_PATH/export.sh" >> ~/.bashrc
        log_success "已添加 ESP-IDF 环境变量到 ~/.bashrc"
    fi
    
    if ! grep -q "source $IDF_PATH/export.sh" ~/.zshrc; then
        echo "# ESP-IDF" >> ~/.zshrc
        echo "source $IDF_PATH/export.sh" >> ~/.zshrc
        log_success "已添加 ESP-IDF 环境变量到 ~/.zshrc"
    fi
    
    log_success "ESP-IDF 安装完成"
}

# 验证安装
verify_installation() {
    log_info "验证安装..."
    
    # 加载环境变量
    source "$HOME/esp-idf/export.sh"
    
    # 检查idf.py命令
    if command -v idf.py &> /dev/null; then
        log_success "idf.py 命令可用"
        idf.py --version
    else
        log_error "idf.py 命令不可用"
        return 1
    fi
    
    # 检查编译工具链
    if command -v xtensa-esp32s3-elf-gcc &> /dev/null; then
        log_success "ESP32-S3 工具链安装成功"
    else
        log_error "ESP32-S3 工具链安装失败"
        return 1
    fi
}

# 编译项目
build_project() {
    log_info "编译小智AI项目..."
    
    # 返回项目目录
    cd "$(dirname "$0")"
    
    # 加载环境变量
    source "$HOME/esp-idf/export.sh"
    
    # 设置目标
    idf.py set-target esp32s3
    
    # 编译
    idf.py build
    
    log_success "项目编译完成"
}

# 主函数
main() {
    echo "=========================================="
    echo "  小智ESP32开发环境一键安装脚本"
    echo "=========================================="
    
    detect_os
    check_dependencies
    install_esp_idf
    verify_installation
    
    read -p "是否立即编译项目？(y/N): " -n 1 -r
    echo
    if [[ $REPLY =~ ^[Yy]$ ]]; then
        build_project
    fi
    
    echo
    log_success "安装完成！"
    echo
    echo "使用说明："
    echo "1. 重启终端或运行: source ~/.bashrc"
    echo "2. 连接ESP32-S3设备"
    echo "3. 运行: idf.py flash monitor"
    echo
    echo "Docker使用："
    echo "运行: ./setup-docker.sh"
    echo
}

# 错误处理
trap 'log_error "脚本执行失败，退出码: $?"' ERR

# 执行主函数
main "$@"