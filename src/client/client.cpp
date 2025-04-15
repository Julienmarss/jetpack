#include "client.hpp"
#include <chrono>
#include <thread>
#include <iostream>
#include <SDL2/SDL.h>
#include <SDL2/SDL_image.h>

Client::Client(const std::string& serverIP, int port)
   : serverIP(serverIP), port(port) {
   for (int i = 0; i < MAX_PLAYERS; ++i) {
       players[i].id = i;
       players[i].alive = true;
   }
}

Client::~Client() {
   stop();
}

bool Client::connect() {
   clientSocket = socket(AF_INET, SOCK_STREAM, 0);
   if (clientSocket < 0) {
       std::cerr << "Erreur lors de la création du socket" << std::endl;
       return false;
   }
   
   struct sockaddr_in serverAddr;
   std::memset(&serverAddr, 0, sizeof(serverAddr));
   serverAddr.sin_family = AF_INET;
   serverAddr.sin_port = htons(port);
   
   if (inet_pton(AF_INET, serverIP.c_str(), &serverAddr.sin_addr) <= 0) {
       std::cerr << "Adresse IP invalide: " << serverIP << std::endl;
       close(clientSocket);
       clientSocket = -1;
       return false;
   }
   
   if (::connect(clientSocket, (struct sockaddr*)&serverAddr, sizeof(serverAddr)) < 0) {
       std::cerr << "Erreur lors de la connexion au serveur" << std::endl;
       close(clientSocket);
       clientSocket = -1;
       return false;
   }
   
   std::cout << "Connecté au serveur " << serverIP << ":" << port << std::endl;
   
   if (!Protocol::sendPacket(clientSocket, READY)) {
       std::cerr << "Erreur lors de l'envoi du paquet READY" << std::endl;
       close(clientSocket);
       clientSocket = -1;
       return false;
   }
   
   return true;
}

bool Client::start() {
    if (clientSocket < 0) {
        std::cerr << "Non connecté au serveur" << std::endl;
        return false;
    }
    
    if (!initSDL()) {
        std::cerr << "Erreur lors de l'initialisation de SDL" << std::endl;
        return false;
    }
    
    running = true;
    
    try {
        networkThread = std::thread(&Client::networkLoop, this);
        graphicsThread = std::thread(&Client::graphicsLoop, this);
    } catch (const std::exception& e) {
        std::cerr << "Erreur lors du démarrage des threads: " << e.what() << std::endl;
        running = false;
        return false;
    }
    
    return true;
}

void Client::stop() {
   running = false;
   
   if (networkThread.joinable()) {
       networkThread.join();
   }
   
   if (graphicsThread.joinable()) {
       graphicsThread.join();
   }
   
   if (clientSocket >= 0) {
       close(clientSocket);
       clientSocket = -1;
   }
   
   cleanupSDL();
   std::cout << "Client arrêté" << std::endl;
}

void Client::graphicsLoop() {
    debugPrint("Thread graphique démarré");
    
    while (running) {
        // Traitez les événements
        SDL_Event event;
        while (SDL_PollEvent(&event)) {
            if (event.type == SDL_QUIT) {
                running = false;
                break;
            }
        }
        
        // Rendu
        render();
        
        // Évitez les mises à jour trop fréquentes
        std::this_thread::sleep_for(std::chrono::milliseconds(16));
    }
    
    debugPrint("Thread graphique terminé");
}

