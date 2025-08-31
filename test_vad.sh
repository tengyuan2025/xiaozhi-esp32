#!/bin/bash

echo "🔧 Building firmware with VAD debug logging..."

# 退出conda环境
if command -v conda &> /dev/null; then
    echo "📦 Deactivating conda environment..."
    conda deactivate 2>/dev/null || true
fi

# 设置ESP-IDF环境
echo "🔧 Setting up ESP-IDF environment..."
source ~/esp/esp-idf/export.sh

# 清理构建缓存（如果需要）
echo "🧹 Cleaning build cache..."
idf.py fullclean

# 构建项目
echo "🔨 Building project..."
idf.py build

if [ $? -eq 0 ]; then
    echo "✅ Build successful! Flashing to device..."
    idf.py flash
    
    if [ $? -eq 0 ]; then
        echo "✅ Flash successful! Starting monitor..."
        echo "🎤 Please speak into the microphone to test VAD detection"
        echo "🔍 Look for these debug messages:"
        echo "   - '🎛️ AFE Configuration' - AFE initialization"
        echo "   - '🎤 Mic input' - Microphone data levels"
        echo "   - '🎙️ Raw VAD state' - VAD processing"
        echo "   - '🔊 VAD SPEECH DETECTED!' - Voice detection"
        echo "   - '🌐 === HTTP POST REQUEST START ===' - HTTP transmission"
        echo ""
        
        # Start monitor and filter for relevant messages
        idf.py monitor | grep --line-buffered -E "(AfeAudioProcessor|AudioService|HTTP|VAD|🎤|🎙️|🔊|🔇|🌐|📥|📤)"
    else
        echo "❌ Flash failed!"
        exit 1
    fi
else
    echo "❌ Build failed!"
    exit 1
fi