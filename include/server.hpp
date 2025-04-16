/*
** EPITECH PROJECT, 2025
** Tek 2 B-NWP-400-LIL-4-1-jetpack-julien.mars
** File description:
** server.hpp
*/

#ifndef SERVER_HPP
#define SERVER_HPP

#include "common.hpp"
#include "map.hpp"
#include "protocol.hpp"

class Server {
public:
    Server(int port, const std::string& mapFile);
    ~Server();

    bool start();
    void stop();

private:
    int port;
    std::string mapFile;
    int serverSocket = -1;
    Map gameMap;
    std::array<Player, MAX_PLAYERS> players;
    std::array<int, MAX_PLAYERS> clientSockets;
    GameState gameState = WAITING;
    std::atomic<bool> running{false};

    void handleConnections();
    bool acceptClient();
    void handleClientMessage(int clientIndex);
    void gameLoop();
    void updateGameState();
    void checkCollisions(int playerIndex);
    void broadcastGameState();
    void endGame(int winnerId);
    int getConnectedClientCount() const;
};

#endif /* SERVER_HPP */