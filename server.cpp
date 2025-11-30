#include <iostream>
#include <fstream>
#include <string>
#include <vector>
#include <map>
#include <sstream>
#include <thread>
#include <mutex>
#include <ctime>
#include <iomanip>
#include <cstring>

#ifdef _WIN32
    #include <winsock2.h>
    #include <ws2tcpip.h>
    #pragma comment(lib, "ws2_32.lib")
    #define close closesocket
    typedef int socklen_t;
#else
    #include <sys/socket.h>
    #include <netinet/in.h>
    #include <arpa/inet.h>
    #include <unistd.h>
    #define SOCKET int
    #define INVALID_SOCKET -1
    #define SOCKET_ERROR -1
#endif

// 食物信息结构
struct Food {
    int id;
    std::string name;
    std::vector<std::string> ingredients;
};

// 全局变量
std::map<int, Food> foods;  // 食物ID -> 食物信息
std::map<std::string, int> inventory;  // 食材名 -> 数量
std::mutex inventory_mutex;  // 库存互斥锁
std::mutex log_mutex;  // 日志互斥锁

// 读取食物信息文件
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
        Food food;
        iss >> food.id;
        iss >> food.name;
        
        std::string ingredient;
        while (iss >> ingredient) {
            food.ingredients.push_back(ingredient);
        }
        
        foods[food.id] = food;
    }
    
    file.close();
    std::cout << "已加载 " << foods.size() << " 种食物" << std::endl;
}

// 读取库存信息文件
void loadInventory(const std::string& filename) {
    std::ifstream file(filename);
    if (!file.is_open()) {
        std::cerr << "无法打开库存信息文件: " << filename << std::endl;
        exit(1);
    }
    
    std::string line;
    while (std::getline(file, line)) {
        if (line.empty()) continue;
        
        std::istringstream iss(line);
        std::string ingredient;
        int quantity;
        iss >> ingredient >> quantity;
        inventory[ingredient] = quantity;
    }
    
    file.close();
    std::cout << "已加载 " << inventory.size() << " 种食材库存" << std::endl;
}

// 获取当前时间字符串
std::string getCurrentTime() {
    auto now = std::time(nullptr);
    auto tm = *std::localtime(&now);
    std::ostringstream oss;
    oss << std::setfill('0') << std::setw(2) << tm.tm_hour << "-"
        << std::setw(2) << tm.tm_min << "-"
        << std::setw(2) << tm.tm_sec;
    return oss.str();
}

// 检查并扣减库存
bool checkAndDeductInventory(const std::vector<int>& foodIds) {
    std::lock_guard<std::mutex> lock(inventory_mutex);
    
    // 首先检查所有食材是否充足
    std::map<std::string, int> required;  // 需要的食材数量
    
    for (int foodId : foodIds) {
        if (foods.find(foodId) == foods.end()) {
            return false;  // 食物不存在
        }
        
        const Food& food = foods[foodId];
        for (const std::string& ingredient : food.ingredients) {
            required[ingredient]++;
        }
    }
    
    // 检查库存是否充足
    for (const auto& req : required) {
        if (inventory.find(req.first) == inventory.end() || 
            inventory[req.first] < req.second) {
            return false;  // 库存不足
        }
    }
    
    // 扣减库存
    for (const auto& req : required) {
        inventory[req.first] -= req.second;
    }
    
    return true;
}

// 获取当前库存状态字符串
std::string getInventoryStatus() {
    std::lock_guard<std::mutex> lock(inventory_mutex);
    std::ostringstream oss;
    bool first = true;
    for (const auto& item : inventory) {
        if (!first) oss << " ";
        oss << item.first << " " << item.second << ";";
        first = false;
    }
    return oss.str();
}

