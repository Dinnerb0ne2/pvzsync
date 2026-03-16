#include "ui.h"
#include "core.h"
#include "network.h"
#include "remote.h"
#include "imgui_impl_win32.h"
#include "imgui_impl_opengl3.h"

#include <string.h>
#include <iostream>
#include <shlwapi.h>
#include <vector>
#include <mutex>
#include <thread>

// 前向声明ImGui_ImplWin32_WndProcHandler（从imgui_impl_win32.h复制）
extern IMGUI_IMPL_API LRESULT ImGui_ImplWin32_WndProcHandler(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam);

// 全局窗口状态
WindowState g_window_state;

// 消息系统
std::vector<MessageItem> g_messages;
const int MAX_MESSAGES = 50;  // 最多保存50条消息

// 远程控制画面纹理
static GLuint g_remote_texture = 0;
uint8_t* g_remote_frame_buffer = nullptr;
int g_remote_frame_width = 1280;
int g_remote_frame_height = 720;
static std::mutex g_remote_frame_mutex;

// DPI感知函数声明
typedef HRESULT(WINAPI* SetProcessDpiAwarenessProc)(int PROCESS_DPI_AWARENESS);

// 启用DPI感知
void EnableDPIAwareness() {
    HMODULE shcore = LoadLibraryA("shcore.dll");
    if (shcore) {
        SetProcessDpiAwarenessProc set_process_dpi_awareness = 
            (SetProcessDpiAwarenessProc)GetProcAddress(shcore, "SetProcessDpiAwareness");
        if (set_process_dpi_awareness) {
            set_process_dpi_awareness(2); // PROCESS_PER_MONITOR_DPI_AWARE
        }
        FreeLibrary(shcore);
    }
}

// 窗口过程函数
LRESULT CALLBACK WndProc(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam) {
    if (ImGui_ImplWin32_WndProcHandler(hWnd, msg, wParam, lParam)) {
        return true;
    }

    switch (msg) {
    case WM_CLOSE:
        g_window_state.running = false;
        DestroyWindow(hWnd);
        return 0;
    case WM_DESTROY:
        PostQuitMessage(0);
        return 0;
    default:
        return DefWindowProc(hWnd, msg, wParam, lParam);
    }
}

