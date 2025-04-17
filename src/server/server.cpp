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

void Server::handleConnections() {
    std::vector<pollfd> fds;
    fds.push_back({serverSocket, POLLIN, 0});
    
    for (int clientSocket : clientSockets) {
        if (clientSocket >= 0) {
            fds.push_back({clientSocket, POLLIN, 0});
        }
    }
    
    int connectedClients = 0;
    for (int socket : clientSockets) {
        if (socket >= 0) {
            connectedClients++;
        }
    }
    
    while (running) {
        int ready = poll(fds.data(), fds.size(), POLL_TIMEOUT);
        
        if (ready <= 0) {
            if (ready < 0 && errno != EINTR) {
                std::cerr << "Erreur de poll: " << strerror(errno) << std::endl;
                break;
            }
            continue;
        }
        
        if (fds[0].revents & POLLIN) {
            if (connectedClients < MAX_PLAYERS) {
                if (acceptClient()) {
                    connectedClients++;
                    
                    fds.clear();
                    fds.push_back({serverSocket, POLLIN, 0});
                    for (int clientSocket : clientSockets) {
                        if (clientSocket >= 0) {
                            fds.push_back({clientSocket, POLLIN, 0});
                        }
                    }
                    
                    std::cout << "Client connecté, " << connectedClients << "/" << MAX_PLAYERS << " joueurs" << std::endl;
                    
                    if (connectedClients == MAX_PLAYERS) {
                        std::cout << "Tous les joueurs sont connectés, démarrage de la partie" << std::endl;
                        
                        const std::vector<Vector2>& startPositions = gameMap.getStartPositions();
                        for (size_t i = 0; i < MAX_PLAYERS && i < startPositions.size(); i++) {
                            players[i].position = startPositions[i];
                        }
                        
                        for (int i = 0; i < MAX_PLAYERS; i++) {
                            if (clientSockets[i] >= 0) {
                                Protocol::sendMap(clientSockets[i], gameMap);
                            }
                        }
                        
                        gameState = RUNNING;
                        std::thread gameThread(&Server::gameLoop, this);
                        gameThread.detach();
                    }
                }
            }
        }
        
        for (size_t i = 1; i < fds.size(); i++) {
            if (fds[i].revents & POLLIN) {
                int clientIndex = -1;
                
                for (int j = 0; j < MAX_PLAYERS; j++) {
                    if (clientSockets[j] == fds[i].fd) {
                        clientIndex = j;
                        break;
                    }
                }
                
                if (clientIndex >= 0) {
                    handleClientMessage(clientIndex);
                }
            }
        }
    }
}

bool Server::acceptClient() {
    struct sockaddr_in clientAddr;
    socklen_t addrLen = sizeof(clientAddr);
    int clientSocket = accept(serverSocket, (struct sockaddr*)&clientAddr, &addrLen);
    
    if (clientSocket < 0) {
        std::cerr << "Erreur lors de l'acceptation de la connexion" << std::endl;
        return false;
    }
    
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
    players[clientIndex].id = clientIndex;
    players[clientIndex].score = 0;
    players[clientIndex].alive = true;
    
    return true;
}

void Server::handleClientMessage(int clientIndex) {
    char buffer[MAX_BUFFER_SIZE];
    int packetType;
    
    int dataSize = Protocol::receivePacket(clientSockets[clientIndex], packetType, buffer, MAX_BUFFER_SIZE);
    
    if (dataSize <= 0) {
        close(clientSockets[clientIndex]);
        clientSockets[clientIndex] = -1;
        players[clientIndex].alive = false;
        
        if (gameState == RUNNING) {
            int aliveCount = 0;
            int lastAlivePlayer = -1;
            
            for (int i = 0; i < MAX_PLAYERS; i++) {
                if (players[i].alive) {
                    aliveCount++;
                    lastAlivePlayer = i;
                }
            }
            
            if (aliveCount <= 1) {
                endGame(lastAlivePlayer);
            }
        }
        
        return;
    }
    
    switch (packetType) {
        case PLAYER_POS: {
            if (dataSize < (int)sizeof(int) * 3 + (int)sizeof(float) * 2) {
                debugPrint("Paquet PLAYER_POS invalide");
                return;
            }
            
            struct {
                int player_id;
                float x;
                float y;
                int jetpack_on;
            } data;
            
            std::memcpy(&data, buffer, sizeof(data));
            
            if (data.player_id == clientIndex) {
                players[clientIndex].position.x = data.x;
                players[clientIndex].position.y = data.y;
                players[clientIndex].jetpackOn = data.jetpack_on != 0;
            } else {
                debugPrint("ID de joueur incorrect dans le paquet PLAYER_POS");
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

void Server::gameLoop() {
    const int TICKS_PER_SECOND = 60;
    const std::chrono::milliseconds TICK_DURATION(1000 / TICKS_PER_SECOND);
    
    debugPrint("Démarrage de la boucle de jeu");
    
    while (running && gameState == RUNNING) {
        auto startTime = std::chrono::steady_clock::now();
        updateGameState();
        broadcastGameState();
        auto endTime = std::chrono::steady_clock::now();
        auto elapsedTime = std::chrono::duration_cast<std::chrono::milliseconds>(endTime - startTime);
        if (elapsedTime < TICK_DURATION) {
            std::this_thread::sleep_for(TICK_DURATION - elapsedTime);
        }
    }
    
    debugPrint("Boucle de jeu finito attention");
}

void Server::updateGameState() {
    for (int i = 0; i < MAX_PLAYERS; i++) {
        if (players[i].alive) {
            if (players[i].jetpackOn) {
                players[i].velocityY -= GRAVITY * 2;
                if (players[i].velocityY < -10.0f) {
                    players[i].velocityY = -10.0f;
                }
            } else {
                players[i].velocityY += GRAVITY;
                if (players[i].velocityY > 10.0f) {
                    players[i].velocityY = 10.0f;
                }
            }
            
            players[i].position.y += players[i].velocityY;
            players[i].position.x += HORIZONTAL_SPEED;
            
            if (players[i].position.y < 0) {
                players[i].position.y = 0;
                players[i].velocityY = 0;
            } else if (players[i].position.y > gameMap.getHeight() - PLAYER_HEIGHT) {
                players[i].position.y = gameMap.getHeight() - PLAYER_HEIGHT;
                players[i].velocityY = 0;
            }
            checkCollisions(i);
            if (players[i].position.x >= gameMap.getWidth() - PLAYER_WIDTH) {
                endGame(i);
                return;
            }
        }
    }
}

void Server::checkCollisions(int playerIndex) {
    Player& player = players[playerIndex];
    
    if (gameMap.checkCollision(player.position.x, player.position.y, PLAYER_WIDTH, PLAYER_HEIGHT, COIN)) {
        player.score++;
        debugPrint("Joueur " + std::to_string(playerIndex) + " a collecté une pièce, score: " + 
                 std::to_string(player.score));
    }
    
    if (gameMap.checkCollision(player.position.x, player.position.y, PLAYER_WIDTH, PLAYER_HEIGHT, ELECTRIC)) {
        player.alive = false;
        debugPrint("Joueur " + std::to_string(playerIndex) + " est mort");
        
        int aliveCount = 0;
        int lastAlivePlayer = -1;
        
        for (int i = 0; i < MAX_PLAYERS; i++) {
            if (players[i].alive) {
                aliveCount++;
                lastAlivePlayer = i;
            }
        }
        
        if (aliveCount <= 1) {
            endGame(lastAlivePlayer);
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
