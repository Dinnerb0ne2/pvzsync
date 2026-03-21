#include <network.h>
#include <core.h>
#include <remote.h>
#include <ui.h>

#include <iostream>
#include <thread>
#include <mutex>
#include <chrono>

// 全局网络状态
NetworkState g_network_state;
std::mutex g_network_mutex;
static bool g_is_connecting = false;  // 是否正在连接
static std::thread g_heartbeat_thread;  // 心跳线程
static std::thread g_reconnect_thread;  // 重连线程
static std::atomic<bool> g_heartbeat_running{false};  // 心跳线程运行标志
static std::atomic<bool> g_reconnect_running{false};  // 重连线程运行标志
static std::string g_reconnect_ip;  // 重连IP
static int g_reconnect_port = 0;  // 重连端口
static bool g_reconnect_is_server = false;  // 重连是否为服务端
static AddressFamily g_reconnect_addr_family = AddressFamily::Auto;  // 重连地址类型

// 初始化Winsock
bool InitWinsock() {
    WSADATA wsaData = {0};
    int ret = WSAStartup(MAKEWORD(2, 2), &wsaData);
    
    if (ret != 0) {
        std::cerr << "Winsock初始化失败: " << ret << std::endl;
        return false;
    }

    return true;
}

// 清理Winsock
void CleanupWinsock() {
    // 停止心跳和重连线程
    g_heartbeat_running = false;
    g_reconnect_running = false;
    g_network_state.should_reconnect = false;
    
    if (g_heartbeat_thread.joinable()) {
        g_heartbeat_thread.join();
    }
    if (g_reconnect_thread.joinable()) {
        g_reconnect_thread.join();
    }
    
    if (g_network_state.sock != INVALID_SOCKET) {
        closesocket(g_network_state.sock);
        g_network_state.sock = INVALID_SOCKET;
    }
    WSACleanup();
    g_network_state.connected = false;
}

// 启动服务端
bool StartServer(int port, AddressFamily addr_family) {
    if (!InitWinsock()) return false;

    // 确定使用的地址族
    int af = AF_INET;  // 默认IPv4
    if (addr_family == AddressFamily::IPv6) {
        af = AF_INET6;
    }
    g_network_state.address_family = af;

    // 创建套接字
    g_network_state.sock = socket(af, SOCK_STREAM, IPPROTO_TCP);
    if (g_network_state.sock == INVALID_SOCKET) {
        std::cerr << "创建套接字失败: " << WSAGetLastError() << std::endl;
        CleanupWinsock();
        return false;
    }

    // 设置SO_REUSEADDR选项
    int opt = 1;
    setsockopt(g_network_state.sock, SOL_SOCKET, SO_REUSEADDR, (char*)&opt, sizeof(opt));

    // 绑定端口
    if (af == AF_INET) {
        sockaddr_in server_addr = {0};
        server_addr.sin_family = AF_INET;
        server_addr.sin_addr.s_addr = INADDR_ANY;
        server_addr.sin_port = htons(port);

        if (bind(g_network_state.sock, (sockaddr*)&server_addr, sizeof(server_addr)) == SOCKET_ERROR) {
            std::cerr << "绑定端口失败: " << WSAGetLastError() << std::endl;
            CleanupWinsock();
            return false;
        }
    } else if (af == AF_INET6) {
        sockaddr_in6 server_addr = {0};
        server_addr.sin6_family = AF_INET6;
        server_addr.sin6_addr = in6addr_any;
        server_addr.sin6_port = htons(port);

        if (bind(g_network_state.sock, (sockaddr*)&server_addr, sizeof(server_addr)) == SOCKET_ERROR) {
            std::cerr << "绑定端口失败: " << WSAGetLastError() << std::endl;
            CleanupWinsock();
            return false;
        }
    }

    // 监听
    if (listen(g_network_state.sock, 1) == SOCKET_ERROR) {
        std::cerr << "监听失败: " << WSAGetLastError() << std::endl;
        CleanupWinsock();
        return false;
    }

    std::cout << "服务端监听端口: " << port << " ("
              << (af == AF_INET6 ? "IPv6" : "IPv4") << ")" << std::endl;

    // 接受连接
    char client_ip_str[INET6_ADDRSTRLEN] = {0};
    SOCKET client_sock = INVALID_SOCKET;

    if (af == AF_INET) {
        sockaddr_in client_addr = {0};
        int addr_len = sizeof(client_addr);
        client_sock = accept(g_network_state.sock, (sockaddr*)&client_addr, &addr_len);
        
        if (client_sock != INVALID_SOCKET) {
            inet_ntop(AF_INET, &client_addr.sin_addr, client_ip_str, INET_ADDRSTRLEN);
        }
    } else if (af == AF_INET6) {
        sockaddr_in6 client_addr = {0};
        int addr_len = sizeof(client_addr);
        client_sock = accept(g_network_state.sock, (sockaddr*)&client_addr, &addr_len);
        
        if (client_sock != INVALID_SOCKET) {
            inet_ntop(AF_INET6, &client_addr.sin6_addr, client_ip_str, INET6_ADDRSTRLEN);
        }
    }
    
    if (client_sock == INVALID_SOCKET) {
        std::cerr << "接受连接失败: " << WSAGetLastError() << std::endl;
        CleanupWinsock();
        return false;
    }

    // 关闭监听套接字，使用客户端套接字
    closesocket(g_network_state.sock);
    g_network_state.sock = client_sock;
    g_network_state.connected = true;

    std::cout << "客户端连接: " << client_ip_str << std::endl;
    AddMessage("客户端已连接: " + std::string(client_ip_str), MessageType::Success);

    // 初始化心跳时间
    g_network_state.last_heartbeat = std::chrono::steady_clock::now();

    // 启动接收线程
    std::thread recv_thread(NetworkRecvThreadFunc);
    recv_thread.detach();

    // 启动心跳线程
    StartHeartbeatThread();

    return true;
}

