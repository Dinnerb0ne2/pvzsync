#include "remote.h"
#include "core.h"
#include <ui.h>
#include <winsock2.h>
#include <windows.h>
#include <psapi.h>
#include <gdiplus.h>
#include <iostream>
#include <thread>
#include <mutex>
#include <vector>
#include "network.h"

#pragma comment(lib, "gdiplus.lib")
#pragma comment(lib, "psapi.lib")

// 全局变量
RemoteState g_remote_state;
std::mutex g_remote_mutex;
static ULONG_PTR g_gdiplus_token = 0;

// 外部变量声明
extern uint8_t* g_remote_frame_buffer;
extern int g_remote_frame_width;
extern int g_remote_frame_height;

// GDI+初始化
static bool InitGDIPlus() {
    Gdiplus::GdiplusStartupInput startup_input;
    if (Gdiplus::GdiplusStartup(&g_gdiplus_token, &startup_input, NULL) != Gdiplus::Ok) {
        std::cerr << "GDI+初始化失败" << std::endl;
        return false;
    }
    return true;
}

// 获取编码器CLSID（前向声明）
static int GetEncoderCLSID(const WCHAR* format, CLSID* pClsid);

// GDI+清理
static void CleanupGDIPlus() {
    if (g_gdiplus_token) {
        Gdiplus::GdiplusShutdown(g_gdiplus_token);
        g_gdiplus_token = 0;
    }
}

// 屏幕捕获相关
static HDC g_screen_dc = nullptr;
static HDC g_capture_dc = nullptr;
static HBITMAP g_capture_bitmap = nullptr;
static uint8_t* g_capture_buffer = nullptr;
static size_t g_capture_buffer_size = 0;

// 查找PVZ窗口
std::string FindPVZWindow() {
    HWND hwnd = FindWindowA(NULL, "Plants vs. Zombies");
    if (hwnd) {
        // 返回窗口句柄作为字符串
        char buf[32];
        sprintf_s(buf, "%p", hwnd);
        return std::string(buf);
    }
    return "";
}

// 初始化屏幕捕获
bool InitScreenCapture() {
    // 获取屏幕DC
    g_screen_dc = GetDC(NULL);
    if (!g_screen_dc) {
        std::cerr << "获取屏幕DC失败" << std::endl;
        return false;
    }

    // 创建兼容DC
    g_capture_dc = CreateCompatibleDC(g_screen_dc);
    if (!g_capture_dc) {
        std::cerr << "创建兼容DC失败" << std::endl;
        CleanupScreenCapture();
        return false;
    }

    // 创建位图（720p）
    g_capture_bitmap = CreateCompatibleBitmap(
        g_screen_dc, 
        g_remote_state.capture_width, 
        g_remote_state.capture_height
    );
    if (!g_capture_bitmap) {
        std::cerr << "创建位图失败" << std::endl;
        CleanupScreenCapture();
        return false;
    }

    // 选择位图到DC
    SelectObject(g_capture_dc, g_capture_bitmap);

    // 分配缓冲区（BGRA格式，4字节/像素）
    g_capture_buffer_size = g_remote_state.capture_width * g_remote_state.capture_height * 4;
    g_capture_buffer = new uint8_t[g_capture_buffer_size];
    if (!g_capture_buffer) {
        std::cerr << "分配捕获缓冲区失败" << std::endl;
        CleanupScreenCapture();
        return false;
    }

    std::cout << "屏幕捕获初始化成功 (" 
              << g_remote_state.capture_width << "x" 
              << g_remote_state.capture_height << ")" << std::endl;

    return true;
}

// 清理屏幕捕获
void CleanupScreenCapture() {
    if (g_capture_buffer) {
        delete[] g_capture_buffer;
        g_capture_buffer = nullptr;
    }

    if (g_capture_bitmap) {
        DeleteObject(g_capture_bitmap);
        g_capture_bitmap = nullptr;
    }

    if (g_capture_dc) {
        DeleteDC(g_capture_dc);
        g_capture_dc = nullptr;
    }

    if (g_screen_dc) {
        ReleaseDC(NULL, g_screen_dc);
        g_screen_dc = nullptr;
    }
}

