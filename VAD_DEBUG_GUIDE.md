# VAD检测问题诊断指南

## 问题描述
用户反馈"我说话了但是没有检测到人声"，需要系统性排查VAD（语音活动检测）问题。

## 已添加的调试功能

### 1. AFE音频处理器调试 (`afe_audio_processor.cc`)
```cpp
// AFE初始化调试
ESP_LOGI(TAG, "🎛️ AFE Configuration:");
ESP_LOGI(TAG, "  📡 Input format: %s", input_format.c_str());
ESP_LOGI(TAG, "  🎤 Input channels: %d (reference: %d)", codec_->input_channels(), ref_num);
ESP_LOGI(TAG, "  🔊 VAD mode: %d", afe_config->vad_mode);
ESP_LOGI(TAG, "  🔇 VAD min noise ms: %d", afe_config->vad_min_noise_ms);
ESP_LOGI(TAG, "  ✅ VAD enabled, Device AEC disabled");

// 原始VAD状态监控
ESP_LOGI(TAG, "🎙️ Raw VAD state: %d, current is_speaking_: %s", 
         res->vad_state, is_speaking_ ? "true" : "false");

// VAD状态变化检测
ESP_LOGI(TAG, "🔊 VAD SPEECH DETECTED! Triggering callback (silence->speech)");
ESP_LOGI(TAG, "🔇 VAD SILENCE DETECTED! Triggering callback (speech->silence)");

// 音频输入级别调试
ESP_LOGD(TAG, "🎵 Feeding audio: %zu samples, max level: %d/32767 (%.1f%%)", 
         data.size(), max_level, (float)max_level * 100.0f / 32767.0f);
```

### 2. 音频服务调试 (`audio_service.cc`)
```cpp
// 麦克风输入启用
ESP_LOGI(TAG, "🎤 Enabling audio input (sample rate: %d, samples: %d)", sample_rate, samples);

// 麦克风信号级别监控（每50次读取记录一次）
ESP_LOGI(TAG, "🎤 Mic input: %zu samples, max level: %d (%.1f%%), rate: %d->%d, channels: %d", 
         data.size(), max_level, (float)max_level * 100.0f / 32767.0f,
         codec_->input_sample_rate(), sample_rate, codec_->input_channels());

// 音频处理器feed size检查
ESP_LOGD(TAG, "🎙️ Audio processor feed size: %d samples", samples);
ESP_LOGI(TAG, "🎵 Read audio: %zu samples, max level: %d (%.1f%%)", 
         data.size(), max_level, (float)max_level * 100.0f / 32767.0f);
```

### 3. 应用层VAD回调调试 (`application.cc`)
```cpp
callbacks.on_vad_change = [this](bool speaking) {
    ESP_LOGI(TAG, "🎤 VAD State Changed: %s (recording=%s, state=%s)", 
             speaking ? "VOICE_DETECTED" : "VOICE_STOPPED",
             vad_trigger_recording_ ? "enabled" : "disabled",
             STATE_STRINGS[device_state_]);
```

### 4. VAD敏感度优化
```cpp
// 更敏感的VAD设置
afe_config->vad_mode = VAD_MODE_0;  // 0=normal, 1=loose, 2=strict
afe_config->vad_min_noise_ms = 50;  // 从100ms减少到50ms提高敏感度
```

## 诊断步骤

### 步骤1: 检查构建环境
由于当前ESP-IDF环境有问题，需要先修复：

```bash
# 方法1: 重新安装ESP-IDF组件管理器
pip install idf-component-manager

# 方法2: 清理并重新安装ESP-IDF
rm -rf /Users/yushuangyang/.espressif/python_env
./setup_esp_idf.sh

# 方法3: 使用Docker环境（推荐）
docker run --rm -v $PWD:/project -w /project espressif/idf:v5.4.0 idf.py build
```

### 步骤2: 构建并烧录调试版本
```bash
# 一旦环境修复，运行调试构建脚本
./test_vad.sh

# 或者手动构建
source /Users/yushuangyang/esp/esp-idf/export.sh
idf.py build
idf.py flash monitor
```