// 创建Win32+OpenGL窗口
bool CreateWin32OpenGLWindow() {
    // 启用DPI感知，解决UI模糊问题
    EnableDPIAwareness();

    // 注册窗口类（使用Unicode版本）
    WNDCLASSW wc = {0};
    wc.style         = CS_CLASSDC;
    wc.lpfnWndProc   = WndProc;
    wc.hInstance     = GetModuleHandle(NULL);
    wc.lpszClassName = L"PVZSyncClass";

    if (!RegisterClassW(&wc)) {
        std::cerr << "窗口类注册失败: " << GetLastError() << std::endl;
        return false;
    }

    // 创建窗口（使用Unicode版本解决标题中文乱码）
    g_window_state.hwnd = CreateWindowExW(
        0,
        wc.lpszClassName,
        L"PVZ内网联动工具",
        WS_OVERLAPPEDWINDOW,
        100, 100, 1050, 900,
        NULL, NULL, wc.hInstance, NULL
    );

    if (!g_window_state.hwnd) {
        std::cerr << "窗口创建失败: " << GetLastError() << std::endl;
        return false;
    }

    // 初始化OpenGL
    PIXELFORMATDESCRIPTOR pfd = {0};
    pfd.nSize      = sizeof(PIXELFORMATDESCRIPTOR);
    pfd.nVersion   = 1;
    pfd.dwFlags    = PFD_DRAW_TO_WINDOW | PFD_SUPPORT_OPENGL | PFD_DOUBLEBUFFER;
    pfd.iPixelType = PFD_TYPE_RGBA;
    pfd.cColorBits = 32;
    pfd.cDepthBits = 24;
    pfd.cStencilBits = 8;

    g_window_state.hdc = GetDC(g_window_state.hwnd);
    int pf = ChoosePixelFormat(g_window_state.hdc, &pfd);
    SetPixelFormat(g_window_state.hdc, pf, &pfd);

    g_window_state.hglrc = wglCreateContext(g_window_state.hdc);
    wglMakeCurrent(g_window_state.hdc, g_window_state.hglrc);

    // 显示窗口
    ShowWindow(g_window_state.hwnd, SW_SHOWDEFAULT);
    UpdateWindow(g_window_state.hwnd);

    // 初始化ImGui
    IMGUI_CHECKVERSION();
    ImGui::CreateContext();
    ImGuiIO& io = ImGui::GetIO();
    io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard;
    io.FontGlobalScale = 1.0f;  // 字体全局缩放

    // 检测Windows主题并自适应
    HKEY hKey;
    DWORD value = 0;
    DWORD size = sizeof(DWORD);
    bool is_dark_theme = false;
    
    if (RegOpenKeyExA(HKEY_CURRENT_USER, 
                      "Software\\Microsoft\\Windows\\CurrentVersion\\Themes\\Personalize", 
                      0, KEY_READ, &hKey) == ERROR_SUCCESS) {
        if (RegQueryValueExA(hKey, "AppsUseLightTheme", NULL, NULL, (LPBYTE)&value, &size) == ERROR_SUCCESS) {
            is_dark_theme = (value == 0);  // 0表示暗色主题
        }
        RegCloseKey(hKey);
    }
    
    // 应用主题样式
    ImGuiStyle& style = ImGui::GetStyle();
    if (is_dark_theme) {
        ImGui::StyleColorsDark();
    } else {
        ImGui::StyleColorsLight();
    }
    
    // 调整样式使其更现代
    style.WindowRounding = 8.0f;
    style.FrameRounding = 4.0f;
    style.GrabRounding = 4.0f;
    style.ScrollbarRounding = 4.0f;
    style.PopupRounding = 4.0f;
    style.WindowPadding = ImVec2(10, 10);
    style.ItemSpacing = ImVec2(8, 6);
    style.FramePadding = ImVec2(8, 6);
    
    ImGui_ImplWin32_Init(g_window_state.hwnd);
    ImGui_ImplOpenGL3_Init("#version 130");

    // 加载中文字体（微软雅黑），增加Oversample参数提高清晰度
    const char* font_path = "C:\\Windows\\Fonts\\msyh.ttf";
    ImFontConfig font_config;
    font_config.OversampleH = 3;      // 提高水平过采样
    font_config.OversampleV = 3;      // 提高垂直过采样
    font_config.PixelSnapH = false;
    font_config.RasterizerMultiply = 1.5f;  // 增加栅格化倍数
    
    // 检查字体文件是否存在
    if (GetFileAttributesA(font_path) != INVALID_FILE_ATTRIBUTES) {
        io.Fonts->AddFontFromFileTTF(font_path, 18.0f, &font_config, io.Fonts->GetGlyphRangesChineseFull());
    } else {
        // 如果微软雅黑不存在，尝试使用宋体
        const char* font_path_fallback = "C:\\Windows\\Fonts\\simsun.ttc";
        if (GetFileAttributesA(font_path_fallback) != INVALID_FILE_ATTRIBUTES) {
            io.Fonts->AddFontFromFileTTF(font_path_fallback, 18.0f, &font_config, io.Fonts->GetGlyphRangesChineseFull());
        }
    }

    // 初始化远程控制画面纹理
    glGenTextures(1, &g_remote_texture);
    glBindTexture(GL_TEXTURE_2D, g_remote_texture);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP);
    
    // 分配画面缓冲区
    g_remote_frame_buffer = new uint8_t[g_remote_frame_width * g_remote_frame_height * 4];
    
    // 初始化黑色画面
    memset(g_remote_frame_buffer, 0, g_remote_frame_width * g_remote_frame_height * 4);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, g_remote_frame_width, g_remote_frame_height, 
                 0, GL_RGBA, GL_UNSIGNED_BYTE, g_remote_frame_buffer);

    return true;
}

// 销毁窗口
void DestroyWin32OpenGLWindow() {
    // 清理远程控制纹理和缓冲区
    if (g_remote_texture) {
        glDeleteTextures(1, &g_remote_texture);
        g_remote_texture = 0;
    }
    if (g_remote_frame_buffer) {
        delete[] g_remote_frame_buffer;
        g_remote_frame_buffer = nullptr;
    }

    // 清理ImGui
    ImGui_ImplOpenGL3_Shutdown();
    ImGui_ImplWin32_Shutdown();
    ImGui::DestroyContext();

    // 清理OpenGL
    wglMakeCurrent(NULL, NULL);
    wglDeleteContext(g_window_state.hglrc);
    ReleaseDC(g_window_state.hwnd, g_window_state.hdc);

    // 销毁窗口
    DestroyWindow(g_window_state.hwnd);
    UnregisterClassW(L"PVZSyncClass", GetModuleHandle(NULL));
}

// 更新GUI状态
void UpdateGUIState() {
    CheckPVZRunning();
}

