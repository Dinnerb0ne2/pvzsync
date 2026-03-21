# PVZ Sync Tool - 植物大战僵尸内网联动工具

Windows平台专属的《植物大战僵尸杂交版》（PVZ-Hybrid）局域网联动工具，实现两台内网电脑间的PVZ进程联动控制、存档自动备份、远程控制功能。

## 项目概述

本工具提供以下核心功能：
- **进程联动控制**：两台电脑间PVZ进程的同步控制
- **存档自动备份**：本地和远程存档的自动备份
- **远程控制**：屏幕捕获、输入模拟、画面传输
- **图形化界面**：现代化粉蓝色渐变主题，支持中文

## 技术栈

- **构建工具**：CMake
- **线程**：C++17 std::thread/std::mutex（保证线程安全）
- **编译环境**：MinGW-w64（64位）
- **网络协议**：TCP（仅支持内网局域网通信）
- **GUI框架**：ImGui + Win32 + OpenGL3

## 目录结构

```
pvzsync/
├── CMakeLists.txt       # 根目录CMake配置文件
├── main.cpp             # 程序入口（主函数+消息循环）
├── config.ini           # 配置文件（运行必需）
├── README.md            # 项目说明文档
├── include/             # 头文件目录（仅放模块接口声明）
│   ├── core.h           # 核心业务：配置+PVZ控制+存档备份+进程管理
│   ├── network.h        # 网络模块：TCP通信+异步连接
│   ├── remote.h         # 远程控制模块：屏幕捕获+输入模拟+画面传输
│   └── ui.h             # UI模块：Win32窗口+ImGui渲染+消息系统
├── src/                 # 源文件目录（仅放模块实现）
│   ├── core.cpp
│   ├── network.cpp
│   ├── remote.cpp
│   └── ui.cpp
└── imgui/               # ImGui完整源码目录（包含backends子目录）
    ├── imgui.cpp
    ├── imgui_draw.cpp
    ├── imgui_tables.cpp
    ├── imgui_widgets.cpp
    ├── imgui_demo.cpp
    ├── backends/
    │   ├── imgui_impl_win32.cpp
    │   └── imgui_impl_opengl3.cpp
    └── ...
```

## 编译部署

### 环境要求

- Windows 10/11
- MinGW-w64 64位编译器
- CMake 3.18+
- ImGui源码（放在项目根目录的imgui文件夹中）

### 编译步骤

1. **准备ImGui源码**
   - 下载ImGui源码到项目根目录下的`imgui`文件夹
   - 确保包含`backends`子目录

2. **使用CMake编译**
   ```powershell
   cd build
   cmake .. -G "MinGW Makefiles"
   mingw32-make -j4
   ```

3. **运行程序**
   ```powershell
   ./pvz_tool.exe
   ```

### 运行注意事项

- 需将`config.ini`放在程序运行目录
- 确保ImGui源码目录完整
- 两台电脑需在同一内网，关闭防火墙
- 远程控制功能需要目标进程运行

## 配置说明

`config.ini`配置文件包含以下选项：

### 基础配置

- **AllowAutoOpen**：随动打开PVZ开关（0/1）
- **Role**：角色（server/client）
- **PeerIP**：对方内网IP地址
- **PeerPort**：通信端口（默认8888）
- **AddressFamily**：地址类型（Auto/IPv4/IPv6）

### 路径配置

- **PVZPath**：PVZ可执行文件路径
- **SaveFilePath**：PVZ存档路径
- **LocalBackupPath**：本地备份路径
- **RemoteBackupPath**：远程备份路径

### 远程控制配置

- **Resolution**：远程控制分辨率（540p/720p/1080p）
- **Framerate**：远程控制帧率（25fps/30fps/45fps/60fps）
- **TargetProcess**：目标进程名（用于远程控制）

## 核心功能

### 1. 配置管理模块

- 配置读写（使用Windows INI API）
- PVZ进程控制（检测、启动）
- 存档备份（带时间戳）
- 进程管理（关闭指定进程）

### 2. 网络通信模块

- TCP通信（支持IPv4/IPv6）
- 异步连接（避免阻塞UI）
- 命令处理（启动PVZ、备份、关闭等）
- 线程安全发送

### 3. 远程控制模块

- 屏幕捕获（GDI+）
- JPEG压缩/解压
- 输入模拟（鼠标、键盘）
- 画面传输（基于TCP）

### 4. UI渲染模块

- Win32窗口管理
- OpenGL渲染
- ImGui界面
- 消息系统
- DPI感知

## 代码规范

1. 全局变量在头文件声明、源文件定义
2. 使用std::mutex保证线程安全
3. 动态资源（线程/套接字/窗口/GDI+）确保释放
4. 字符串拷贝使用strncpy，避免越界
5. 支持中文显示（UI字体加载）

## 项目特色

1. **模块化设计**：core、network、remote、ui四大模块独立
2. **图形化界面**：现代化粉蓝色渐变主题，支持中文
3. **完整错误处理**：所有Win32 API调用都有错误检查
4. **线程安全**：网络发送使用互斥锁保护
5. **远程控制**：支持屏幕捕获、输入模拟、画面传输
6. **实时消息**：消息栏实时显示操作状态

## 许可证

本项目仅供学习和个人使用。