// 连接服务端
bool ConnectToServer(const std::string& ip, int port, AddressFamily addr_family) {
    if (!InitWinsock()) return false;

    // 确定使用的地址族
    int af = AF_INET;  // 默认IPv4
    if (addr_family == AddressFamily::IPv6 || IsIPv6Address(ip)) {
        af = AF_INET6;
    }
    g_network_state.address_family = af;

    // 创建套接字
    g_network_state.sock = socket(af, SOCK_STREAM, IPPROTO_TCP);
    if (g_network_state.sock == INVALID_SOCKET) {
        std::cerr << "创建套接字失败: " << WSAGetLastError() << std::endl;
        CleanupWinsock();
        return false;
    }

    // 连接
    bool connect_success = false;
    if (af == AF_INET) {
        sockaddr_in server_addr = {0};
        server_addr.sin_family = AF_INET;
        inet_pton(AF_INET, ip.c_str(), &server_addr.sin_addr);
        server_addr.sin_port = htons(port);

        connect_success = (connect(g_network_state.sock, (sockaddr*)&server_addr, sizeof(server_addr)) != SOCKET_ERROR);
    } else if (af == AF_INET6) {
        sockaddr_in6 server_addr = {0};
        server_addr.sin6_family = AF_INET6;
        inet_pton(AF_INET6, ip.c_str(), &server_addr.sin6_addr);
        server_addr.sin6_port = htons(port);

        connect_success = (connect(g_network_state.sock, (sockaddr*)&server_addr, sizeof(server_addr)) != SOCKET_ERROR);
    }

    if (!connect_success) {
        std::cerr << "连接失败: " << WSAGetLastError() << std::endl;
        CleanupWinsock();
        return false;
    }

    g_network_state.connected = true;
    std::cout << "已连接到服务端: " << ip << ":" << port << " ("
              << (af == AF_INET6 ? "IPv6" : "IPv4") << ")" << std::endl;
    AddMessage("已连接到服务端: " + ip + ":" + std::to_string(port), MessageType::Success);

    // 初始化心跳时间
    g_network_state.last_heartbeat = std::chrono::steady_clock::now();

    // 启动接收线程
    std::thread recv_thread(NetworkRecvThreadFunc);
    recv_thread.detach();

    // 启动心跳线程
    StartHeartbeatThread();

    return true;
}

// 接收线程
void NetworkRecvThreadFunc() {
    char buf[4096] = {0};
    while (g_network_state.connected) {
        // 如果远程控制正在运行，跳过数据处理（由RemoteDisplayThreadFunc处理）
        if (g_remote_state.streaming) {
            Sleep(10);
            continue;
        }

        int recv_len = recv(g_network_state.sock, buf, sizeof(buf)-1, 0);
        
        if (recv_len <= 0) {
            std::cerr << "连接断开: " << WSAGetLastError() << std::endl;
            g_network_state.connected = false;
            AddMessage("连接已断开", MessageType::Error);
            
            // 启动重连机制
            if (g_network_state.should_reconnect) {
                StartReconnectThread(g_reconnect_ip, g_reconnect_port, g_reconnect_is_server, g_reconnect_addr_family);
            }
            
            CleanupWinsock();
            break;
        }

        buf[recv_len] = '\0';
        std::string cmd = buf;
        std::cout << "收到命令: " << cmd << std::endl;

        // 更新心跳时间
        g_network_state.last_heartbeat = std::chrono::steady_clock::now();

        // 处理命令
        if (cmd == "HEARTBEAT") {
            // 心跳包，不处理
            continue;
        } else if (cmd == "START_PVZ" && g_config.allow_auto_open) {
            StartPVZ(g_config.pvz_path);
        } else if (cmd == "BACKUP_LOCAL") {
            BackupSaveDir(g_config.save_path, g_config.local_backup_path);
        } else if (cmd == "BACKUP_REMOTE") {
            BackupSaveDir(g_config.save_path, g_config.remote_backup_path);
        } else if (cmd == "CLOSE_SELF") {
            CloseApp();
        } else if (cmd == "CLOSE_TARGET") {
            if (!g_config.target_process.empty()) {
                if (CloseProcessByName(g_config.target_process)) {
                    AddMessage("远程指令关闭目标进程: " + g_config.target_process, MessageType::Success);
                }
            }
        } else if (cmd == "CLOSE_BOTH") {
            // 关闭自己和目标进程
            CloseSelfAndTarget();
        }
    }
}

