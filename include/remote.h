#pragma once
#include <cstdint>
#include <string>
#include <windows.h>

// 数据包类型
enum class PacketType : uint32_t {
    FRAME_DATA = 1,        // 画面帧数据
    PACKET_MOUSE_EVENT = 2,       // 鼠标事件
    KEYBOARD_EVENT = 3,    // 键盘事件
    CONTROL_CMD = 4        // 控制命令
};

// 数据包头（固定16字节）
#pragma pack(push, 1)
struct PacketHeader {
    uint32_t type;         // 数据包类型
    uint32_t size;         // 数据大小（不含头）
    uint32_t frame_id;     // 帧ID（用于帧同步）
    uint32_t reserved;     // 保留字段
};
#pragma pack(pop)

// 鼠标事件
#pragma pack(push, 1)
struct MouseEvent {
    int16_t x;             // 鼠标X坐标（相对720p画面）
    int16_t y;             // 鼠标Y坐标（相对720p画面）
    uint8_t buttons;       // 按键状态（bit0:左键, bit1:右键, bit2:中键）
    uint8_t wheel;         // 滚轮值（-127~127）
};
#pragma pack(pop)

// 键盘事件
#pragma pack(push, 1)
struct KeyboardEvent {
    uint16_t vk_code;      // 虚拟键码
    uint8_t is_pressed;    // 是否按下（1=按下, 0=释放）
    uint8_t reserved[5];   // 保留字段
};
#pragma pack(pop)

// 画面帧信息
#pragma pack(push, 1)
struct FrameInfo {
    uint32_t width;        // 画面宽度（1280）
    uint32_t height;       // 画面高度（720）
    uint32_t format;       // 格式（1=JPEG）
    uint32_t quality;      // JPEG质量（1-100）
};
#pragma pack(pop)

// 远程控制状态
struct RemoteState {
    bool is_server = false;        // 是否为服务端（被控端）
    bool is_client = false;        // 是否为客户端（控制端）
    bool streaming = false;        // 是否正在传输
    int target_fps = 25;           // 目标帧率
    int capture_width = 1280;      // 抓屏宽度（720p）
    int capture_height = 720;      // 抓屏高度（720p）
    int jpeg_quality = 75;         // JPEG压缩质量
    HWND target_window = nullptr;  // 目标窗口（PVZ窗口）
};

// 全局远程控制状态
extern RemoteState g_remote_state;

// 屏幕捕获相关
bool InitScreenCapture();
void CleanupScreenCapture();
bool CaptureScreen(uint8_t* buffer, size_t buffer_size, size_t* captured_size);

// 输入模拟相关
void SimulateMouseEvent(const MouseEvent& event);
void SimulateKeyboardEvent(const KeyboardEvent& event);

// 图像压缩相关
bool CompressJPEG(const uint8_t* src, int width, int height, int quality, 
                  uint8_t* dst, size_t dst_size, size_t* compressed_size);
bool DecompressJPEG(const uint8_t* src, size_t src_size, 
                    uint8_t* dst, int* width, int* height);

// 远程控制线程
void RemoteCaptureThreadFunc();   // 服务端：抓屏并传输
void RemoteDisplayThreadFunc();   // 客户端：接收并显示画面

// 工具函数
std::string FindPVZWindow();      // 查找PVZ窗口句柄
bool ParseResolution(const std::string& res, int* width, int* height);  // 解析分辨率
int ParseFramerate(const std::string& fps);  // 解析帧率
void ApplyRemoteConfig();         // 应用远程控制配置