// 渲染GUI
void RenderGUI() {
    // 开始ImGui帧
    ImGui_ImplOpenGL3_NewFrame();
    ImGui_ImplWin32_NewFrame();
    ImGui::NewFrame();

    // 设置窗口
    ImGui::SetNextWindowPos(ImVec2(10, 10));
    ImGui::SetNextWindowSize(ImVec2(1000, 850));
    ImGui::Begin("PVZ 内网联动工具", nullptr, ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoMove);

    // 基础配置
    if (ImGui::CollapsingHeader("基础配置 [General]", ImGuiTreeNodeFlags_DefaultOpen)) {
        ImGui::Checkbox("随动打开PVZ", &g_config.allow_auto_open);
        
        char role_buf[32] = {0};
        strncpy(role_buf, g_config.role.c_str(), sizeof(role_buf)-1);
        ImGui::InputText("本机角色 (server/client)", role_buf, sizeof(role_buf));
        g_config.role = role_buf;

        char peer_ip_buf[32] = {0};
        strncpy(peer_ip_buf, g_config.peer_ip.c_str(), sizeof(peer_ip_buf)-1);
        ImGui::InputText("对方IP", peer_ip_buf, sizeof(peer_ip_buf));
        g_config.peer_ip = peer_ip_buf;

        ImGui::InputInt("通信端口", &g_config.peer_port);
    }

    // 路径配置
    if (ImGui::CollapsingHeader("路径配置", ImGuiTreeNodeFlags_DefaultOpen)) {
        char pvz_path_buf[512] = {0};
        strncpy(pvz_path_buf, g_config.pvz_path.c_str(), sizeof(pvz_path_buf)-1);
        ImGui::InputText("PVZ程序路径", pvz_path_buf, sizeof(pvz_path_buf));
        g_config.pvz_path = pvz_path_buf;

        char save_path_buf[512] = {0};
        strncpy(save_path_buf, g_config.save_path.c_str(), sizeof(save_path_buf)-1);
        ImGui::InputText("存档文件夹路径", save_path_buf, sizeof(save_path_buf));
        g_config.save_path = save_path_buf;

        char local_backup_buf[512] = {0};
        strncpy(local_backup_buf, g_config.local_backup_path.c_str(), sizeof(local_backup_buf)-1);
        ImGui::InputText("本地备份路径", local_backup_buf, sizeof(local_backup_buf));
        g_config.local_backup_path = local_backup_buf;

        char remote_backup_buf[512] = {0};
        strncpy(remote_backup_buf, g_config.remote_backup_path.c_str(), sizeof(remote_backup_buf)-1);
        ImGui::InputText("远程备份路径", remote_backup_buf, sizeof(remote_backup_buf));
        g_config.remote_backup_path = remote_backup_buf;
    }

    // 运行状态
    if (ImGui::CollapsingHeader("运行状态", ImGuiTreeNodeFlags_DefaultOpen)) {
        ImGui::Text("连接状态: %s", g_network_state.connected ? "✅ 已连接" : "❌ 未连接");
        ImGui::Text("PVZ状态: %s", g_pvz_state.is_running ? "▶️ 运行中" : "⏹️ 未运行");
        
        if (PathIsDirectoryA(g_config.save_path.c_str())) {
            time_t latest_time = GetLatestFileTimeInDir(g_config.save_path);
            ImGui::Text("存档最新修改时间: %s", latest_time > 0 ? ctime(&latest_time) : "未知");
        }
    }

    // 远程控制
    if (ImGui::CollapsingHeader("远程控制", ImGuiTreeNodeFlags_DefaultOpen)) {
        ImGui::Text("角色: %s", g_config.role == "server" ? "服务端（被控）" : "客户端（控制）");
        ImGui::Text("传输状态: %s", g_remote_state.streaming ? "🔴 传输中" : "⚫ 未传输");
        ImGui::Text("配置: %s @ %s (已导入)", g_config.resolution.c_str(), g_config.framerate.c_str());
        
        if (ImGui::Button(g_remote_state.streaming ? "停止传输" : "开始传输", ImVec2(100, 30))) {
            if (!g_remote_state.streaming) {
                // 开始传输
                if (g_network_state.connected) {
                    g_remote_state.streaming = true;
                    g_remote_state.is_server = (g_config.role == "server");
                    g_remote_state.is_client = (g_config.role == "client");
                    
                    if (g_remote_state.is_server) {
                        // 启动抓屏线程
                        std::thread capture_thread(RemoteCaptureThreadFunc);
                        capture_thread.detach();
                    } else {
                        // 应用配置并启动显示线程
                        ApplyRemoteConfig();
                        std::thread display_thread(RemoteDisplayThreadFunc);
                        display_thread.detach();
                    }
                }
            } else {
                // 停止传输
                g_remote_state.streaming = false;
            }
        }

        ImGui::Spacing();
        if (ImGui::Button("关联到exe进程", ImVec2(120, 30))) {
            if (!g_config.target_process.empty()) {
                if (CloseProcessByName(g_config.target_process)) {
                    AddMessage("已关闭目标进程: " + g_config.target_process, MessageType::Success);
                    Sleep(500);
                    StartPVZ(g_config.pvz_path);
                    AddMessage("已重新启动目标进程", MessageType::Success);
                } else {
                    AddMessage("未找到目标进程，正在启动...", MessageType::Info);
                    StartPVZ(g_config.pvz_path);
                }
            }
        }

        ImGui::SameLine();
        if (ImGui::Button("手动备份", ImVec2(100, 30))) {
            BackupSaveDir(g_config.save_path, g_config.local_backup_path);
            if (g_network_state.connected) {
                SendCommand("BACKUP_REMOTE");
            }
        }

        ImGui::Spacing();
        ImGui::Separator();
        ImGui::Spacing();

        ImGui::Text("一键关闭功能:");
        if (ImGui::Button("关闭自己+PVZ", ImVec2(120, 30))) {
            CloseSelfAndTarget();
        }

        ImGui::SameLine();
        if (ImGui::Button("关闭双方", ImVec2(100, 30))) {
            CloseSelfAndTarget();
            if (g_network_state.connected) {
                SendCommand("CLOSE_BOTH");
            }
        }

        ImGui::SameLine();
        if (ImGui::Button("关闭对方", ImVec2(100, 30))) {
            if (g_network_state.connected) {
                SendCommand("CLOSE_SELF");
            }
        }
        
        // 显示画面（客户端）
        if (g_remote_state.is_client && g_remote_texture) {
            std::lock_guard<std::mutex> lock(g_remote_frame_mutex);
            
            // 更新纹理
            glBindTexture(GL_TEXTURE_2D, g_remote_texture);
            glTexSubImage2D(GL_TEXTURE_2D, 0, 0, 0, 
                           g_remote_frame_width, g_remote_frame_height,
                           GL_RGBA, GL_UNSIGNED_BYTE, g_remote_frame_buffer);
            
            // 显示画面
            float aspect = (float)g_remote_frame_width / g_remote_frame_height;
            float display_height = 360.0f;
            float display_width = display_height * aspect;
            
            ImGui::Image((ImTextureID)(intptr_t)g_remote_texture, 
                        ImVec2(display_width, display_height));
            
            // 捕获鼠标点击并转发
                                if (ImGui::IsItemHovered()) {
                                    ImVec2 mouse_pos = ImGui::GetMousePos();
                                    ImVec2 item_pos = ImGui::GetItemRectMin();
                                    ImVec2 item_size = ImGui::GetItemRectSize();
                                    
                                    float rel_x = (mouse_pos.x - item_pos.x) / item_size.x;
                                    float rel_y = (mouse_pos.y - item_pos.y) / item_size.y;
                                    
                                    if (rel_x >= 0.0f && rel_x <= 1.0f && rel_y >= 0.0f && rel_y <= 1.0f) {
                                        MouseEvent event;
                                        event.x = (int16_t)(rel_x * g_remote_frame_width);
                                        event.y = (int16_t)(rel_y * g_remote_frame_height);
                                        event.buttons = 0;
                                        event.wheel = 0;
                                        
                                        if (ImGui::IsMouseClicked(0)) event.buttons |= 0x01;  // 左键
                                        if (ImGui::IsMouseClicked(1)) event.buttons |= 0x02;  // 右键
                                        if (ImGui::IsMouseClicked(2)) event.buttons |= 0x04;  // 中键
                                        
                                        // 发送鼠标事件
                                        PacketHeader header;
                                        header.type = (uint32_t)PacketType::PACKET_MOUSE_EVENT;
                                        header.size = sizeof(MouseEvent);
                                        header.frame_id = 0;
                                        header.reserved = 0;
                                        
                                        send(g_network_state.sock, (char*)&header, sizeof(header), 0);
                                        send(g_network_state.sock, (char*)&event, sizeof(event), 0);
                                    }
                                }        }
    }

    // 操作按钮
    ImGui::Spacing();
    if (ImGui::Button("保存配置到INI", ImVec2(120, 30))) {
        SaveConfig("config.ini");
    }
    ImGui::SameLine();
    if (ImGui::Button("从INI加载配置", ImVec2(120, 30))) {
        ReadConfig("config.ini");
    }

    ImGui::SameLine();
    if (ImGui::Button("连接对方", ImVec2(100, 30)) && !g_network_state.connected && !IsConnecting()) {
        StartAsyncConnect(g_config.peer_ip, g_config.peer_port, g_config.role == "server");
        AddMessage("正在连接，请稍候...", MessageType::Info);
    } else if (IsConnecting()) {
        ImGui::SameLine();
        ImGui::TextColored(ImVec4(1.0f, 0.8f, 0.2f, 1.0f), " (连接中...)");
    }

    ImGui::SameLine();
    if (ImGui::Button("启动PVZ", ImVec2(100, 30))) {
        StartPVZ(g_config.pvz_path);
        if (g_network_state.connected) {
            SendCommand("START_PVZ");
        }
    }

    ImGui::Spacing();
    if (ImGui::Button("备份存档到本地", ImVec2(150, 30))) {
        BackupSaveDir(g_config.save_path, g_config.local_backup_path);
    }
    ImGui::SameLine();
    if (ImGui::Button("发送远程备份指令", ImVec2(150, 30)) && g_network_state.connected) {
        SendCommand("BACKUP_REMOTE");
    }

    // 渲染消息栏（在最上方）
    RenderMessageBar();

    // 结束GUI
    ImGui::End();
    ImGui::Render();
    ImGui_ImplOpenGL3_RenderDrawData(ImGui::GetDrawData());
    SwapBuffers(g_window_state.hdc);
}