void Client::handleServerMessage() {
    if (clientSocket < 0) {
        std::cerr << "Socket invalide dans handleServerMessage" << std::endl;
        return;
    }

    char buffer[MAX_BUFFER_SIZE];
    int packetType;
    
    // Vérifier si des données sont disponibles
    struct pollfd pfd = {clientSocket, POLLIN, 0};
    int ret = poll(&pfd, 1, 100);
    
    if (ret < 0) {
        std::cerr << "Erreur de poll: " << strerror(errno) << std::endl;
        return;
    }
    
    if (ret == 0) {
        // Timeout - pas de données disponibles
        return;
    }
    
    // Données disponibles, on peut lire
    int dataSize = Protocol::receivePacket(clientSocket, packetType, buffer, MAX_BUFFER_SIZE);
    
    if (dataSize < 0) {
        std::cerr << "Erreur de réception: " << strerror(errno) << std::endl;
        return;
    }
    
    if (dataSize == 0) {
        debugPrint("Connexion fermée proprement");
        running = false;
        return;
    }
    
    std::cout << "Paquet reçu: type=" << packetType << ", taille=" << dataSize << std::endl;
    
    // Traitement des paquets
    switch (packetType) {
        case MAP_DATA: {
            debugPrint("Réception des données de la carte");
            buffer[dataSize] = '\0';  // Assure que la chaîne est terminée
            
            std::lock_guard<std::mutex> lock(gameMutex);
            if (gameMap.fromString(std::string(buffer, dataSize))) {
                debugPrint("Carte chargée avec succès");
            } else {
                debugPrint("Erreur lors du chargement de la carte");
            }
            break;
        }
        
        case GAME_STATE: {
            if (dataSize < (int)sizeof(int) + (int)(sizeof(int) * 5 * MAX_PLAYERS)) {
                debugPrint("Paquet GAME_STATE invalide");
                break;
            }
            
            struct {
                int game_state;
                struct {
                    int player_id;
                    float x;
                    float y;
                    int score;
                    int alive;
                } players[MAX_PLAYERS];
            } data;
            
            std::memcpy(&data, buffer, sizeof(data));
            
            std::lock_guard<std::mutex> lock(gameMutex);
            
            std::cout << "Mise à jour état du jeu: " << data.game_state << std::endl;
            
            // Met à jour l'état du jeu
            gameState = static_cast<GameState>(data.game_state);
            
            // Met à jour les joueurs
            for (int i = 0; i < MAX_PLAYERS; i++) {
                int id = data.players[i].player_id;
                if (id >= 0 && id < MAX_PLAYERS) {
                    players[id].id = id;
                    players[id].position.x = data.players[i].x;
                    players[id].position.y = data.players[i].y;
                    players[id].score = data.players[i].score;
                    players[id].alive = data.players[i].alive != 0;
                    
                    // Détermine mon ID de joueur
                    if (myPlayerId == -1 && id == 0) {
                        myPlayerId = id;
                        debugPrint("Mon ID de joueur: " + std::to_string(myPlayerId));
                    }
                }
            }
            break;
        }
        
        case GAME_OVER: {
            if (dataSize < (int)sizeof(int) + (int)(sizeof(int) * MAX_PLAYERS)) {
                debugPrint("Paquet GAME_OVER invalide");
                break;
            }
            
            struct {
                int winner_id;
                int scores[MAX_PLAYERS];
            } data;
            
            std::memcpy(&data, buffer, sizeof(data));
            
            std::lock_guard<std::mutex> lock(gameMutex);
            
            // Met à jour l'état du jeu
            gameState = OVER;
            
            // Affiche le gagnant
            std::string message = "Fin de partie! ";
            if (data.winner_id >= 0 && data.winner_id < MAX_PLAYERS) {
                message += "Joueur " + std::to_string(data.winner_id) + " a gagné avec " + 
                          std::to_string(data.scores[data.winner_id]) + " points!";
            } else {
                message += "Pas de gagnant.";
            }
            
            std::cout << message << std::endl;
            break;
        }
        
        default:
            debugPrint("Type de paquet non géré: " + std::to_string(packetType));
            break;
    }
}

void Client::sendPlayerPosition(bool jetpackOn) {
   if (myPlayerId < 0 || myPlayerId >= MAX_PLAYERS) {
       return;
   }
   
   Protocol::sendPlayerPosition(clientSocket, myPlayerId, players[myPlayerId].position, jetpackOn);
}

