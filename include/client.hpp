/*
** EPITECH PROJECT, 2025
** Tek 2 B-NWP-400-LIL-4-1-jetpack-julien.mars
** File description:
** client.hpp
*/

#ifndef CLIENT_HPP
#define CLIENT_HPP

#include "common.hpp"
#include "map.hpp"
#include "protocol.hpp"
#include <SDL2/SDL.h>
#include <SDL2/SDL_image.h>
#include <SDL2/SDL_ttf.h>
#include <map>

class Client {
public:
    Client(const std::string& serverIP, int port);
    ~Client();

    bool connect();
    bool start();
    void stop();
    bool isRunning() const { return running; }

private:
    std::string serverIP;
    int port;
    int clientSocket = -1;
    Map gameMap;
    std::array<Player, MAX_PLAYERS> players;
    int myPlayerId = -1;
    GameState gameState = WAITING;
    std::atomic<bool> running{false};
    
    std::thread networkThread;
    std::thread graphicsThread;
    std::mutex gameMutex;
    
    SDL_Window* window = nullptr;
    SDL_Renderer* renderer = nullptr;
    TTF_Font* font = nullptr;
    std::map<std::string, SDL_Texture*> textures;
    
    int windowWidth = 800;
    int windowHeight = 600;
    int frameCountPlayer = 0;
    int frameCountCoin = 0;
    int frameCountZapper = 0;
    int lastFrameTime = 0;
    int waitingPlayers = 1;

    
    void renderPlayer(int x, int y, int width, int height, bool jetpackOn);
    void renderCoin(int x, int y, int width, int height);
    void renderZapper(int x, int y, int width, int height);
    void networkLoop();
    void graphicsLoop();
    void handleServerMessage();
    void sendPlayerPosition(bool jetpackOn);
    bool initSDL();
    SDL_Texture* loadTexture(const std::string& path);
    void render();
    void cleanupSDL();
};

#endif /* CLIENT_HPP */