// 添加消息
void AddMessage(const std::string& text, MessageType type) {
    MessageItem item;
    item.text = text;
    item.type = type;
    item.timestamp = ImGui::GetTime();
    
    // 如果消息数量超过限制，删除最旧的消息
    if (g_messages.size() >= MAX_MESSAGES) {
        g_messages.erase(g_messages.begin());
    }
    
    g_messages.push_back(item);
}

// 渲染消息栏
void RenderMessageBar() {
    // 获取主窗口大小，自适应位置
    ImVec2 main_window_size = ImGui::GetWindowSize();
    ImVec2 main_window_pos = ImGui::GetWindowPos();
    
    // 消息栏位于主窗口底部
    float msg_bar_height = 60.0f;
    float msg_bar_width = main_window_size.x - 40.0f;  // 留20px边距
    float msg_bar_x = main_window_pos.x + 20.0f;
    float msg_bar_y = main_window_pos.y + main_window_size.y - msg_bar_height - 20.0f;
    
    ImGui::SetNextWindowPos(ImVec2(msg_bar_x, msg_bar_y));
    ImGui::SetNextWindowSize(ImVec2(msg_bar_width, msg_bar_height));
    ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, 3.0f);
    ImGui::PushStyleColor(ImGuiCol_WindowBg, ImVec4(0.1f, 0.1f, 0.1f, 0.9f));  // 半透明背景
    
    if (ImGui::Begin("##MessageBar", nullptr, 
                     ImGuiWindowFlags_NoTitleBar | 
                     ImGuiWindowFlags_NoResize | 
                     ImGuiWindowFlags_NoMove | 
                     ImGuiWindowFlags_NoScrollbar | 
                     ImGuiWindowFlags_NoCollapse)) {
        // 显示最近的消息（最多2条）
        int show_count = std::min((int)g_messages.size(), 2);
        for (int i = 0; i < show_count; i++) {
            const MessageItem& msg = g_messages[g_messages.size() - 1 - i];
            
            // 根据消息类型设置颜色
            ImVec4 color;
            const char* prefix = "";
            switch (msg.type) {
                case MessageType::Info:
                    color = ImVec4(0.6f, 0.6f, 0.6f, 1.0f);  // 灰色
                    prefix = "[信息] ";
                    break;
                case MessageType::Success:
                    color = ImVec4(0.3f, 0.8f, 0.3f, 1.0f);  // 绿色
                    prefix = "[成功] ";
                    break;
                case MessageType::Warning:
                    color = ImVec4(1.0f, 0.7f, 0.2f, 1.0f);  // 黄色
                    prefix = "[警告] ";
                    break;
                case MessageType::Error:
                    color = ImVec4(1.0f, 0.3f, 0.3f, 1.0f);  // 红色
                    prefix = "[错误] ";
                    break;
            }
            
            // 显示消息
            ImGui::TextColored(color, "%s%s", prefix, msg.text.c_str());
        }
        
        if (g_messages.empty()) {
            ImGui::TextColored(ImVec4(0.4f, 0.4f, 0.4f, 1.0f), "暂无消息");
        }
    }
    
    ImGui::PopStyleColor();
    ImGui::PopStyleVar();
    ImGui::End();
}