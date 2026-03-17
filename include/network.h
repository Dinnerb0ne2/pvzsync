#pragma once
#include <string>
#include <winsock2.h>
#include <ws2tcpip.h>

// 地址类型枚举
enum class AddressFamily {
    Auto,       // 自动选择（优先IPv4）
    IPv4,       // 强制使用IPv4
    IPv6        // 强制使用IPv6
};

// 网络状态
struct NetworkState {
    SOCKET sock = INVALID_SOCKET;
    bool connected = false;
    int address_family = AF_INET;  // AF_INET(IPv4) 或 AF_INET6(IPv6)
};

// 全局网络状态
extern NetworkState g_network_state;

// Winsock初始化/清理
bool InitWinsock();
void CleanupWinsock();

// 服务端/客户端
bool StartServer(int port, AddressFamily addr_family = AddressFamily::Auto);
bool ConnectToServer(const std::string& ip, int port, AddressFamily addr_family = AddressFamily::Auto);

// 网络线程/命令
void NetworkRecvThreadFunc();
void SendCommand(const std::string& cmd);

// 异步连接
void StartAsyncConnect(const std::string& ip, int port, bool is_server, AddressFamily addr_family = AddressFamily::Auto);
bool IsConnecting();

// 工具函数
bool IsIPv6Address(const std::string& ip);
AddressFamily ParseAddressFamily(const std::string& addr_family_str);
std::string GetAddressFamilyString(AddressFamily addr_family);