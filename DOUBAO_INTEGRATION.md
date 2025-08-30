# 豆包端到端语音集成说明

## 功能概述

本项目已集成豆包（Doubao）端到端实时语音大模型API，支持低延迟的语音对话功能。该集成实现了：

- **端到端语音交互**：无需单独的ASR和TTS模块，直接实现语音输入到语音输出
- **低延迟对话**：流式处理架构，实现实时语音交互
- **自动语音检测**：内置VAD功能，自动检测用户说话开始和结束
- **高质量音频**：支持PCM 16kHz输入，24kHz输出，保证音频质量

## 配置说明

### 1. 启用豆包协议

在编译固件时，通过menuconfig选择豆包协议：

```bash
idf.py menuconfig
```

导航到：`Xiaozhi Assistant → Communication Protocol → Doubao End-to-End Voice Protocol`

### 2. API凭证配置

当前API凭证已硬编码在 `main/protocols/doubao_protocol.cc` 中：

```cpp
static constexpr const char* APP_ID = "7059594059";
static constexpr const char* ACCESS_TOKEN = "tRDp6c2pMhqtMXWYCINDSCDQPyfaWZbt";
```

如需更改，请编辑该文件中的相应常量。

## 技术实现

### 协议架构

1. **WebSocket连接**：使用WebSocket连接到豆包服务器 `wss://openspeech.bytedance.com/api/v3/realtime/dialogue`

2. **二进制协议**：实现豆包的二进制协议格式，包括：
   - 4字节协议头
   - 可选字段（event ID, session ID等）
   - 负载数据（音频或JSON）

3. **事件交互流程**：
   ```
   ESP32 → StartConnection → 服务器
   ESP32 → StartSession → 服务器
   ESP32 → TaskRequest(音频) → 服务器
   服务器 → TTSResponse(音频) → ESP32
   服务器 → ASRResponse(文本) → ESP32
   ```

### 音频处理

- **输入**：16kHz PCM音频，单声道，16位采样
- **输出**：24kHz PCM音频，自动重采样后播放
- **持续发送**：即使无语音也需持续发送静音数据保持连接

### 主要文件

- `main/protocols/doubao_protocol.h` - 协议头文件
- `main/protocols/doubao_protocol.cc` - 协议实现
- `main/Kconfig.projbuild` - 配置选项
- `main/application.cc` - 协议选择逻辑

## 使用流程

1. **编译固件**：
   ```bash
   idf.py menuconfig  # 选择Doubao协议
   idf.py build
   ```

2. **刷写固件**：
   ```bash
   idf.py flash
   ```

3. **运行设备**：
   - 设备启动后自动连接WiFi
   - 连接成功后自动连接豆包服务器
   - 对设备说话即可开始对话（无需唤醒词）

## 特性说明

### 优势

- **简化架构**：无需中间服务器，直接连接豆包
- **低延迟**：端到端处理，减少中间环节
- **自然对话**：VAD自动检测，无需唤醒词
- **高质量音频**：支持高采样率音频

### 限制

- 需要稳定的网络连接
- QPM限流：每分钟StartSession事件有配额限制
- TPM限流：每分钟消耗的token有配额限制

## 调试信息

通过串口监视器查看运行日志：

```bash
idf.py monitor
```

关键日志标签：
- `DoubaoProtocol` - 协议层日志
- `Application` - 应用层日志
- `AudioService` - 音频服务日志

## 故障排除

1. **连接失败**：
   - 检查WiFi连接
   - 验证API凭证是否正确
   - 查看串口日志中的错误信息

2. **音频问题**：
   - 确认麦克风和扬声器连接正确
   - 检查音频编解码器配置
   - 验证音频采样率设置

3. **对话中断**：
   - 检查网络稳定性
   - 查看是否触发限流
   - 确认持续发送音频数据

## 后续优化

- [ ] 支持通过NVS存储API凭证
- [ ] 添加更多音色选择
- [ ] 实现断线重连机制
- [ ] 优化音频缓冲管理
- [ ] 添加对话历史记录功能

## 参考文档

- [豆包端到端实时语音大模型API文档](demo/demo.txt)
- [Python示例代码](demo/)
- [ESP32音频处理文档](main/audio/README.md)