bool Client::initSDL() {
    if (SDL_Init(SDL_INIT_VIDEO) < 0) {
        std::cerr << "SDL Init Error: " << SDL_GetError() << std::endl;
        return false;
    }
    
    std::cout << "SDL Video initialized" << std::endl;
    
    int imgFlags = IMG_INIT_PNG;
    if (!(IMG_Init(imgFlags) & imgFlags)) {
        std::cerr << "SDL_image Init Error: " << IMG_GetError() << std::endl;
        SDL_Quit();
        return false;
    }
    
    std::cout << "SDL_image initialized" << std::endl;
    
    window = SDL_CreateWindow("Jetpack Joyride", 
                             SDL_WINDOWPOS_UNDEFINED, SDL_WINDOWPOS_UNDEFINED,
                             windowWidth, windowHeight, SDL_WINDOW_SHOWN);
    if (!window) {
        std::cerr << "Window creation error: " << SDL_GetError() << std::endl;
        IMG_Quit();
        SDL_Quit();
        return false;
    }
    
    std::cout << "Window created" << std::endl;
    
    // Modifié pour ajouter PRESENTVSYNC
    renderer = SDL_CreateRenderer(window, -1, SDL_RENDERER_ACCELERATED | SDL_RENDERER_PRESENTVSYNC);
    if (!renderer) {
        std::cerr << "Renderer creation error: " << SDL_GetError() << std::endl;
        SDL_DestroyWindow(window);
        IMG_Quit();
        SDL_Quit();
        return false;
    }
    
    std::cout << "Renderer created" << std::endl;
    
    // Force clear la fenêtre immédiatement
    SDL_SetRenderDrawColor(renderer, 0, 0, 0, 255);
    SDL_RenderClear(renderer);
    SDL_RenderPresent(renderer);
    
    // Chargement des textures
    textures["background"] = loadTexture("assets/background.png");
    std::cout << "Background texture loaded: " << (textures["background"] != nullptr) << std::endl;
    
    textures["player"] = loadTexture("assets/player_sprite_sheet.png");
    std::cout << "Player texture loaded: " << (textures["player"] != nullptr) << std::endl;
    
    textures["coin"] = loadTexture("assets/coins_sprite_sheet.png");
    std::cout << "Coin texture loaded: " << (textures["coin"] != nullptr) << std::endl;
    
    textures["electric"] = loadTexture("assets/zapper_sprite_sheet.png");
    std::cout << "Electric texture loaded: " << (textures["electric"] != nullptr) << std::endl;
    
    std::cout << "SDL initialized successfully" << std::endl;
    return true;
}

SDL_Texture* Client::loadTexture(const std::string& path) {
    std::cout << "Loading texture: " << path << std::endl;
    
    if (path.empty()) {
        std::cerr << "Empty path specified for texture" << std::endl;
        return nullptr;
    }
    
    // Vérifier si le fichier existe
    std::ifstream file(path);
    if (!file.good()) {
        std::cerr << "Cannot open texture file: " << path << std::endl;
        return nullptr;
    }
    file.close();
    
    SDL_Surface* surface = IMG_Load(path.c_str());
    if (!surface) {
        std::cerr << "Failed to load image: " << path << ". Error: " << IMG_GetError() << std::endl;
        return nullptr;
    }
    
    SDL_Texture* texture = SDL_CreateTextureFromSurface(renderer, surface);
    SDL_FreeSurface(surface);
    
    if (!texture) {
        std::cerr << "Failed to create texture from " << path << ". Error: " << SDL_GetError() << std::endl;
    }
    
    return texture;
}

