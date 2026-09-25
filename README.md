# androidNativeSurfaceImgui

一个轻量、高性能的 Android native悬浮图层ImGui运行框架。

包含ImGui标准平台后端，nativewindow的创建，vk渲染

---

## 核心特性

- **surface创建**：通过反射 Android Java 系统隐藏 API（`SurfaceControl`），向 `SurfaceFlinger` 申请硬件 Overlay 图层，支持透明混合与全局顶置，比传统native直接调用系统so库更稳定，可以跨版本运行。
- **Vulkan**：纯原生 Vulkan WSI 交换链桥接与双缓冲同步，自适应屏幕旋转重建（`VK_ERROR_OUT_OF_DATE_KHR`）。
- **触摸输入**：直接读取 Linux 内核 `/dev/input/event*` 节点，解析原始触摸输入，自带旋转坐标仿射映射。

---

## 环境准备与依赖

在编译之前，请确保本地开发环境已安装以下组件：

1. **Android NDK**：推荐 NDK r25 及以上版本。
2. **Android SDK**：需包含 `build-tools`（提供 `d8` 打包工具）以及 `platforms/android-*/android.jar`。
3. **JDK (Java Development Kit)**：JDK 11 或 JDK 17，确保命令行可执行 `javac` 和 `jar`（需配置进系统环境变量 `PATH`）。
4. **CMake**：3.18.1 及以上。
5. **Ninja**。
6. **运行设备**：Android 10+（需Root或者adb权限）。

---

## 构建配置与编译

### 1. 修改本地路径配置
打开根目录的 [`CMakeLists.txt`](CMakeLists.txt)，将开头的 SDK 和 NDK 路径修改为本机的实际路径：

```cmake
# 1. 设置NDK路径
set(ANDROID_NDK "C:/Android/Sdk/ndk/27.3.13750724")

# 2. 设置SDK根路径
set(ANDROID_SDK_ROOT "C:/Android/Sdk")
```

> **注意**：
> - 请确保您的系统环境变量中能够直接调用 `javac` 和 `jar`，构建系统会自动调用它们将 Java 模块打包为 `classes.dex`。

### 2. 执行编译


```bash
# 生成构建目录并指定 Ninja
cmake -B build -G Ninja -DCMAKE_BUILD_TYPE=Release

# 开始编译
cmake --build build
```

编译成功后，将生成两个核心产物：
- `libandroidNativeSurfaceImgui.so`（底层核心动态库）
- `classes.dex`（Java 隐藏层与启动桩模块）

---

## 运行与部署

### 1. 推送到测试设备
通过 ADB 将编译产物推送到手机的 `/data/local/tmp` 目录：

```bash
adb push <编译输出目录>/libandroidNativeSurfaceImgui.so /data/local/tmp/
adb push <编译输出目录>/classes.dex /data/local/tmp/
```

### 2. 命令行启动
通过 `app_process` 运行应用：

```bash
adb shell
su # (可选，无Root设备跳过此行)
cd /data/local/tmp
export LD_LIBRARY_PATH=/data/local/tmp:$LD_LIBRARY_PATH
export CLASSPATH=/data/local/tmp/classes.dex
app_process -Djava.library.path=/data/local/tmp /data/local/tmp com.nativesurface.app.Entry [参数...]
```

### 3. 可用命令行参数

| 参数 | 说明 | 默认值 |
| :--- | :--- | :--- |
| `--name <str>` | 悬浮图层调试名称 | `NativeSurfaceOverlay` |
| `--width <n>` | 渲染缓冲区宽度（像素） | 屏幕实际物理宽 |
| `--height <n>` | 渲染缓冲区高度（像素） | 屏幕实际物理高 |
| `--no-trusted` | 跳过设置 `setTrustedOverlay` 信任属性 | 默认开启 |
| `--no-watch` | 禁用屏幕旋转与尺寸动态监听 | 默认开启监听 |
| `--watch-ms <n>` | 屏幕状态轮询监听间隔（毫秒） | `250` |
| `--once [ms]` | 运行指定毫秒后自动退出（常用于自动化测试） | 默认持续运行，传参默认 `3000` |
| `-h, --help` | 显示命令行帮助信息 | - |

---

## 开源协议

本项目采用 [MIT License](LICENSE) 协议开源。
