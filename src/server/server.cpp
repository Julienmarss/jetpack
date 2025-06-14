#include "server.hpp"
#include <chrono>
#include <thread>

Server::Server(int port, const std::string& mapFile)
    : port(port), mapFile(mapFile) {
    for (int& socket : clientSockets) {
        socket = -1;
    }
    
    for (int i = 0; i < MAX_PLAYERS; i++) {
        players[i].id = i;
        players[i].alive = true;
    }
}

Server::~Server() {
    stop();
}

bool Server::start() {
    if (!gameMap.loadFromFile(mapFile)) {
        std::cerr << "Impossible de charger la carte: " << mapFile << std::endl;
        return false;
    }
    
    serverSocket = socket(AF_INET, SOCK_STREAM, 0);
    if (serverSocket < 0) {
        std::cerr << "Erreur lors de la création du socket" << std::endl;
        return false;
    }
    
    struct sockaddr_in serverAddr;
    std::memset(&serverAddr, 0, sizeof(serverAddr));
    serverAddr.sin_family = AF_INET;
    serverAddr.sin_addr.s_addr = INADDR_ANY;
    serverAddr.sin_port = htons(port);
    
    int opt = 1;
    if (setsockopt(serverSocket, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt)) < 0) {
        std::cerr << "Erreur lors de la configuration du socket" << std::endl;
        close(serverSocket);
        return false;
    }
    
    if (bind(serverSocket, (struct sockaddr*)&serverAddr, sizeof(serverAddr)) < 0) {
        std::cerr << "Erreur lors du bind du socket" << std::endl;
        close(serverSocket);
        return false;
    }
    
    if (listen(serverSocket, 5) < 0) {
        std::cerr << "Erreur lors de l'écoute des connexions" << std::endl;
        close(serverSocket);
        return false;
    }
    std::cout << "Serveur démarré sur le port " << port << std::endl;
    std::cout << "En attente de " << MAX_PLAYERS << " joueurs..." << std::endl;
    running = true;
    handleConnections();
    return true;
}

void Server::stop() {
    running = false;
    
    for (int& clientSocket : clientSockets) {
        if (clientSocket >= 0) {
            close(clientSocket);
            clientSocket = -1;
        }
    }
    
    if (serverSocket >= 0) {
        close(serverSocket);
        serverSocket = -1;
    }
    
    std::cout << "Serveur arrêté" << std::endl;
}

bool Server::acceptClient() {
    struct sockaddr_in clientAddr;
    socklen_t addrLen = sizeof(clientAddr);
    int clientSocket = accept(serverSocket, (struct sockaddr*)&clientAddr, &addrLen);
    
    if (clientSocket < 0) {
        std::cerr << "Erreur lors de l'acceptation de la connexion" << std::endl;
        return false;
    }
    
    char clientIP[INET_ADDRSTRLEN];
    inet_ntop(AF_INET, &(clientAddr.sin_addr), clientIP, INET_ADDRSTRLEN);
    
    int clientIndex = -1;
    for (int i = 0; i < MAX_PLAYERS; i++) {
        if (clientSockets[i] < 0) {
            clientSockets[i] = clientSocket;
            clientIndex = i;
            break;
        }
    }
    
    if (clientIndex < 0) {
        close(clientSocket);
        return false;
    }
    
    debugPrint("Client accepté avec index " + std::to_string(clientIndex) + 
              ", IP: " + std::string(clientIP) + ", socket: " + std::to_string(clientSocket));
    
    players[clientIndex].id = clientIndex;
    players[clientIndex].score = 0;
    players[clientIndex].alive = true;
    
    int connectedClients = getConnectedClientCount();
    debugPrint("Total clients connectés: " + std::to_string(connectedClients));
    
    for (int i = 0; i < MAX_PLAYERS; i++) {
        if (clientSockets[i] >= 0) {
            Protocol::sendWaitingStatus(clientSockets[i], connectedClients);
        }
    }
    
    return true;
}

void Server::handleClientMessage(int clientIndex) {
    if (clientSockets[clientIndex] < 0) return;
    
    char buffer[MAX_BUFFER_SIZE];
    int packetType;
    
    int dataSize = Protocol::receivePacket(clientSockets[clientIndex], packetType, buffer, MAX_BUFFER_SIZE);
    
    if (dataSize <= 0) {
        close(clientSockets[clientIndex]);
        clientSockets[clientIndex] = -1;
        players[clientIndex].alive = false;
        
        if (gameState == RUNNING) {
            checkGameEndCondition();
        }
        return;
    }
    
    switch (packetType) {
        case PLAYER_POS: {
            if (dataSize < 16) {
                debugPrint("Paquet PLAYER_POS invalide: taille=" + std::to_string(dataSize));
                return;
            }
            int player_id;
            float x, y;
            int jetpack_on;
            int offset = 0;
            
            std::memcpy(&player_id, buffer + offset, sizeof(int));
            offset += sizeof(int);
            std::memcpy(&x, buffer + offset, sizeof(float));
            offset += sizeof(float);
            std::memcpy(&y, buffer + offset, sizeof(float));
            offset += sizeof(float);
            std::memcpy(&jetpack_on, buffer + offset, sizeof(int));
            
            if (player_id == clientIndex) {
                bool oldState = players[clientIndex].jetpackOn;
                players[clientIndex].jetpackOn = (jetpack_on != 0);
            } else {
                debugPrint("ID de joueur incorrect dans PLAYER_POS");
            }
            break;
        }
        
        case READY: {
            debugPrint("Client " + std::to_string(clientIndex) + " prêt");
            break;
        }
        
        default:
            debugPrint("Type de paquet non géré: " + std::to_string(packetType));
            break;
    }
}