void Client::render() {
    std::cout << "Rendering, gameState=" << (int)gameState << std::endl;
    std::lock_guard<std::mutex> lock(gameMutex);
    
    // Fond noir
    SDL_SetRenderDrawColor(renderer, 0, 0, 0, 255);
    SDL_RenderClear(renderer);

    if (gameState == WAITING) {
        std::cout << "Affichage de l'écran d'attente" << std::endl;
        SDL_Rect msgRect = {windowWidth/4, windowHeight/2 - 40, windowWidth/2, 80};
        SDL_SetRenderDrawColor(renderer, 40, 40, 40, 255);
        SDL_RenderFillRect(renderer, &msgRect);
        SDL_SetRenderDrawColor(renderer, 255, 255, 255, 255);
        SDL_RenderDrawRect(renderer, &msgRect);
        SDL_RenderPresent(renderer);
        return;
    }
    
    // Si le jeu est en cours
    // Fond
    if (textures["background"]) {
        SDL_Rect bgRect = {0, 0, windowWidth, windowHeight};
        SDL_RenderCopy(renderer, textures["background"], NULL, &bgRect);
    }
    
    // Calcul du viewport
    int mapWidth = gameMap.getWidth();
    int mapHeight = gameMap.getHeight();
    
    float scaleX = static_cast<float>(windowWidth) / mapWidth;
    float scaleY = static_cast<float>(windowHeight) / mapHeight;
    
    float offsetX = 0;
    if (myPlayerId >= 0 && myPlayerId < MAX_PLAYERS && players[myPlayerId].alive) {
        float playerCenterX = players[myPlayerId].position.x + PLAYER_WIDTH / 2;
        offsetX = playerCenterX - windowWidth / (2 * scaleX);
        
        if (offsetX < 0) offsetX = 0;
        if (offsetX > mapWidth - windowWidth / scaleX) 
            offsetX = mapWidth - windowWidth / scaleX;
    }
    
    // Rendu des éléments
    int visibleStartX = static_cast<int>(offsetX);
    int visibleEndX = static_cast<int>(offsetX + windowWidth / scaleX);
    
    for (int y = 0; y < mapHeight; y++) {
        for (int x = visibleStartX; x < visibleEndX; x++) {
            if (x < 0 || x >= mapWidth || y < 0 || y >= mapHeight) {
                continue;
            }
            
            CellType cell = gameMap.getCell(x, y);
            
            int screenX = static_cast<int>((x - offsetX) * scaleX);
            int screenY = static_cast<int>(y * scaleY);
            int cellWidth = static_cast<int>(scaleX);
            int cellHeight = static_cast<int>(scaleY);
            
            // Show placeholder for cells
            if (cell == COIN) {
                SDL_SetRenderDrawColor(renderer, 255, 255, 0, 255); // Yellow for coins
                SDL_Rect coinRect = {screenX, screenY, cellWidth, cellHeight};
                SDL_RenderFillRect(renderer, &coinRect);
            } else if (cell == ELECTRIC) {
                SDL_SetRenderDrawColor(renderer, 255, 0, 0, 255); // Red for electric
                SDL_Rect elecRect = {screenX, screenY, cellWidth, cellHeight};
                SDL_RenderFillRect(renderer, &elecRect);
            }
        }
    }
    
    // Joueurs
    for (const Player& player : players) {
        if (player.alive) {
            int screenX = static_cast<int>((player.position.x - offsetX) * scaleX);
            int screenY = static_cast<int>(player.position.y * scaleY);
            int playerWidth = static_cast<int>(PLAYER_WIDTH * scaleX);
            int playerHeight = static_cast<int>(PLAYER_HEIGHT * scaleY);
            
            // Placeholder for player
            SDL_SetRenderDrawColor(renderer, 0, 255, 0, 255);
            SDL_Rect playerRect = {screenX, screenY, playerWidth, playerHeight};
            SDL_RenderFillRect(renderer, &playerRect);
        }
    }
    
    // Mise à jour de l'écran
    SDL_RenderPresent(renderer);
}

void Client::networkLoop() {
    debugPrint("Thread réseau démarré");
    
    while (running) {
        try {
            handleServerMessage();
            std::this_thread::sleep_for(std::chrono::milliseconds(50));
        } catch (const std::exception& e) {
            std::cerr << "Exception dans networkLoop: " << e.what() << std::endl;
        }
    }
    
    running = false;
    debugPrint("Thread réseau terminé");
}

void Client::cleanupSDL()
{
   for (auto& pair : textures) {
       if (pair.second) {
           SDL_DestroyTexture(pair.second);
           pair.second = nullptr;
       }
   }
   
   if (renderer) {
       SDL_DestroyRenderer(renderer);
       renderer = nullptr;
   }
   
   if (window) {
       SDL_DestroyWindow(window);
       window = nullptr;
   }
   IMG_Quit();
   SDL_Quit();
}

void Client::renderPlayer(int x, int y, int width, int height, bool jetpackOn) {
    int frameWidth = 32;
    int frameHeight = 32;
    
    int row = jetpackOn ? 0 : 3; 
    
    SDL_Rect srcRect = {
        frameCountPlayer * frameWidth,
        row * frameHeight,
        frameWidth,
        frameHeight
    };
    
    SDL_Rect destRect = {x, y, width, height};
    SDL_RenderCopy(renderer, textures["player"], &srcRect, &destRect);
}

void Client::renderCoin(int x, int y, int width, int height) {
    int frameWidth = 16;
    int frameHeight = 16;
    
    SDL_Rect srcRect = {
        frameCountCoin * frameWidth,
        0,
        frameWidth,
        frameHeight
    };
    
    SDL_Rect destRect = {x, y, width, height};
    SDL_RenderCopy(renderer, textures["coin"], &srcRect, &destRect);
}

void Client::renderZapper(int x, int y, int width, int height) {
    int frameWidth = 16;
    int frameHeight = 32;
    
    SDL_Rect srcRect = {
        frameCountZapper * frameWidth,
        0,
        frameWidth,
        frameHeight
    };
    
    SDL_Rect destRect = {x, y, width, height};
    SDL_RenderCopy(renderer, textures["electric"], &srcRect, &destRect);
}