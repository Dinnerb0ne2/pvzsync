#include <network.h>
#include <core.h>
#include <remote.h>
#include <ui.h>

#include <iostream>
#include <thread>
#include <mutex>

// 全局网络状态
NetworkState g_network_state;
std::mutex g_network_mutex;
static bool g_is_connecting = false;  // 是否正在连接

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
    if (g_network_state.sock != INVALID_SOCKET) {
        closesocket(g_network_state.sock);
        g_network_state.sock = INVALID_SOCKET;
    }
    WSACleanup();
    g_network_state.connected = false;
}

// 启动服务端
bool StartServer(int port) {
    if (!InitWinsock()) return false;

    // 创建套接字
    g_network_state.sock = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    if (g_network_state.sock == INVALID_SOCKET) {
        std::cerr << "创建套接字失败: " << WSAGetLastError() << std::endl;
        CleanupWinsock();
        return false;
    }

    // 绑定端口
    sockaddr_in server_addr = {0};
    server_addr.sin_family = AF_INET;
    server_addr.sin_addr.s_addr = INADDR_ANY;
    server_addr.sin_port = htons(port);

    if (bind(g_network_state.sock, (sockaddr*)&server_addr, sizeof(server_addr)) == SOCKET_ERROR) {
        std::cerr << "绑定端口失败: " << WSAGetLastError() << std::endl;
        CleanupWinsock();
        return false;
    }

    // 监听
    if (listen(g_network_state.sock, 1) == SOCKET_ERROR) {
        std::cerr << "监听失败: " << WSAGetLastError() << std::endl;
        CleanupWinsock();
        return false;
    }

    std::cout << "服务端监听端口: " << port << std::endl;

    // 接受连接
    sockaddr_in client_addr = {0};
    int addr_len = sizeof(client_addr);
    SOCKET client_sock = accept(g_network_state.sock, (sockaddr*)&client_addr, &addr_len);
    
    if (client_sock == INVALID_SOCKET) {
        std::cerr << "接受连接失败: " << WSAGetLastError() << std::endl;
        CleanupWinsock();
        return false;
    }

    // 关闭监听套接字，使用客户端套接字
    closesocket(g_network_state.sock);
    g_network_state.sock = client_sock;
    g_network_state.connected = true;

    std::cout << "客户端连接: " << inet_ntoa(client_addr.sin_addr) << std::endl;
    AddMessage("客户端已连接: " + std::string(inet_ntoa(client_addr.sin_addr)), MessageType::Success);

    // 启动接收线程
    std::thread recv_thread(NetworkRecvThreadFunc);
    recv_thread.detach();

    return true;
}

// 连接服务端
bool ConnectToServer(const std::string& ip, int port) {
    if (!InitWinsock()) return false;

    // 创建套接字
    g_network_state.sock = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    if (g_network_state.sock == INVALID_SOCKET) {
        std::cerr << "创建套接字失败: " << WSAGetLastError() << std::endl;
        CleanupWinsock();
        return false;
    }

    // 服务端地址
    sockaddr_in server_addr = {0};
    server_addr.sin_family = AF_INET;
    server_addr.sin_addr.s_addr = inet_addr(ip.c_str());
    server_addr.sin_port = htons(port);

    // 连接
    if (connect(g_network_state.sock, (sockaddr*)&server_addr, sizeof(server_addr)) == SOCKET_ERROR) {
        std::cerr << "连接失败: " << WSAGetLastError() << std::endl;
        CleanupWinsock();
        return false;
    }

    g_network_state.connected = true;
    std::cout << "已连接到服务端: " << ip << ":" << port << std::endl;
    AddMessage("已连接到服务端: " + ip + ":" + std::to_string(port), MessageType::Success);

    // 启动接收线程
    std::thread recv_thread(NetworkRecvThreadFunc);
    recv_thread.detach();

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
            CleanupWinsock();
            break;
        }

        buf[recv_len] = '\0';
        std::string cmd = buf;
        std::cout << "收到命令: " << cmd << std::endl;

        // 处理命令
        if (cmd == "START_PVZ" && g_config.allow_auto_open) {
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
void AsyncConnectTask(const std::string& ip, int port, bool is_server) {
    g_is_connecting = true;
    
    bool success = false;
    if (is_server) {
        success = StartServer(port);
    } else {
        success = ConnectToServer(ip, port);
    }
    
    if (!success) {
        AddMessage("连接失败，请检查网络设置", MessageType::Error);
    }
    
    g_is_connecting = false;
}

// 开始异步连接
void StartAsyncConnect(const std::string& ip, int port, bool is_server) {
    if (g_is_connecting || g_network_state.connected) {
        return;
    }
    
    std::thread connect_thread(AsyncConnectTask, ip, port, is_server);
    connect_thread.detach();
}

// 检查是否正在连接
bool IsConnecting() {
    return g_is_connecting;
}