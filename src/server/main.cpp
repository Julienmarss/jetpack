#include "server.hpp"
#include <cstdlib>
#include <iostream>
#include <string>

void printUsage(const char* binaryName) {
    std::cout << "Usage: " << binaryName << " -p <port> -m <map> [-d]" << std::endl;
    std::cout << "  -p <port>  Port on which the server will listen" << std::endl;
    std::cout << "  -m <map>   Path to the map file" << std::endl;
    std::cout << "  -d         Enable debug mode" << std::endl;
}

int main(int argc, char* argv[]) {
    int port = 0;
    std::string mapFile;
    
    for (int i = 1; i < argc; i++) {
        std::string arg = argv[i];
        
        if (arg == "-p" && i + 1 < argc) {
            port = std::atoi(argv[++i]);
        } else if (arg == "-m" && i + 1 < argc) {
            mapFile = argv[++i];
        } else if (arg == "-d") {
            debug_mode = true;
        } else {
            std::cerr << "Unknown option: " << arg << std::endl;
            printUsage(argv[0]);
            return 1;
        }
    }
    
    if (port <= 0 || mapFile.empty()) {
        std::cerr << "Missing required arguments!" << std::endl;
        printUsage(argv[0]);
        return 1;
    }
    
    Server server(port, mapFile);
    
    if (!server.start()) {
        std::cerr << "Failed to start server" << std::endl;
        return 1;
    }
    
    std::cout << "Press Enter to stop the server..." << std::endl;
    std::cin.get();
    server.stop();
    return 0;
}