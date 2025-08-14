# ESP32 小智AI开发环境
FROM ubuntu:22.04

# 设置环境变量
ENV DEBIAN_FRONTEND=noninteractive
ENV ESP_IDF_VERSION=v5.4
ENV IDF_PATH=/opt/esp-idf
ENV IDF_TOOLS_PATH=/opt/esp-idf-tools

# 安装基础依赖
RUN apt-get update && apt-get install -y \
    git \
    wget \
    flex \
    bison \
    gperf \
    python3 \
    python3-pip \
    python3-venv \
    cmake \
    ninja-build \
    ccache \
    libffi-dev \
    libssl-dev \
    dfu-util \
    libusb-1.0-0 \
    curl \
    unzip \
    && rm -rf /var/lib/apt/lists/*

# 创建用户（避免root运行）
RUN useradd -ms /bin/bash esp32dev && \
    usermod -aG dialout esp32dev && \
    mkdir -p ${IDF_PATH} ${IDF_TOOLS_PATH} && \
    chown -R esp32dev:esp32dev ${IDF_PATH} ${IDF_TOOLS_PATH}

USER esp32dev
WORKDIR /home/esp32dev

# 安装 ESP-IDF
RUN git clone --recursive --depth 1 --branch ${ESP_IDF_VERSION} \
    https://github.com/espressif/esp-idf.git ${IDF_PATH}

# 安装 ESP-IDF 工具
RUN cd ${IDF_PATH} && ./install.sh esp32s3

# 设置环境变量
RUN echo "source ${IDF_PATH}/export.sh" >> ~/.bashrc

# 工作目录
WORKDIR /workspace

# 暴露常用端口
EXPOSE 3333 8080

# 默认命令
CMD ["/bin/bash"]