// 写入日志文件
void writeLog(const std::string& time, 
            const std::vector<int>& foodIds, 
            bool success) {
    std::lock_guard<std::mutex> lock(log_mutex);
    
    std::ofstream logFile("order.log", std::ios::app);
    if (!logFile.is_open()) {
        std::cerr << "无法打开日志文件" << std::endl;
        return;
    }
    
    logFile << time << " ";
    
    // 写入订单食物种类
    for (size_t i = 0; i < foodIds.size(); i++) {
        if (i > 0) logFile << ",";
        logFile << foodIds[i];
        if (foods.find(foodIds[i]) != foods.end()) {
            logFile << "(" << foods[foodIds[i]].name << ")";
        }
    }
    
    logFile << " " << (success ? "完成" : "失败") << " [";
    logFile << getInventoryStatus() << "]" << std::endl;
    
    logFile.close();
}

// 处理客户端请求
void handleClient(SOCKET clientSocket) {
    char buffer[1024];
    int bytesReceived;
    
    // 接收订单数据
    bytesReceived = recv(clientSocket, buffer, sizeof(buffer) - 1, 0);
    if (bytesReceived <= 0) {
        close(clientSocket);
        return;
    }
    
    buffer[bytesReceived] = '\0';
    
    // 解析订单（格式：数量 食物ID1 食物ID2 ...）
    std::istringstream iss(buffer);
    int count;
    iss >> count;
    
    std::vector<int> foodIds;
    for (int i = 0; i < count; i++) {
        int foodId;
        iss >> foodId;
        foodIds.push_back(foodId);
    }
    
    // 处理订单
    std::string time = getCurrentTime();
    bool success = checkAndDeductInventory(foodIds);
    
    // 写入日志
    writeLog(time, foodIds, success);
    
    // 发送响应（使用字符串格式）
    std::string response = success ? "1" : "-1";
    send(clientSocket, response.c_str(), response.length(), 0);
    
    close(clientSocket);
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

int main() {
    // 加载数据
    loadFoods("foods.txt");
    loadInventory("inventory.txt");
    
    // 初始化socket
#ifdef _WIN32
    if (!initWinsock()) {
        return 1;
    }
#endif
    
    // 创建socket
    SOCKET serverSocket = socket(AF_INET, SOCK_STREAM, 0);
    if (serverSocket == INVALID_SOCKET) {
        std::cerr << "创建socket失败" << std::endl;
#ifdef _WIN32
        cleanupWinsock();
#endif
        return 1;
    }
    
    // 设置socket选项（允许地址重用）
    int opt = 1;
#ifdef _WIN32
    setsockopt(serverSocket, SOL_SOCKET, SO_REUSEADDR, (char*)&opt, sizeof(opt));
#else
    setsockopt(serverSocket, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));
#endif
    
    // 绑定地址
    sockaddr_in serverAddr;
    memset(&serverAddr, 0, sizeof(serverAddr));
    serverAddr.sin_family = AF_INET;
    serverAddr.sin_addr.s_addr = INADDR_ANY;
    serverAddr.sin_port = htons(8888);
    
    if (bind(serverSocket, (sockaddr*)&serverAddr, sizeof(serverAddr)) == SOCKET_ERROR) {
        std::cerr << "绑定地址失败" << std::endl;
        close(serverSocket);
#ifdef _WIN32
        cleanupWinsock();
#endif
        return 1;
    }
    
    // 监听
    if (listen(serverSocket, 10) == SOCKET_ERROR) {
        std::cerr << "监听失败" << std::endl;
        close(serverSocket);
#ifdef _WIN32
        cleanupWinsock();
#endif
        return 1;
    }
    
    std::cout << "后厨系统启动，监听端口 8888..." << std::endl;
    
    // 接受客户端连接
    while (true) {
        sockaddr_in clientAddr;
        socklen_t clientAddrLen = sizeof(clientAddr);
        SOCKET clientSocket = accept(serverSocket, (sockaddr*)&clientAddr, &clientAddrLen);
        
        if (clientSocket == INVALID_SOCKET) {
            std::cerr << "接受连接失败" << std::endl;
            continue;
        }
        
        // 为每个客户端创建新线程处理
        std::thread(handleClient, clientSocket).detach();
    }
    
    close(serverSocket);
#ifdef _WIN32
    cleanupWinsock();
#endif
    
    return 0;
}