// 捕获屏幕
bool CaptureScreen(uint8_t* buffer, size_t buffer_size, size_t* captured_size) {
    if (!g_capture_dc || !g_capture_buffer) {
        return false;
    }

    // 拷贝屏幕到位图
    HWND target_hwnd = g_remote_state.target_window;
    RECT rect = {0};
    
    if (target_hwnd) {
        // 捕获特定窗口
        GetWindowRect(target_hwnd, &rect);
        int src_width = rect.right - rect.left;
        int src_height = rect.bottom - rect.top;
        
        StretchBlt(
            g_capture_dc, 0, 0, 
            g_remote_state.capture_width, g_remote_state.capture_height,
            g_screen_dc, rect.left, rect.top, src_width, src_height,
            SRCCOPY
        );
    } else {
        // 捕获整个屏幕
        StretchBlt(
            g_capture_dc, 0, 0, 
            g_remote_state.capture_width, g_remote_state.capture_height,
            g_screen_dc, 0, 0, 
            GetSystemMetrics(SM_CXSCREEN), GetSystemMetrics(SM_CYSCREEN),
            SRCCOPY
        );
    }

    // 获取位图数据（BGRA格式）
    BITMAPINFO bmi = {0};
    bmi.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
    bmi.bmiHeader.biWidth = g_remote_state.capture_width;
    bmi.bmiHeader.biHeight = -g_remote_state.capture_height;  // 负值表示从上到下
    bmi.bmiHeader.biPlanes = 1;
    bmi.bmiHeader.biBitCount = 32;
    bmi.bmiHeader.biCompression = BI_RGB;

    int scan_lines = GetDIBits(
        g_capture_dc, g_capture_bitmap, 
        0, g_remote_state.capture_height,
        g_capture_buffer, &bmi, DIB_RGB_COLORS
    );

    if (scan_lines == 0) {
        std::cerr << "获取位图数据失败: " << GetLastError() << std::endl;
        return false;
    }

    // 转换BGRA到RGBA
    for (size_t i = 0; i < g_capture_buffer_size; i += 4) {
        std::swap(g_capture_buffer[i], g_capture_buffer[i + 2]);
    }

    // 压缩为JPEG
    if (!CompressJPEG(g_capture_buffer, g_remote_state.capture_width, 
                      g_remote_state.capture_height, g_remote_state.jpeg_quality,
                      buffer, buffer_size, captured_size)) {
        std::cerr << "JPEG压缩失败" << std::endl;
        return false;
    }

    return true;
}

// 模拟鼠标事件
void SimulateMouseEvent(const MouseEvent& event) {
    INPUT input = {0};
    input.type = INPUT_MOUSE;

    // 计算屏幕坐标（根据720p比例）
    int screen_width = GetSystemMetrics(SM_CXSCREEN);
    int screen_height = GetSystemMetrics(SM_CYSCREEN);
    
    float scale_x = (float)screen_width / g_remote_state.capture_width;
    float scale_y = (float)screen_height / g_remote_state.capture_height;

    int screen_x = (int)(event.x * scale_x);
    int screen_y = (int)(event.y * scale_y);

    // 设置鼠标位置
    input.mi.dx = screen_x * (65535 / screen_width);
    input.mi.dy = screen_y * (65535 / screen_height);
    input.mi.dwFlags = MOUSEEVENTF_ABSOLUTE | MOUSEEVENTF_MOVE;

    SendInput(1, &input, sizeof(INPUT));

    // 处理鼠标按键
    if (event.buttons & 0x01) {  // 左键
        input.mi.dwFlags = MOUSEEVENTF_LEFTDOWN;
        SendInput(1, &input, sizeof(INPUT));
    } else {
        input.mi.dwFlags = MOUSEEVENTF_LEFTUP;
        SendInput(1, &input, sizeof(INPUT));
    }

    if (event.buttons & 0x02) {  // 右键
        input.mi.dwFlags = MOUSEEVENTF_RIGHTDOWN;
        SendInput(1, &input, sizeof(INPUT));
    } else {
        input.mi.dwFlags = MOUSEEVENTF_RIGHTUP;
        SendInput(1, &input, sizeof(INPUT));
    }

    // 处理滚轮
    if (event.wheel != 0) {
        input.mi.dwFlags = MOUSEEVENTF_WHEEL;
        input.mi.mouseData = event.wheel * WHEEL_DELTA;
        SendInput(1, &input, sizeof(INPUT));
    }
}

