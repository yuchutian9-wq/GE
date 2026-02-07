#include <winsock2.h>
#include <ws2tcpip.h>
#include <iostream>
#include <vector>
#include <string>
#include <thread>
#include <mutex>
#include <map>
#include <sstream>

#pragma comment(lib, "ws2_32.lib")

std::map<SOCKET, std::string> client_map;
std::mutex mtx;

void BroadcastUserList() {
    std::string listMsg = "LIST:";
    mtx.lock(); 
    for (auto const& user : client_map) {
        listMsg += user.second + ",";
    }
    mtx.unlock();

    mtx.lock();
    for (auto const& user : client_map) {
        send(user.first, listMsg.c_str(), (int)listMsg.size(), 0);
    }
    mtx.unlock();
}

void doClient(SOCKET client_socket) {
    char buffer[1024];
    while (true) {
        int bytes_received = recv(client_socket, buffer, sizeof(buffer) - 1, 0);

        if (bytes_received <= 0) {
            mtx.lock();
            client_map.erase(client_socket);
            mtx.unlock(); 

            closesocket(client_socket);
            std::cout << "Client disconnected." << std::endl;

            BroadcastUserList();
            return; 
        }

        buffer[bytes_received] = '\0';
        std::string raw(buffer);

        if (raw.substr(0, 6) == "LOGIN:") {
            std::string name = raw.substr(6);

            mtx.lock();
            client_map[client_socket] = name;
            mtx.unlock(); 

            std::cout << name << " logged in." << std::endl;
            BroadcastUserList();
        }
        else if (raw.substr(0, 4) == "PUB:") {
            std::string content = raw.substr(4);
            std::string sender;
            mtx.lock(); 
            sender = client_map[client_socket];
            mtx.unlock();
            std::string formatted = "[Pub] " + sender + ": " + content;
            mtx.lock();
            for (auto const& user : client_map) {
                send(user.first, formatted.c_str(), (int)formatted.size(), 0);
            }
            mtx.unlock();
        }
        else if (raw.substr(0, 3) == "DM:") {
            size_t Maohao = raw.find(':', 3);
            if (Maohao == std::string::npos) continue;

            std::string Name = raw.substr(3, Maohao - 3);
            std::string content = raw.substr(Maohao + 1);
            std::string sender;
            mtx.lock();
            sender = client_map[client_socket];
            mtx.unlock();
            std::string formatted = "[DM from " + sender + "]: " + content;
            mtx.lock();
            for (auto const& user : client_map) {
                if (user.second == Name) { 
                    send(user.first, formatted.c_str(), (int)formatted.size(), 0);
                    break;
                }
            }
            mtx.unlock();
        }
    }
}

int main() {
    WSADATA wsaData;
    if (WSAStartup(MAKEWORD(2, 2), &wsaData) != 0) {
        std::cerr << "WSAStartup failed with error: " << WSAGetLastError() << std::endl;
        return 1;
    }

    SOCKET server_socket = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    if (server_socket == INVALID_SOCKET) {
        std::cerr << "Socket creation failed with error: " << WSAGetLastError() << std::endl;
        WSACleanup();
        return 1;
    }
    sockaddr_in server_address = {};
    server_address.sin_family = AF_INET;
    server_address.sin_port = htons(65432);  // Server port
    server_address.sin_addr.s_addr = INADDR_ANY; // Accept connections on any IP address

    if (bind(server_socket, (sockaddr*)&server_address, sizeof(server_address)) == SOCKET_ERROR) {
        std::cerr << "Bind failed with error: " << WSAGetLastError() << std::endl;
        closesocket(server_socket);
        WSACleanup();
        return 1;
    }

    if (listen(server_socket, SOMAXCONN) == SOCKET_ERROR) {
        std::cerr << "Listen failed with error: " << WSAGetLastError() << std::endl;
        closesocket(server_socket);
        WSACleanup();
        return 1;
    }

    std::cout << "Server is listening on port 65432..." << std::endl;

    int connection_count = 0;
    while (true) {
        SOCKET client_socket = accept(server_socket, NULL, NULL);

        if (client_socket == INVALID_SOCKET) {
            std::cerr << "Accept failed with error: " << WSAGetLastError() << std::endl;
            continue;
        }

        std::cout << "Accepted new connection. ID = " << ++connection_count << std::endl;

        std::thread(doClient, client_socket).detach();
    }
    closesocket(server_socket);
    WSACleanup();
    return 0;
}