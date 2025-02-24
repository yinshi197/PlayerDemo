# 🎥 基于 FFmpeg 、SDL 和 QT 的多媒体播放器

![License](https://img.shields.io/badge/License-MIT-blue.svg)
![FFmpeg](https://img.shields.io/badge/FFmpeg-7.0-green.svg)
![SDL](https://img.shields.io/badge/SDL-2.30.6-orange.svg)
![QT](https://img.shields.io/badge/QT-6.8.1-purple.svg)
![CMake](https://img.shields.io/badge/CMake-3.29.3-red.svg)

这是一个基于 `ffplay.c` 源码开发的多媒体播放器，支持多种视频和音频格式，具有优美的播放界面和丰富的功能。项目使用 `FFmpeg 7.0`、`SDL 2.30.6` 和 `QT 6.8.1` 构建，开发环境为 `VSCode`，构建工具为 `CMake 3.29.3`。

本项目参考自(十分感谢🙏)：[itisyang/playerdemo: 一个视频播放器，开源版 potplayer ，用于总结播放器开发技术。](https://github.com/itisyang/playerdemo)

不需要图形界面希望直接调试ffplay.c源码可以参考我的另一个项目：[基于 FFmpeg ffplay.c 的移植项目，无需编译 FFmpeg 源码，只需修改库路径即可开始调试！ffplay-debug-helper](https://github.com/yinshi197/ffplay-debug-helper)

## 🚀 功能特性

- **🎨 播放界面优美**：基于 QT 的现代化播放界面。
- **📋 播放列表**：支持播放列表功能，可自动存储播放列表。
- **🖱️ 拖放支持**：可以将文件直接拖放到播放窗口进行播放。
- **⏳ 进度控制**：包含进度条、音量调节、视频总时间和当前播放时间显示。
- **🎛️ 播放控制**：支持播放、暂停、下一曲、上一曲等功能。
- **📂 多种格式支持**：支持多种视频和音频格式，同时还支持网络流。
- **🖼️ 专辑图显示**：如果音频文件包含专辑图，可以显示专辑图。
- **⏭️ 自动播放下一曲**：支持自动播放下一曲功能。

## 🎬 演示视频

观看项目的演示视频，请访问以下链接：  
[Bilibili 演示视频](https://www.bilibili.com/video/BV1dGPWeoEKE)  


## ⌨️ 快捷键

| 快捷键          | 功能描述               |
| --------------- | ---------------------- |
| `ESC`, `Enter`  | 切换全屏播放           |
| `SPC`           | 暂停/播放              |
| `m`             | 切换静音               |
| `Up`            | 音量增加 10%           |
| `Down`          | 音量减少 10%           |
| `a`             | 切换当前节目中的音频流 |
| `v`             | 切换视频流             |
| `t`             | 切换当前节目中的字幕流 |
| `c`             | 切换节目（切换全部流） |
| `w`             | 切换显示模式或视频滤镜 |
| `s`             | 激活逐帧播放模式       |
| `Left`, `Right` | 快退/快进 5 秒         |

## 🛠️ 构建与运行

### 依赖项

确保已安装以下依赖项：

- FFmpeg 7.0
- SDL 2.30.6
- QT 6.8.1
- CMake 3.29.3

FFmpeg和SDL库可以下载仓库提供的压缩包

### 构建步骤

1. 克隆仓库：

   ```bash
   git clone https://github.com/yinshi197/Simple-Qt-FFplayer.git
   cd Simple-Qt-FFplayer

2. 创建构建目录并构建项目：

   ```bash
   mkdir build
   cd build
   cmake ..
   make
   ```

3. 修改CMakeLists.txt中的依赖库位置

   ```cmake
   set(FFMPEG_PATH D://FFmpeg//ffmpeg-7.0-fdk_aac)			#更换为你的FFmpeg库路径
   set(SDL_PATH D://FFmpeg//SDL-release-2.30.6//build64)	#更换为你的SDL库路径
   ```

4. 运行播放器：

   ```bash
   ./player
   ```

## ❓ 项目当前存在的问题

- **性能优化**：某些高分辨率视频播放时可能会出现卡顿现象。
- **存在Bug**：播放视频过程中放大或者缩小窗口会卡顿，视频画面不会刷新，后续有时间会去解决。
- **字幕支持**：部分字幕格式的解析和显示尚未完全支持。
- **跨平台兼容性**：目前主要在 Windows 上测试，Linux和 macOS 上的兼容性需要进一步验证。

## 📅 项目后续希望补充的功能

- **优化控件布局**：优化视频显示窗口的控件布局，解决视频播放过程中调整大小卡顿的问题。

- **优化播放列表**：播放列表显示视频封面图，区分视频、音频和网络流。
- **增加缩略图**：鼠标悬停在进度条上面显示当前帧视频缩略图
- **增加倍数播放**：添加倍数播放功能，使用sonic API实现倍数播放不变音。

## 🤝 贡献

欢迎贡献代码！请遵循以下步骤：

1. Fork 项目
2. 创建你的分支 (`git checkout -b feature/YourFeatureName`)
3. 提交你的更改 (`git commit -m 'Add some feature'`)
4. 推送到分支 (`git push origin feature/YourFeatureName`)
5. 创建一个 Pull Request

## 🙏 致谢

- 感谢 [FFmpeg](https://ffmpeg.org/) 提供了强大的多媒体处理库。
- 感谢 [SDL](https://www.libsdl.org/) 提供了跨平台的多媒体支持。
- 感谢 [QT](https://www.qt.io/) 提供了现代化的 GUI 框架。
- 感谢以下 GitHub 项目的参考与启发：
  - [FFmpeg/FFmpeg](https://github.com/FFmpeg/FFmpeg)
  - [SDL-mirror/SDL](https://github.com/SDL-mirror/SDL)
  - [itisyang/playerdemo: 一个视频播放器，开源版 potplayer ，用于总结播放器开发技术。](https://github.com/itisyang/playerdemo)

## 📜 许可证

本项目采用 [MIT 许可证](https://cloud.siliconflow.cn/playground/chat/LICENSE)。

## 📧 联系

如有任何问题或建议，请通过 [GitHub Issues](https://github.com/yourusername/your-repo-name/issues) 联系我。