// 模拟键盘事件
void SimulateKeyboardEvent(const KeyboardEvent& event) {
    INPUT input = {0};
    input.type = INPUT_KEYBOARD;
    input.ki.wVk = event.vk_code;
    input.ki.dwFlags = event.is_pressed ? 0 : KEYEVENTF_KEYUP;

    SendInput(1, &input, sizeof(INPUT));
}

// 压缩JPEG（使用GDI+）
bool CompressJPEG(const uint8_t* src, int width, int height, int quality, 
                  uint8_t* dst, size_t dst_size, size_t* compressed_size) {
    // 创建Bitmap
    Gdiplus::Bitmap bitmap(width, height, width * 4, PixelFormat32bppARGB, (BYTE*)src);
    
    // 设置JPEG编码参数
    CLSID encoder_clsid;
    if (GetEncoderCLSID(L"image/jpeg", &encoder_clsid) != 0) {
        return false;
    }

    Gdiplus::EncoderParameters encoder_params;
    encoder_params.Count = 1;
    encoder_params.Parameter[0].Guid = Gdiplus::EncoderQuality;
    encoder_params.Parameter[0].Type = Gdiplus::EncoderParameterValueTypeLong;
    encoder_params.Parameter[0].NumberOfValues = 1;
    encoder_params.Parameter[0].Value = &quality;

    // 创建IStream
    IStream* stream = nullptr;
    if (CreateStreamOnHGlobal(NULL, TRUE, &stream) != S_OK) {
        return false;
    }

    // 保存为JPEG
    Gdiplus::Status status = bitmap.Save(stream, &encoder_clsid, &encoder_params);
    
    if (status == Gdiplus::Ok) {
        // 获取JPEG数据
        STATSTG stat = {0};
        stream->Stat(&stat, STATFLAG_NONAME);
        
        LARGE_INTEGER pos = {0};
        stream->Seek(pos, STREAM_SEEK_SET, NULL);
        
        ULONG bytes_read = 0;
        if (stream->Read(dst, (ULONG)dst_size, &bytes_read) == S_OK) {
            *compressed_size = bytes_read;
            stream->Release();
            return true;
        }
    }

    stream->Release();
    return false;
}

// 获取编码器CLSID
static int GetEncoderCLSID(const WCHAR* format, CLSID* pClsid) {
    UINT num = 0;
    UINT size = 0;
    
    Gdiplus::GetImageEncodersSize(&num, &size);
    if (size == 0) return -1;
    
    std::vector<BYTE> buffer(size);
    Gdiplus::ImageCodecInfo* pImageCodecInfo = (Gdiplus::ImageCodecInfo*)buffer.data();
    
    Gdiplus::GetImageEncoders(num, size, pImageCodecInfo);
    
    for (UINT j = 0; j < num; ++j) {
        if (wcscmp(pImageCodecInfo[j].MimeType, format) == 0) {
            *pClsid = pImageCodecInfo[j].Clsid;
            return 0;
        }
    }
    
    return -1;
}