void Server::handleConnections() {
    std::vector<pollfd> fds;

    while (running) {
        fds.clear();
        fds.push_back({serverSocket, POLLIN, 0});
        for (int clientSocket : clientSockets) {
            if (clientSocket >= 0) {
                fds.push_back({clientSocket, POLLIN, 0});
            }
        }

        int connectedClients = getConnectedClientCount();
        int ready = poll(fds.data(), fds.size(), POLL_TIMEOUT);

        if (ready <= 0) {
            if (ready < 0 && errno != EINTR) {
                std::cerr << "Erreur de poll: " << strerror(errno) << std::endl;
                break;
            }
            continue;
        }

        if (fds[0].revents & POLLIN) {
            if (acceptClient()) {
                int connectedClients = getConnectedClientCount();
                debugPrint("Total clients connectés: " + std::to_string(connectedClients));
                std::cout << "Client connecté, " << connectedClients << "/" << MAX_PLAYERS << " joueurs" << std::endl;

                for (int i = 0; i < MAX_PLAYERS; i++) {
                    if (clientSockets[i] >= 0) {
                        Protocol::sendWaitingStatus(clientSockets[i], connectedClients);
                    }
                }

                if (connectedClients >= MAX_PLAYERS) {
                    std::cout << "Tous les joueurs sont connectés, démarrage de la partie" << std::endl;

                    const std::vector<Vector2>& startPositions = gameMap.getStartPositions();
                    const float CELL_SIZE = 32.0f;
                    for (size_t i = 0; i < MAX_PLAYERS && i < startPositions.size(); i++) {
                        players[i].position.x = startPositions[i].x * CELL_SIZE;
                        players[i].position.y = startPositions[i].y * CELL_SIZE;
                        players[i].velocityY = 0.0f;
                        players[i].jetpackOn = false;
                    }

                    for (int i = 0; i < MAX_PLAYERS; i++) {
                        if (clientSockets[i] >= 0) {
                            Protocol::sendMap(clientSockets[i], gameMap);
                        }
                    }
                    gameState = RUNNING;
                    broadcastGameState();
                    std::thread gameThread(&Server::gameLoop, this);
                    gameThread.detach();
                }
            }
        }
    }
}

void Server::gameLoop() {
    const int TICKS_PER_SECOND = 60;
    const std::chrono::milliseconds TICK_DURATION(1000 / TICKS_PER_SECOND);
    const float GRACE_PERIOD = 2.0f;
    gameStartTime = std::chrono::steady_clock::now();
    bool gracePeriod = true;
    
    while (running && gameState == RUNNING) {
        auto startTime = std::chrono::steady_clock::now();
        
        if (gracePeriod) {
            auto elapsedTime = std::chrono::duration_cast<std::chrono::seconds>(
                startTime - gameStartTime).count();
            if (elapsedTime >= GRACE_PERIOD) {
                gracePeriod = false;
                debugPrint("Période de grâce terminée");
            }
        }
        updateGameState();
        if (gracePeriod) {
            for (int i = 0; i < MAX_PLAYERS; i++) {
                if (!players[i].alive) {
                    players[i].alive = true;
                    debugPrint("[RESURRECTION] Joueur " + std::to_string(i) + 
                              " ressuscité pendant la période de grâce");
                }
            }
        }
        broadcastGameState();
        auto endTime = std::chrono::steady_clock::now();
        auto elapsedTime = std::chrono::duration_cast<std::chrono::milliseconds>(endTime - startTime);
        if (elapsedTime < TICK_DURATION) {
            std::this_thread::sleep_for(TICK_DURATION - elapsedTime);
        }
    }
}

