#include <iostream>
#include <fstream>
#include <string>
#include <vector>
#include <map>
#include <sstream>
#include <thread>
#include <random>
#include <chrono>
#include <cstring>

#ifdef _WIN32
    #include <winsock2.h>
    #include <ws2tcpip.h>
    #pragma comment(lib, "ws2_32.lib")
    #define close closesocket
#else
    #include <sys/socket.h>
    #include <netinet/in.h>
    #include <arpa/inet.h>
    #include <unistd.h>
    #define SOCKET int
    #define INVALID_SOCKET -1
    #define SOCKET_ERROR -1
#endif

// 全局变量
std::map<int, std::string> foods;  // 食物ID -> 食物名称
std::mutex cout_mutex;  // 输出互斥锁

// 读取食物信息文件（仅读取ID和名称）
void loadFoods(const std::string& filename) {
    std::ifstream file(filename);
    if (!file.is_open()) {
        std::cerr << "无法打开食物信息文件: " << filename << std::endl;
        exit(1);
    }
    
    std::string line;
    while (std::getline(file, line)) {
        if (line.empty()) continue;
        
        std::istringstream iss(line);
        int id;
        std::string name;
        iss >> id >> name;
        foods[id] = name;
    }
    
    file.close();
}

// 初始化socket（Windows）
#ifdef _WIN32
bool initWinsock() {
    WSADATA wsaData;
    int result = WSAStartup(MAKEWORD(2, 2), &wsaData);
    if (result != 0) {
        std::cerr << "WSAStartup失败: " << result << std::endl;
        return false;
    }
    return true;
}

void cleanupWinsock() {
    WSACleanup();
}
#endif

// 顾客线程函数
void customerThread(int threadId) {
    // 随机等待一段时间（1-5秒）
    std::random_device rd;
    std::mt19937 gen(rd());
    std::uniform_int_distribution<> waitDist(1000, 5000);
    std::uniform_int_distribution<> foodCountDist(1, 3);  // 每次点1-3个食物
    std::uniform_int_distribution<> foodIdDist(1, static_cast<int>(foods.size()));
    
    int waitTime = waitDist(gen);
    std::this_thread::sleep_for(std::chrono::milliseconds(waitTime));
    
    // 创建socket
    SOCKET clientSocket = socket(AF_INET, SOCK_STREAM, 0);
    if (clientSocket == INVALID_SOCKET) {
        std::lock_guard<std::mutex> lock(cout_mutex);
        std::cerr << "线程 " << threadId << ": 创建socket失败" << std::endl;
        return;
    }
    
    // 连接服务器
    sockaddr_in serverAddr;
    memset(&serverAddr, 0, sizeof(serverAddr));
    serverAddr.sin_family = AF_INET;
    serverAddr.sin_port = htons(8888);
#ifdef _WIN32
    serverAddr.sin_addr.s_addr = inet_addr("127.0.0.1");
#else
    inet_pton(AF_INET, "127.0.0.1", &serverAddr.sin_addr);
#endif
    
    if (connect(clientSocket, (sockaddr*)&serverAddr, sizeof(serverAddr)) == SOCKET_ERROR) {
        std::lock_guard<std::mutex> lock(cout_mutex);
        std::cerr << "线程 " << threadId << ": 连接服务器失败" << std::endl;
        close(clientSocket);
        return;
    }
    
    // 生成随机订单（1-3个食物）
    int orderCount = foodCountDist(gen);
    std::vector<int> order;
    for (int i = 0; i < orderCount; i++) {
        order.push_back(foodIdDist(gen));
    }
    
    // 构建订单消息（格式：数量 食物ID1 食物ID2 ...）
    std::ostringstream oss;
    oss << orderCount;
    for (int foodId : order) {
        oss << " " << foodId;
    }
    std::string orderMsg = oss.str();
    
    // 发送订单
    send(clientSocket, orderMsg.c_str(), orderMsg.length(), 0);
    
    // 接收响应
    char responseBuffer[16];
    int bytesReceived = recv(clientSocket, responseBuffer, sizeof(responseBuffer) - 1, 0);
    
    close(clientSocket);
    
    // 输出结果
    {
        std::lock_guard<std::mutex> lock(cout_mutex);
        std::cout << "线程 " << threadId << ": 订单 [";
        for (size_t i = 0; i < order.size(); i++) {
            if (i > 0) std::cout << ", ";
            std::cout << order[i];
            if (foods.find(order[i]) != foods.end()) {
                std::cout << "(" << foods[order[i]] << ")";
            }
        }
        std::cout << "] ";
        
        if (bytesReceived > 0) {
            responseBuffer[bytesReceived] = '\0';
            int response = std::stoi(responseBuffer);
            if (response == 1) {
                std::cout << "订单完成" << std::endl;
            } else {
                std::cout << "缺货失败" << std::endl;
            }
        } else {
            std::cout << "接收响应失败" << std::endl;
        }
    }
}

int main(int argc, char* argv[]) {
    // 加载食物信息
    loadFoods("foods.txt");
    
    // 初始化socket
#ifdef _WIN32
    if (!initWinsock()) {
        return 1;
    }
#endif
    
    // 获取线程数量（默认为5）
    int threadCount = 5;
    if (argc > 1) {
        threadCount = std::stoi(argv[1]);
    }
    
    std::cout << "顾客系统启动，创建 " << threadCount << " 个顾客线程..." << std::endl;
    
    // 创建多个顾客线程
    std::vector<std::thread> threads;
    for (int i = 1; i <= threadCount; i++) {
        threads.emplace_back(customerThread, i);
    }
    
    // 等待所有线程完成
    for (auto& t : threads) {
        t.join();
    }
    
    std::cout << "所有顾客线程已完成" << std::endl;
    
#ifdef _WIN32
    cleanupWinsock();
#endif
    
    return 0;
}