// 解压JPEG（客户端显示用，使用GDI+）
bool DecompressJPEG(const uint8_t* src, size_t src_size, 
                    uint8_t* dst, int* width, int* height) {
    // 创建IStream
    IStream* stream = nullptr;
    HGLOBAL hGlobal = GlobalAlloc(GMEM_MOVEABLE, src_size);
    if (!hGlobal) return false;
    
    void* pGlobal = GlobalLock(hGlobal);
    memcpy(pGlobal, src, src_size);
    GlobalUnlock(hGlobal);
    
    if (CreateStreamOnHGlobal(hGlobal, FALSE, &stream) != S_OK) {
        GlobalFree(hGlobal);
        return false;
    }

    // 加载JPEG
    Gdiplus::Bitmap bitmap(stream, FALSE);
    if (bitmap.GetLastStatus() != Gdiplus::Ok) {
        stream->Release();
        GlobalFree(hGlobal);
        return false;
    }

    *width = bitmap.GetWidth();
    *height = bitmap.GetHeight();

    // 获取位图数据
    Gdiplus::Rect rect(0, 0, *width, *height);
    Gdiplus::BitmapData bitmap_data;
    
    if (bitmap.LockBits(&rect, Gdiplus::ImageLockModeRead, PixelFormat32bppARGB, &bitmap_data) == Gdiplus::Ok) {
        // 拷贝数据
        memcpy(dst, bitmap_data.Scan0, bitmap_data.Stride * *height);
        bitmap.UnlockBits(&bitmap_data);
        
        stream->Release();
        GlobalFree(hGlobal);
        return true;
    }

    stream->Release();
    GlobalFree(hGlobal);
    return false;
}

// 远程捕获线程（服务端）
void RemoteCaptureThreadFunc() {
    std::cout << "远程捕获线程启动" << std::endl;

    // 应用远程控制配置
    ApplyRemoteConfig();

    // 初始化GDI+
    if (!InitGDIPlus()) {
        std::cerr << "GDI+初始化失败" << std::endl;
        AddMessage("GDI+初始化失败", MessageType::Error);
        return;
    }

    // 初始化屏幕捕获
    if (!InitScreenCapture()) {
        std::cerr << "屏幕捕获初始化失败" << std::endl;
        AddMessage("屏幕捕获初始化失败", MessageType::Error);
        CleanupGDIPlus();
        return;
    }

    AddMessage("远程控制传输已启动: " + g_config.resolution + " @ " + g_config.framerate, MessageType::Success);

    // 分配JPEG缓冲区（最大可能大小）
    size_t jpeg_buffer_size = g_capture_buffer_size;
    uint8_t* jpeg_buffer = nullptr;
    try {
        jpeg_buffer = new uint8_t[jpeg_buffer_size];
    } catch (const std::bad_alloc& e) {
        std::cerr << "分配JPEG缓冲区失败: " << e.what() << std::endl;
        AddMessage("内存分配失败", MessageType::Error);
        CleanupScreenCapture();
        CleanupGDIPlus();
        return;
    }

    uint32_t frame_id = 0;
    int frame_time = 1000 / g_remote_state.target_fps;
    g_remote_state.last_frame_time = std::chrono::steady_clock::now();
    g_remote_state.frames_sent = 0;
    int consecutive_low_fps = 0;  // 连续低帧率计数

    while (g_remote_state.streaming) {
        auto start_time = std::chrono::steady_clock::now();

        // 检查网络连接状态
        if (!g_network_state.connected) {
            std::cerr << "网络连接断开，停止传输" << std::endl;
            AddMessage("网络连接断开，远程控制已停止", MessageType::Error);
            break;
        }

        // 动态调整JPEG质量
        if (g_remote_state.auto_quality && g_remote_state.frames_sent > 30) {
            float current_fps = g_remote_state.fps;
            float target_fps = g_remote_state.target_fps;
            
            if (current_fps < target_fps * 0.7f) {
                // 帧率过低，降低质量
                if (g_remote_state.jpeg_quality > g_remote_state.min_quality) {
                    g_remote_state.jpeg_quality -= 5;
                    consecutive_low_fps++;
                    if (consecutive_low_fps >= 3) {
                        AddMessage("降低JPEG质量到 " + std::to_string(g_remote_state.jpeg_quality) + " 以提升帧率", MessageType::Info);
                        consecutive_low_fps = 0;
                    }
                }
            } else if (current_fps > target_fps * 1.2f) {
                // 帧率过高，提高质量
                if (g_remote_state.jpeg_quality < g_remote_state.max_quality) {
                    g_remote_state.jpeg_quality += 2;
                    consecutive_low_fps = 0;
                    if (g_remote_state.frames_sent % 60 == 0) {
                        AddMessage("提高JPEG质量到 " + std::to_string(g_remote_state.jpeg_quality) + " 以提升画质", MessageType::Info);
                    }
                }
            } else {
                consecutive_low_fps = 0;
            }
        }

        // 捕获屏幕
        size_t compressed_size = 0;
        if (CaptureScreen(jpeg_buffer, jpeg_buffer_size, &compressed_size)) {
            // 发送画面帧
            PacketHeader header;
            header.type = (uint32_t)PacketType::FRAME_DATA;
            header.size = (uint32_t)compressed_size;
            header.frame_id = frame_id++;
            header.reserved = 0;

            // 发送包头
            std::lock_guard<std::mutex> lock(g_remote_mutex);
            send(g_network_state.sock, (char*)&header, sizeof(header), 0);
            
            // 发送画面数据
            send(g_network_state.sock, (char*)jpeg_buffer, compressed_size, 0);
            
            g_remote_state.frames_sent++;
            
            // 计算实际帧率
            auto now = std::chrono::steady_clock::now();
            auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(now - g_remote_state.last_frame_time);
            if (elapsed.count() > 0) {
                g_remote_state.fps = 1000.0f / elapsed.count();
            }
            g_remote_state.last_frame_time = now;
        }

        // 控制帧率
        auto end_time = std::chrono::steady_clock::now();
        auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(end_time - start_time);
        if (elapsed.count() < frame_time) {
            std::this_thread::sleep_for(std::chrono::milliseconds(frame_time - elapsed.count()));
        }
    }

    // 清理资源
    if (jpeg_buffer) {
        delete[] jpeg_buffer;
        jpeg_buffer = nullptr;
    }
    CleanupScreenCapture();
    CleanupGDIPlus();
    
    std::cout << "远程捕获线程退出，共发送 " << g_remote_state.frames_sent << " 帧" << std::endl;
    AddMessage("远程控制传输已停止，共发送 " + std::to_string(g_remote_state.frames_sent) + " 帧", MessageType::Info);
}

