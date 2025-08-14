# 小智ESP32团队开发指南

快速设置指南，帮助团队成员快速搭建开发环境。

## 🚀 快速开始

### 方法1: Docker 一键启动 (推荐)

最快速的方式，无需手动安装依赖：

```bash
# 克隆项目
git clone <项目地址>
cd xiaozhi-esp32

# 启动Docker开发环境
chmod +x setup-docker.sh
./setup-docker.sh
```

选择选项1使用Docker Compose，脚本会自动：
- 检查Docker环境
- 检测ESP32设备
- 构建开发镜像
- 启动容器并进入开发环境

### 方法2: VS Code Dev Container

如果你使用VS Code，这是最佳选择：

1. 安装VS Code扩展：`Dev Containers`
2. 打开项目文件夹
3. 按 `Ctrl+Shift+P` (Mac: `Cmd+Shift+P`)
4. 选择 `Dev Containers: Reopen in Container`

VS Code会自动构建开发环境并安装推荐的扩展。

### 方法3: 本地安装

如果你偏好本地开发环境：

```bash
# 运行一键安装脚本
chmod +x setup.sh
./setup.sh
```

## 📋 系统要求

### 必需软件
- **Docker Desktop** (方法1和2)
- **VS Code** + Dev Containers扩展 (方法2)
- **Python 3.8+** (方法3)
- **Git** (所有方法)

### 硬件要求
- ESP32-S3开发板
- USB数据线
- 8GB+ RAM (Docker环境)

## 🛠 开发流程

### 编译项目

进入开发环境后：

```bash
# 设置目标芯片
idf.py set-target esp32s3

# 配置项目 (可选)
idf.py menuconfig

# 编译
idf.py build
```

### 烧录固件

```bash
# 连接设备后烧录
idf.py flash

# 查看串口输出
idf.py monitor

# 或者一步完成
idf.py flash monitor
```

### 常用命令

```bash
# 清理构建
idf.py clean

# 查看设备信息
idf.py flash_id

# 擦除设备
idf.py erase-flash
```

## 🎯 开发板配置

项目支持多种ESP32开发板：

- **ESP32-S3-DevKitC-1**: 默认配置
- **ESP-S3-EV-Board-2**: 8MB PSRAM
- **自定义开发板**: 修改 `boards/` 目录配置

选择开发板：
```bash
idf.py set-target esp32s3
idf.py menuconfig
# 选择对应的开发板配置
```

## 🔧 故障排除

### 常见问题

**1. Docker构建失败**
```bash
# 清理Docker缓存
docker system prune -a
./setup-docker.sh
```

**2. 设备未识别**
```bash
# 检查设备连接
ls /dev/tty* | grep -E "(USB|ACM)"

# macOS检查
ls /dev/tty.* | grep -E "(usbserial|SLAB|usbmodem)"
```

**3. 编译错误**
```bash
# 清理构建缓存
idf.py clean
rm -rf build managed_components
idf.py build
```

**4. WiFi库缺失**
```bash
# 重新安装ESP-IDF（本地环境）
cd ~/esp-idf
git submodule update --init --recursive
./install.sh esp32s3
```

### 环境变量检查

Docker环境中验证：
```bash
echo $IDF_PATH          # 应该是 /opt/esp-idf
echo $IDF_TOOLS_PATH    # 应该是 /opt/esp-idf-tools
which idf.py            # 应该找到命令
```

本地环境中验证：
```bash
source ~/esp-idf/export.sh
idf.py --version
```

## 📝 项目结构

```
xiaozhi-esp32/
├── main/                 # 主程序代码
├── components/           # 自定义组件
├── boards/              # 开发板配置
├── managed_components/  # 依赖组件(自动生成)
├── build/              # 构建输出(自动生成)
├── docs/               # 文档
├── CLAUDE.md           # AI助手项目文档
├── setup.sh            # 本地环境安装脚本
├── setup-docker.sh     # Docker环境启动脚本
├── Dockerfile          # Docker镜像定义
└── docker-compose.yml  # Docker编排配置
```

## 🤝 协作规范

### 代码提交

1. **功能分支开发**
   ```bash
   git checkout -b feature/新功能名称
   ```

2. **代码格式化**
   ```bash
   # C/C++代码格式化
   idf.py clang-format
   ```

3. **测试验证**
   ```bash
   idf.py build        # 编译测试
   idf.py flash monitor # 硬件测试
   ```

### 开发环境同步

团队使用统一的开发环境确保一致性：
- **ESP-IDF版本**: v5.4
- **Python版本**: 3.10+
- **CMake版本**: 3.20+

## 📚 相关资源

- [ESP-IDF编程指南](https://docs.espressif.com/projects/esp-idf/zh_CN/v5.4/)
- [ESP32-S3技术参考手册](https://www.espressif.com/sites/default/files/documentation/esp32-s3_technical_reference_manual_cn.pdf)
- [项目完整文档](./CLAUDE.md)
- [开发日志](./DEVELOPMENT_LOG.md)

## 💡 技巧和最佳实践

### 性能优化
- 使用PSRAM缓存大型数据
- 合理配置WiFi和蓝牙功耗
- 优化LVGL渲染性能

### 调试技巧
- 使用 `ESP_LOG*` 宏进行日志输出
- 启用coredump分析崩溃问题
- 使用GDB调试复杂问题

### 安全考虑
- 不要在代码中硬编码密钥
- 使用NVS安全存储敏感数据
- 定期更新依赖组件

---

需要帮助？请查看 [故障排除](#故障排除) 部分或联系团队维护者。