// 发送命令
void SendCommand(const std::string& cmd) {
    std::lock_guard<std::mutex> lock(g_network_mutex);
    if (!g_network_state.connected) return;

    send(g_network_state.sock, cmd.c_str(), cmd.size(), 0);
}

// 异步连接函数
void AsyncConnectTask(const std::string& ip, int port, bool is_server, AddressFamily addr_family) {
    g_is_connecting = true;
    SetLoadingState(LoadingState::Connecting, "正在连接到 " + ip + ":" + std::to_string(port));
    
    bool success = false;
    if (is_server) {
        success = StartServer(port, addr_family);
    } else {
        success = ConnectToServer(ip, port, addr_family);
    }
    
    if (!success) {
        AddMessage("连接失败，请检查网络设置", MessageType::Error);
    }
    
    g_is_connecting = false;
    SetLoadingState(LoadingState::None, "");
}

// 开始异步连接
void StartAsyncConnect(const std::string& ip, int port, bool is_server, AddressFamily addr_family) {
    if (g_is_connecting || g_network_state.connected) {
        return;
    }
    
    std::thread connect_thread(AsyncConnectTask, ip, port, is_server, addr_family);
    connect_thread.detach();
}

// 检查是否正在连接
bool IsConnecting() {
    return g_is_connecting;
}

// 检查是否为IPv6地址
bool IsIPv6Address(const std::string& ip) {
    return ip.find(':') != std::string::npos;
}

// 解析地址类型字符串
AddressFamily ParseAddressFamily(const std::string& addr_family_str) {
    if (addr_family_str == "ipv6" || addr_family_str == "IPv6") {
        return AddressFamily::IPv6;
    } else if (addr_family_str == "ipv4" || addr_family_str == "IPv4") {
        return AddressFamily::IPv4;
    }
    return AddressFamily::Auto;
}

// 获取地址类型字符串
std::string GetAddressFamilyString(AddressFamily addr_family) {
    switch (addr_family) {
        case AddressFamily::IPv4:
            return "IPv4";
        case AddressFamily::IPv6:
            return "IPv6";
        case AddressFamily::Auto:
        default:
            return "Auto";
    }
}

// 心跳线程函数
static void HeartbeatThreadFunc() {
    while (g_heartbeat_running && g_network_state.connected) {
        SendHeartbeat();
        std::this_thread::sleep_for(std::chrono::seconds(10));  // 每10秒发送一次心跳
    }
}

// 启动心跳线程
void StartHeartbeatThread() {
    if (g_heartbeat_running) {
        return;  // 已经在运行
    }
    
    g_heartbeat_running = true;
    g_heartbeat_thread = std::thread(HeartbeatThreadFunc);
    g_heartbeat_thread.detach();
}

// 发送心跳包
void SendHeartbeat() {
    if (!g_network_state.connected) {
        return;
    }
    
    std::lock_guard<std::mutex> lock(g_network_mutex);
    const char* heartbeat = "HEARTBEAT";
    send(g_network_state.sock, heartbeat, strlen(heartbeat), 0);
}

// 检查连接超时
bool CheckConnectionTimeout(int timeout_seconds) {
    auto now = std::chrono::steady_clock::now();
    auto elapsed = std::chrono::duration_cast<std::chrono::seconds>(now - g_network_state.last_heartbeat);
    return elapsed.count() > timeout_seconds;
}

// 重连线程函数
static void ReconnectThreadFunc() {
    while (g_reconnect_running && g_network_state.should_reconnect) {
        if (!g_network_state.connected && !g_is_connecting) {
            AddMessage("尝试重新连接...", MessageType::Info);
            
            bool success = false;
            if (g_reconnect_is_server) {
                success = StartServer(g_reconnect_port, g_reconnect_addr_family);
            } else {
                success = ConnectToServer(g_reconnect_ip, g_reconnect_port, g_reconnect_addr_family);
            }
            
            if (success) {
                AddMessage("重连成功", MessageType::Success);
                g_network_state.should_reconnect = false;
                break;
            } else {
                AddMessage("重连失败，5秒后重试", MessageType::Warning);
            }
        }
        
        std::this_thread::sleep_for(std::chrono::seconds(5));  // 每5秒重试一次
    }
}

// 启动重连线程
void StartReconnectThread(const std::string& ip, int port, bool is_server, AddressFamily addr_family) {
    if (g_reconnect_running) {
        return;  // 已经在运行
    }
    
    g_reconnect_ip = ip;
    g_reconnect_port = port;
    g_reconnect_is_server = is_server;
    g_reconnect_addr_family = addr_family;
    g_network_state.should_reconnect = true;
    
    g_reconnect_running = true;
    g_reconnect_thread = std::thread(ReconnectThreadFunc);
    g_reconnect_thread.detach();
}