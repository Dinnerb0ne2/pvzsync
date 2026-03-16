#pragma once
#include <string>
#include <winsock2.h>

// 网络状态
struct NetworkState {
    SOCKET sock = INVALID_SOCKET;
    bool connected = false;
};

// 全局网络状态
extern NetworkState g_network_state;

// Winsock初始化/清理
bool InitWinsock();
void CleanupWinsock();

// 服务端/客户端
bool StartServer(int port);
bool ConnectToServer(const std::string& ip, int port);

// 网络线程/命令
void NetworkRecvThreadFunc();
void SendCommand(const std::string& cmd);