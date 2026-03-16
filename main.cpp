#include <winsock2.h>
#include <windows.h>
#include <iostream>
#include "core.h"
#include "network.h"
#include "ui.h"

/**
 * @brief 程序主入口函数
 * @param hInstance 应用程序实例句柄
 * @param hPrevInstance 前一个实例句柄（Win32中始终为NULL）
 * @param lpCmdLine 命令行参数字符串
 * @param nCmdShow 窗口显示状态
 * @return 消息循环退出时的返回值
 */
int WINAPI WinMain(HINSTANCE hInstance, HINSTANCE hPrevInstance, LPSTR lpCmdLine, int nCmdShow) {
    // 配置文件路径
    std::string config_path = "config.ini";

    // 检查配置文件是否存在
    if (GetFileAttributesA(config_path.c_str()) != INVALID_FILE_ATTRIBUTES) {
        // 配置文件存在，加载配置
        ReadConfig(config_path);
    } else {
        // 配置文件不存在，创建默认配置并保存
        SaveConfig(config_path);
    }

    // 创建Win32+OpenGL窗口
    if (!CreateWin32OpenGLWindow()) {
        return 1;
    }

    // 消息循环
    MSG msg;
    while (g_window_state.running) {
        // 处理Windows消息
        while (PeekMessage(&msg, NULL, 0, 0, PM_REMOVE)) {
            TranslateMessage(&msg);
            DispatchMessage(&msg);
        }

        // 更新GUI状态
        UpdateGUIState();

        // 渲染GUI
        RenderGUI();

        // 控制帧率（约60FPS）
        Sleep(16);
    }

    // 资源清理
    // 清理Winsock资源
    CleanupWinsock();
    
    // 销毁窗口资源
    DestroyWin32OpenGLWindow();
    
    // 保存配置
    SaveConfig(config_path);
    
    return 0;
}