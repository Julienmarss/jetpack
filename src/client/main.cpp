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
    
    // Main loop du programme - NÉCESSAIRE pour traiter les événements SDL
    SDL_Event event;
    bool jetpackOn = false;
    
    while (client.isRunning()) {
        // Traitement des événements SDL dans le thread principal
        while (SDL_PollEvent(&event)) {
            if (event.type == SDL_QUIT) {
                client.stop();
                break;
            } else if (event.type == SDL_KEYDOWN) {
                if (event.key.keysym.sym == SDLK_SPACE) {
                    jetpackOn = true;
                    client.sendPlayerPosition(jetpackOn);
                }
            } else if (event.type == SDL_KEYUP) {
                if (event.key.keysym.sym == SDLK_SPACE) {
                    jetpackOn = false;
                    client.sendPlayerPosition(jetpackOn);
                }
            }
        }
        
        // Petite pause pour éviter de surcharger le CPU
        std::this_thread::sleep_for(std::chrono::milliseconds(16)); // ~60 FPS
    }

    return 0;
}