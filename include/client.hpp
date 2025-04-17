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
#include <SFML/Graphics.hpp>
#include <SFML/Window.hpp>
#include <SFML/System.hpp>
#include <map>

class Client {
public:
    Client(const std::string& serverIP, int port);
    ~Client();

    bool connect();
    bool start();
    void stop();
    bool isRunning() const { return running; }
    void sendPlayerPosition(bool jetpackOn);

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
    
    sf::RenderWindow window;
    sf::Font font;
    std::map<std::string, sf::Texture> textures;
    std::map<std::string, sf::Sprite> sprites;
    
    int windowWidth = 800;
    int windowHeight = 600;
    int frameCountPlayer = 0;
    int frameCountCoin = 0;
    int frameCountZapper = 0;
    int lastFrameTime = 0;
    int waitingPlayers = 1;

    
    void networkLoop();
    void graphicsLoop();
    void handleServerMessage();
};

#endif /* CLIENT_HPP */