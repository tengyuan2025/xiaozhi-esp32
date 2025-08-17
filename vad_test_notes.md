# VAD触发录音功能测试说明

## 修改概述

本次修改实现了VAD（Voice Activity Detection）直接触发录音功能，让设备在检测到语音时无需唤醒词就能自动开始录音并发送到服务器。

## 主要修改文件

### 1. `/main/application.h`
- 添加了 `vad_trigger_recording_` 成员变量
- 添加了 `SetVadTriggerRecording()` 和 `IsVadTriggerRecordingEnabled()` 方法
- 添加了 `OnVadDetected()` 私有方法

### 2. `/main/application.cc`
- 修改VAD回调处理逻辑，在检测到语音时自动触发录音
- 添加 `SetVadTriggerRecording()` 方法实现
- 添加 `OnVadDetected()` 方法实现
- 修改设备状态管理，支持VAD触发录音模式
- 在构造函数中根据配置初始化VAD触发录音状态

### 3. `/main/Kconfig.projbuild`
- 添加 `CONFIG_VAD_TRIGGER_RECORDING` 配置选项

## 工作原理

### 正常模式（唤醒词模式）
1. 设备处于 `kDeviceStateIdle` 状态
2. 启用唤醒词检测：`EnableWakeWordDetection(true)`
3. 禁用语音处理：`EnableVoiceProcessing(false)`
4. 等待唤醒词触发

### VAD触发录音模式
1. 设备处于 `kDeviceStateIdle` 状态
2. 禁用唤醒词检测：`EnableWakeWordDetection(false)`
3. 启用语音处理：`EnableVoiceProcessing(true)`
4. VAD持续监听，检测到语音时自动开始录音

## 配置方法

### 编译时配置
在 `menuconfig` 中启用：
```
Xiaozhi Assistant → Enable VAD Triggered Recording
```

### 运行时配置
```cpp
Application& app = Application::GetInstance();
app.SetVadTriggerRecording(true);  // 启用VAD触发录音
app.SetVadTriggerRecording(false); // 禁用VAD触发录音
```

## 测试步骤

1. **编译配置**：
   ```bash
   idf.py menuconfig
   # 导航到 Xiaozhi Assistant → Enable VAD Triggered Recording
   # 选择 [*] Enable VAD Triggered Recording
   ```

2. **编译烧录**：
   ```bash
   idf.py build
   idf.py flash monitor
   ```

3. **功能测试**：
   - 启动设备，观察日志输出 "VAD trigger recording enabled"
   - 对着设备说话，观察是否自动进入 listening 状态
   - 检查音频数据是否正常传输到服务器

4. **切换测试**：
   ```cpp
   // 可以通过调用API动态切换模式
   app.SetVadTriggerRecording(false); // 切换回唤醒词模式
   app.SetVadTriggerRecording(true);  // 切换回VAD触发模式
   ```

## 关键代码逻辑

### VAD检测回调
```cpp
callbacks.on_vad_change = [this](bool speaking) {
    xEventGroupSetBits(event_group_, MAIN_EVENT_VAD_CHANGE);
    // 如果启用了VAD触发录音模式，且检测到语音，自动开始录音
    if (vad_trigger_recording_ && speaking && device_state_ == kDeviceStateIdle) {
        Schedule([this]() {
            OnVadDetected();
        });
    }
};
```

### VAD触发录音处理
```cpp
void Application::OnVadDetected() {
    if (device_state_ == kDeviceStateIdle) {
        ESP_LOGI(TAG, "VAD detected, starting recording");
        
        if (!protocol_->IsAudioChannelOpened()) {
            SetDeviceState(kDeviceStateConnecting);
            if (!protocol_->OpenAudioChannel()) {
                return;
            }
        }

        SetListeningMode(kListeningModeAutoStop);
    }
}
```

## 注意事项

1. VAD触发录音模式会增加功耗，因为需要持续进行语音处理
2. 确保服务器端支持频繁的音频连接建立和断开
3. 可以通过调整VAD敏感度来优化触发效果
4. 建议在实际使用中根据场景需求动态切换模式

## 预期效果

启用VAD触发录音后：
- 无需说唤醒词
- 检测到语音即开始录音传输
- 静音时自动停止录音
- 支持运行时动态切换模式