void Server::updateGameState() {
    const float CELL_SIZE = 32.0f;
    const float FLOOR_Y = 486.0f;
    const float JET_ACCEL = -1.5f;
    const float GRAV_ACCEL = 0.5f;
    const float HORIZ_SPEED = 4.0f;
    const float MAX_FALL = 10.0f;
    const float MAX_RISE = -10.0f;
    const float DAMP_FACTOR = 0.97f;
    
    for (int i = 0; i < MAX_PLAYERS; i++) {
        if (!players[i].alive) 
            continue;

        float oldY = players[i].position.y;
        float oldVelocityY = players[i].velocityY;
        
        if (players[i].jetpackOn) {
            players[i].velocityY += JET_ACCEL;
        }
        oldVelocityY = players[i].velocityY;
        players[i].velocityY += GRAV_ACCEL;
        oldVelocityY = players[i].velocityY;
        players[i].velocityY *= DAMP_FACTOR;
        
        if (players[i].velocityY > MAX_FALL) {
            players[i].velocityY = MAX_FALL;
        } else if (players[i].velocityY < MAX_RISE) {
            players[i].velocityY = MAX_RISE;
        }
        
        oldY = players[i].position.y;
        players[i].position.y += players[i].velocityY;
        players[i].position.x += HORIZ_SPEED;
        
        if (players[i].position.y < 0) {
            players[i].position.y = 0;
            players[i].velocityY = 0;
        } else if (players[i].position.y > FLOOR_Y) {
            players[i].position.y = FLOOR_Y;
            players[i].velocityY = 0;
        }

        bool wasAlive = players[i].alive;
        checkCollisions(i);
        if (wasAlive && !players[i].alive) {
            debugPrint("Joueur " + std::to_string(i) + " est mort lors de checkCollisions");
            continue;
        }
        
        if (players[i].position.x >= gameMap.getWidth() * CELL_SIZE - PLAYER_WIDTH) {
            debugPrint("Joueur " + std::to_string(i) + " a atteint la fin du niveau");
            endGame(i);
            return;
        }
    }
}

void Map::setCell(int x, int y, CellType cellType) {
    if (x >= 0 && x < width && y >= 0 && y < height) {
        data[y * width + x] = cellType;
    }
}

void Server::checkCollisions(int playerIndex) {
    Player& player = players[playerIndex];
    
    const float CELL_SIZE = 32.0f;
    int startTileX = static_cast<int>(player.position.x / CELL_SIZE);
    int endTileX = static_cast<int>((player.position.x + PLAYER_WIDTH - 1) / CELL_SIZE);
    int startTileY = static_cast<int>(player.position.y / CELL_SIZE);
    int endTileY = static_cast<int>((player.position.y + PLAYER_HEIGHT - 1) / CELL_SIZE);
    
    for (int tileY = startTileY; tileY <= endTileY; tileY++) {
        for (int tileX = startTileX; tileX <= endTileX; tileX++) {
            if (tileX < 0 || tileX >= gameMap.getWidth() || tileY < 0 || tileY >= gameMap.getHeight()) {
                continue;
            }
            
            CellType cell = gameMap.getCell(tileX, tileY);
            
            if (cell == COIN) {
                player.score++;
                gameMap.setCell(tileX, tileY, EMPTY);
            } 
            else if (cell == ELECTRIC) {
                player.alive = false;
                checkGameEndCondition();
                return;
            }
        }
    }
}

void Server::checkCoinCollisions(int playerIndex) {
    Player& player = players[playerIndex];
    
    const float CELL_SIZE = 32.0f;
    int startTileX = static_cast<int>(player.position.x / CELL_SIZE);
    int endTileX = static_cast<int>((player.position.x + PLAYER_WIDTH - 1) / CELL_SIZE);
    int startTileY = static_cast<int>(player.position.y / CELL_SIZE);
    int endTileY = static_cast<int>((player.position.y + PLAYER_HEIGHT - 1) / CELL_SIZE);
    
    for (int tileY = startTileY; tileY <= endTileY; tileY++) {
        for (int tileX = startTileX; tileX <= endTileX; tileX++) {
            if (tileX < 0 || tileX >= gameMap.getWidth() || tileY < 0 || tileY >= gameMap.getHeight()) {
                continue;
            }
            
            if (gameMap.getCell(tileX, tileY) == COIN) {
                player.score++;
                gameMap.setCell(tileX, tileY, EMPTY);
            }
        }
    }
}

void Server::broadcastGameState() {
    for (int i = 0; i < MAX_PLAYERS; i++) {
        if (clientSockets[i] >= 0) {
            Protocol::sendGameState(clientSockets[i], gameState, players);
        }
    }
}

void Server::endGame(int winnerId) {
    debugPrint("Fin de partie, gagnant: Joueur " + std::to_string(winnerId));
    
    gameState = OVER;
    std::array<int, MAX_PLAYERS> scores;
    for (int i = 0; i < MAX_PLAYERS; i++) {
        scores[i] = players[i].score;
    }
    
    for (int i = 0; i < MAX_PLAYERS; i++) {
        if (clientSockets[i] >= 0) {
            Protocol::sendGameOver(clientSockets[i], winnerId, scores);
        }
    }
}

void Server::checkGameEndCondition() {
    int aliveCount = 0;
    int lastAlivePlayer = -1;
    
    for (int i = 0; i < MAX_PLAYERS; i++) {
        if (players[i].alive) {
            aliveCount++;
            lastAlivePlayer = i;
        }
    }
    
    if (aliveCount <= 1 && gameState == RUNNING) {
        endGame(lastAlivePlayer);
    }
}

int Server::getConnectedClientCount() const {
    int count = 0;
    for (int socket : clientSockets) {
        if (socket >= 0) {
            count++;
        }
    }
    return count;
}
