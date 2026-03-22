# HA-CAM: 智能家居摄像头项目

基于 ESP32 的智能家居摄像头项目，支持实时视频流、RTSP 服务、Web 管理界面、MQTT 集成（Home Assistant）等功能。

## 功能特性

### 核心功能
- **实时视频流**: 支持 MJPEG 格式视频流，可通过 Web 界面查看
- **RTSP 服务**: 提供 RTSP 视频流服务，兼容主流播放器和 NVR 系统
- **图像抓拍**: 支持单张图像捕获和保存
- **视频录制**: 支持 AVI 格式视频录制到本地存储

### Web 管理界面
- **视频监控**: 实时查看摄像头画面
- **相机配置**:
  - 分辨率调整（支持多种帧尺寸）
  - 图像质量（JPEG Quality: 10-63）
  - 亮度（Brightness: -2 to 2）
  - 对比度（Contrast: 0 to 2）
  - 饱和度（Saturation: -2 to 2）
  - 锐度（Sharpness: -2 to 2）
  - 自动曝光（AEC）及 AE 等级
  - 自动增益（AGC）
  - 自动白平衡（AWB）及 WB 模式
  - 图像镜像（H-Mirror）
  - 图像翻转（V-Flip）
  - 特殊效果（Special Effects）
  - 去噪（Denoise: 0-8）
  - 镜头校正（Lens Correction）
  - 动态去噪（Dynamic Denoise）
- **文件管理**: 浏览、下载和管理存储的图片和视频
- **OTA 更新**: 支持通过 Web 界面进行固件更新

### MQTT 集成
- 支持 Home Assistant MQTT 自动发现
- 实时状态上报
- 远程控制接口

### 硬件支持
- **麦克风**: 支持音频采集
- **存储**: SD 卡支持，用于视频录制和图像存储

## 项目结构

```
ha_cam/
├── components/           # 组件目录
│   ├── Camera/          # 相机驱动和控制
│   ├── EasyRTSPServer/  # RTSP 流媒体服务器
│   ├── WebServer/       # Web 服务器和界面
│   ├── ha_mqtt_client/  # MQTT 客户端（Home Assistant）
│   ├── storage/         # 存储管理（AVI 录制）
│   ├── ChipInfo/        # 芯片信息获取
│   ├── Utils/           # 通用工具函数
│   └── Mic/             # 麦克风支持
├── main/               # 主程序
│   ├── app_main.c      # 主程序入口
│   ├── CMakeLists.txt  # 构建配置
│   └── Kconfig.projbuild # 项目配置菜单
└── README.md
```

## 编译与烧录

### 环境要求
- ESP-IDF v5.0 或更高版本
- 支持的 ESP32 芯片：ESP32、ESP32-S2、ESP32-S3 等

### 配置步骤

1. **配置 WiFi 连接**

   打开项目配置菜单：
   ```bash
   idf.py menuconfig
   ```

   在 `Example Configuration` 菜单中设置：
   - 设置 `WiFi SSID`：你的 WiFi 网络名称
   - 设置 `WiFi Password`：你的 WiFi 密码
   - 可选：调整其他 WiFi 参数（如 WPA3 模式、最大重试次数等）

2. **编译项目**

   ```bash
   idf.py build
   ```

3. **烧录固件**

   ```bash
   idf.py -p COMX flash monitor
   ```

   （请将 `COMX` 替换为实际的串口号）

## 使用说明

### Web 界面访问

1. 连接设备到 WiFi 网络
2. 在串口监视器中查看设备的 IP 地址
3. 在浏览器中访问 `http://<设备IP地址>`

### 主要功能操作

#### 启动/停止视频流
- 点击 "Start Stream" 按钮启动实时视频流
- 点击 "Stop Stream" 按钮停止视频流

#### 抓拍图片
- 点击 "Capture Image" 按钮抓拍单张图片
- 可以保存当前画面到本地

#### 配置相机
- 在左侧配置面板调整各项参数
- 点击 "Load Config" 加载当前配置
- 点击 "Save Config" 保存配置

#### 文件管理
- 点击 "File Manager" 进入文件管理界面
- 可以浏览、下载存储的图片和视频

#### OTA 更新
- 点击 "OTA Update" 进入 OTA 更新界面
- 上传新的固件文件进行更新

### RTSP 服务

设备默认提供 RTSP 服务，RTSP 地址格式：
```
rtsp://<设备IP地址>:8554/live
```

可以使用 VLC、ffplay、Home Assistant 等播放器或软件查看视频流。

### MQTT 集成

设备支持 Home Assistant MQTT 自动发现，配置 MQTT 参数后，设备将自动在 Home Assistant 中显示摄像头实体。

## 配置选项

### 相机配置（可通过 Web 界面或代码配置）

| 参数 | 范围 | 说明 |
|------|------|------|
| Resolution | 多种选项 | 视频分辨率 |
| Quality | 10-63 | JPEG 质量值（越小质量越高） |
| Brightness | -2 to 2 | 亮度调整 |
| Contrast | 0 to 2 | 对比度调整 |
| Saturation | -2 to 2 | 饱和度调整 |
| Sharpness | -2 to 2 | 锐度调整 |
| AEC | On/Off | 自动曝光控制 |
| AE Level | 0-5 | 自动曝光等级 |
| AGC | On/Off | 自动增益控制 |
| AWB | On/Off | 自动白平衡 |
| WB Mode | Auto/Sunny/Cloudy/Tungsten/Fluorescent | 白平衡模式 |
| H-Mirror | On/Off | 水平镜像 |
| V-Flip | On/Off | 垂直翻转 |
| Special Effect | None/Negative/Grayscale/Red/Green/Blue/Sepia | 特殊效果 |
| Denoise | 0-8 | 去噪强度 |
| Lens Correction | On/Off | 镜头校正 |
| Dynamic Denoise | On/Off | 动态去噪 |

## 故障排除

### WiFi 连接失败
- 检查 SSID 和密码是否正确
- 确认 WiFi 信号强度
- 检查路由器的安全设置

### 视频流无法显示
- 确认设备已连接到网络
- 检查浏览器是否支持 MJPEG 流
- 尝试使用不同的浏览器

### RTSP 连接失败
- 确认 RTSP 服务器端口（默认 8554）未被占用
- 检查防火墙设置
- 确认使用正确的 RTSP 地址格式

## 依赖组件

本项目依赖于以下 ESP-IDF 组件和第三方库：

- **ESP-IDF 核心**: esp_wifi, nvs_flash, esp_psram, mqtt
- **EasyRTSPServer**: RTSP 流媒体服务器实现
- **第三方依赖**: 在 `dependencies.lock` 中列出

## 许可证

本项目采用开源许可证，详见源文件中的许可声明。

## 贡献

欢迎提交 Issue 和 Pull Request 来改进这个项目。

## 更新日志

### 最近更新
- 扩展图像配置参数并重构 JSON 结构
- 优化 Web 界面与配置 API
- 添加 OTA 更新与文件管理功能
- 动态获取相机帧尺寸

## 技术支持

如有问题或建议，请通过 GitHub Issues 提交。
