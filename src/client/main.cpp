#include "client.hpp"
#include <cstdlib>
#include <iostream>
#include <string>

void printUsage(const char* programName) {
    std::cout << "Usage: " << programName << " -h <ip> -p <port> [-d]" << std::endl;
    std::cout << "  -h <ip>    IP address of the server" << std::endl;
    std::cout << "  -p <port>  Port of the server" << std::endl;
    std::cout << "  -d         Enable debug mode" << std::endl;
}

int main(int argc, char* argv[]) {
    std::string serverIP = "127.0.0.1";
    int port = 0;
    
    for (int i = 1; i < argc; i++) {
        std::string arg = argv[i];
        
        if (arg == "-h" && i + 1 < argc) {
            serverIP = argv[++i];
        } else if (arg == "-p" && i + 1 < argc) {
            port = std::atoi(argv[++i]);
        } else if (arg == "-d") {
            debug_mode = true;
        } else {
            std::cerr << "Unknown option: " << arg << std::endl;
            printUsage(argv[0]);
            return 1;
        }
    }
    
    if (port <= 0) {
        std::cerr << "Missing required arguments!" << std::endl;
        printUsage(argv[0]);
        return 1;
    }
    
    Client client(serverIP, port);
    
    if (!client.connect()) {
        std::cerr << "Failed to connect to server" << std::endl;
        return 1;
    }
    
    if (!client.start()) {
        std::cerr << "Failed to start client" << std::endl;
        return 1;
    }
    
    while (client.isRunning()) {
        std::this_thread::sleep_for(std::chrono::milliseconds(100));
    }
    return 0;
}