// 远程显示线程（客户端）
void RemoteDisplayThreadFunc() {
    std::cout << "远程显示线程启动" << std::endl;
    AddMessage("远程控制接收已启动", MessageType::Success);

    g_remote_state.frames_received = 0;
    g_remote_state.last_frame_time = std::chrono::steady_clock::now();

    // 接收画面数据
    while (g_remote_state.streaming) {
        // 检查网络连接状态
        if (!g_network_state.connected) {
            std::cerr << "网络连接断开，停止接收" << std::endl;
            AddMessage("网络连接断开，远程控制已停止", MessageType::Error);
            break;
        }

        // 接收包头
        PacketHeader header;
        int recv_size = recv(g_network_state.sock, (char*)&header, sizeof(header), 0);
        
        if (recv_size != sizeof(header)) {
            std::cerr << "接收包头失败" << std::endl;
            AddMessage("接收数据失败，远程控制已停止", MessageType::Error);
            break;
        }

        // 处理不同类型的数据包
        if (header.type == (uint32_t)PacketType::FRAME_DATA) {
            // 接收画面数据
            uint8_t* jpeg_buffer = new uint8_t[header.size];
            recv_size = recv(g_network_state.sock, (char*)jpeg_buffer, header.size, 0);
            
            if (recv_size == header.size) {
                // 解压JPEG并更新到纹理
                int width = 0, height = 0;
                if (DecompressJPEG(jpeg_buffer, header.size, g_remote_frame_buffer, &width, &height)) {
                    g_remote_state.frames_received++;
                    
                    // 计算实际帧率
                    auto now = std::chrono::steady_clock::now();
                    auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(now - g_remote_state.last_frame_time);
                    if (elapsed.count() > 0) {
                        g_remote_state.fps = 1000.0f / elapsed.count();
                    }
                    g_remote_state.last_frame_time = now;
                }
            }
            
            delete[] jpeg_buffer;
        } else if (header.type == (uint32_t)PacketType::PACKET_MOUSE_EVENT) {
            // 接收鼠标事件（服务端处理）
            MouseEvent event;
            recv_size = recv(g_network_state.sock, (char*)&event, sizeof(event), 0);
            if (recv_size == sizeof(event)) {
                SimulateMouseEvent(event);
            }
        } else if (header.type == (uint32_t)PacketType::KEYBOARD_EVENT) {
            // 接收键盘事件（服务端处理）
            KeyboardEvent event;
            recv_size = recv(g_network_state.sock, (char*)&event, sizeof(event), 0);
            if (recv_size == sizeof(event)) {
                SimulateKeyboardEvent(event);
            }
        }
    }

    std::cout << "远程显示线程退出，共接收 " << g_remote_state.frames_received << " 帧" << std::endl;
    AddMessage("远程控制接收已停止，共接收 " + std::to_string(g_remote_state.frames_received) + " 帧", MessageType::Info);
}