### 步骤3: 观察调试输出
监控串口输出，寻找以下关键信息：

#### 3.1 AFE初始化检查
```
🎛️ AFE Configuration:
  📡 Input format: M
  🎤 Input channels: 1 (reference: 0)
  📊 Frame samples: 960
  🔊 VAD mode: 0
  🔇 VAD min noise ms: 50
  ✅ VAD enabled, Device AEC disabled
  🚀 AFE initialized successfully, VAD init: ENABLED
```

#### 3.2 麦克风输入检查
```
🎤 Enabling audio input (sample rate: 16000, samples: 960)
🎤 Mic input: 960 samples, max level: 1234 (3.8%), rate: 16000->16000, channels: 1
```

**关键指标：**
- `max_level` 应该 > 0（有信号输入）
- 说话时 `max_level` 应该显著增加（通常 > 5-10%）
- `channels: 1` 确认单声道配置

#### 3.3 VAD处理检查
```
🎙️ Raw VAD state: 0, current is_speaking_: false
🎙️ Raw VAD state: 1, current is_speaking_: false
🔊 VAD SPEECH DETECTED! Triggering callback (silence->speech)
```

**VAD状态值：**
- `0` = VAD_SILENCE（静音）
- `1` = VAD_SPEECH（有语音）

#### 3.4 应用层回调检查
```
🎤 VAD State Changed: VOICE_DETECTED (recording=enabled, state=IDLE)
🌐 === HTTP POST REQUEST START ===
```

### 步骤4: 问题排查

#### 问题A: 看不到"🎤 Mic input"日志
**原因：** 麦克风硬件或I2S配置问题
**解决：**
1. 检查麦克风硬件连接
2. 确认I2S引脚配置正确
3. 检查音频编解码器初始化

#### 问题B: 麦克风输入 max_level = 0
**原因：** 麦克风无信号输入
**解决：**
1. 检查麦克风电源供应
2. 确认麦克风未被静音
3. 测试麦克风硬件是否损坏
4. 检查增益设置

#### 问题C: 有输入但VAD state始终为0
**原因：** VAD敏感度设置过低
**解决：**
1. 尝试不同的VAD模式：
   ```cpp
   afe_config->vad_mode = VAD_MODE_1;  // 更宽松的检测
   ```
2. 进一步降低最小噪声阈值：
   ```cpp
   afe_config->vad_min_noise_ms = 30;  // 更敏感
   ```
3. 增加麦克风增益

#### 问题D: VAD检测到但没有HTTP传输
**原因：** 回调函数设置问题
**解决：**
1. 检查 `vad_trigger_recording_` 设置
2. 确认HTTP协议已正确配置
3. 检查网络连接状态

## 临时测试方法

如果无法构建，可以先检查：

### 1. 硬件连接测试
```bash
# 检查USB设备是否识别
ls /dev/cu.* | grep -E "(usbserial|SLAB|usbmodem)"

# 测试串口连接
screen /dev/cu.usbserial-XXX 115200
```

### 2. 使用现有固件测试
如果设备上已有固件，可以先：
1. 复位设备观察启动日志
2. 检查WiFi连接状态
3. 测试唤醒词功能
4. 观察现有音频处理日志

### 3. 检查配置文件
```bash
# 检查sdkconfig中的VAD相关配置
grep -i vad sdkconfig*
grep -i afe sdkconfig*
grep -i audio sdkconfig*
```

## 快速修复建议

1. **立即可行的操作：**
   - 检查硬件连接
   - 重启设备
   - 确认麦克风未被遮挡

2. **构建环境修复后：**
   - 烧录调试版本
   - 分析串口日志
   - 根据日志输出定位具体问题

3. **如果问题持续存在：**
   - 考虑使用更敏感的VAD设置
   - 检查音频编解码器配置
   - 测试不同的麦克风硬件

## 联系支持

如果按照本指南仍无法解决问题，请提供：
1. 完整的串口启动日志
2. 硬件连接照片
3. 使用的开发板型号
4. ESP-IDF版本信息