// 解析分辨率字符串
bool ParseResolution(const std::string& res, int* width, int* height) {
    if (res == "540p") {
        *width = 960;
        *height = 540;
        return true;
    } else if (res == "720p") {
        *width = 1280;
        *height = 720;
        return true;
    } else if (res == "1080p") {
        *width = 1920;
        *height = 1080;
        return true;
    }
    return false;
}

// 解析帧率字符串
int ParseFramerate(const std::string& fps) {
    if (fps == "25fps") return 25;
    if (fps == "30fps") return 30;
    if (fps == "45fps") return 45;
    if (fps == "60fps") return 60;
    return 25;  // 默认值
}

// 应用远程控制配置
void ApplyRemoteConfig() {
    // 解析分辨率
    int width = 0, height = 0;
    if (ParseResolution(g_config.resolution, &width, &height)) {
        g_remote_state.capture_width = width;
        g_remote_state.capture_height = height;
    }
    
    // 解析帧率
    g_remote_state.target_fps = ParseFramerate(g_config.framerate);
    
    // 查找目标窗口
    g_remote_state.target_window = FindWindowByProcessName(g_config.target_process);
    
    std::cout << "远程控制配置已应用: " 
              << g_config.resolution << " @ " << g_config.framerate 
              << ", 目标进程: " << g_config.target_process << std::endl;
}

// 根据进程名查找窗口
HWND FindWindowByProcessName(const std::string& process_name) {
    // 遍历所有窗口
    HWND hwnd = nullptr;
    
    // 使用EnumWindows枚举所有窗口
    struct EnumData {
        std::string process_name;
        HWND found_window;
    } data = {process_name, nullptr};
    
    auto enum_windows_proc = [](HWND hwnd, LPARAM lParam) -> BOOL {
        EnumData* data = reinterpret_cast<EnumData*>(lParam);
        
        // 获取窗口进程ID
        DWORD pid = 0;
        GetWindowThreadProcessId(hwnd, &pid);
        
        // 打开进程
        HANDLE hProcess = OpenProcess(PROCESS_QUERY_INFORMATION, FALSE, pid);
        if (hProcess) {
            char process_path[MAX_PATH] = {0};
            if (GetModuleFileNameExA(hProcess, NULL, process_path, MAX_PATH)) {
                // 提取进程名
                std::string path = process_path;
                size_t pos = path.find_last_of("\\/");
                if (pos != std::string::npos) {
                    std::string current_process = path.substr(pos + 1);
                    // 比较进程名（不区分大小写）
                    if (_stricmp(current_process.c_str(), data->process_name.c_str()) == 0) {
                        data->found_window = hwnd;
                        CloseHandle(hProcess);
                        return FALSE;  // 找到了，停止枚举
                    }
                }
            }
            CloseHandle(hProcess);
        }
        
        return TRUE;  // 继续枚举
    };
    
    EnumWindows(enum_windows_proc, reinterpret_cast<LPARAM>(&data));
    
    return data.found_window;
}