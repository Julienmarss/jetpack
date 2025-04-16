#include "client.hpp"
#include <chrono>
#include <thread>
#include <iostream>
#include <SDL2/SDL.h>
#include <SDL2/SDL_image.h>
#include <sys/stat.h>
#include <unistd.h>

Client::Client(const std::string& serverIP, int port)
   : serverIP(serverIP), port(port), gameState(WAITING), waitingPlayers(1) {
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

//--------- MODIFICATION DE LA FONCTION GRAPHICSLOOP ---------
void Client::graphicsLoop() {
    debugPrint("Thread graphique démarré");
    int frameCount = 0;
    auto lastDebugTime = std::chrono::steady_clock::now();
    
    while (running) {
        auto startTime = std::chrono::steady_clock::now();

        try {
            // Essayer de rendre l'image
            std::cout << "RENDER ATTEMPT #" << frameCount << std::endl;
            render();
            std::cout << "RENDER SUCCESS #" << frameCount << std::endl;
            
            frameCount++;
            
            // Afficher les statistiques toutes les 5 secondes
            auto currentTime = std::chrono::steady_clock::now();
            auto elapsed = std::chrono::duration_cast<std::chrono::seconds>(currentTime - lastDebugTime).count();
            if (elapsed >= 5) {
                std::cout << "DEBUG GRAPHICS: " << frameCount / elapsed << " FPS (Rendered " 
                          << frameCount << " frames in " << elapsed << " seconds)" << std::endl;
                lastDebugTime = currentTime;
                frameCount = 0;
            }
        } catch (const std::exception& e) {
            std::cerr << "ERREUR CRITIQUE DANS GRAPHICSLOOP: " << e.what() << std::endl;
        } catch (...) {
            std::cerr << "ERREUR CRITIQUE INCONNUE DANS GRAPHICSLOOP" << std::endl;
        }
        
        // Calculer le temps d'attente pour maintenir 60 FPS
        auto endTime = std::chrono::steady_clock::now();
        auto frameDuration = std::chrono::duration_cast<std::chrono::milliseconds>(endTime - startTime);
        auto targetDuration = std::chrono::milliseconds(16); // ~60 FPS
        
        if (frameDuration < targetDuration) {
            std::this_thread::sleep_for(targetDuration - frameDuration);
        }
    }
    
    debugPrint("Thread graphique terminé");
}

//--------- MODIFICATION DE LA FONCTION NETWORKLOOP ---------
void Client::networkLoop() {
    debugPrint("Thread réseau démarré");
    int packetCount = 0;
    auto lastDebugTime = std::chrono::steady_clock::now();
    
    while (running) {
        try {
            std::cout << "NETWORK ATTEMPT #" << packetCount << std::endl;
            handleServerMessage();
            packetCount++;
            
            // Afficher les statistiques toutes les 5 secondes
            auto currentTime = std::chrono::steady_clock::now();
            auto elapsed = std::chrono::duration_cast<std::chrono::seconds>(currentTime - lastDebugTime).count();
            if (elapsed >= 5) {
                std::cout << "DEBUG NETWORK: Processed " << packetCount << " network events in " 
                          << elapsed << " seconds" << std::endl;
                lastDebugTime = currentTime;
                packetCount = 0;
            }
        } catch (const std::exception& e) {
            std::cerr << "ERREUR CRITIQUE DANS NETWORKLOOP: " << e.what() << std::endl;
        } catch (...) {
            std::cerr << "ERREUR CRITIQUE INCONNUE DANS NETWORKLOOP" << std::endl;
        }
            
        std::this_thread::sleep_for(std::chrono::milliseconds(50));
    }
    
    running = false; // S'assurer que l'autre thread se termine aussi
    debugPrint("Thread réseau terminé");
}

//--------- AJOUTEZ CE CODE DANS VOTRE FONCTION START ---------
bool Client::start() {
    if (clientSocket < 0) {
        std::cerr << "Non connecté au serveur" << std::endl;
        return false;
    }
    
    std::cout << "====== DÉMARRAGE CLIENT ======" << std::endl;
    if (!initSDL()) {
        std::cerr << "Erreur lors de l'initialisation de SDL" << std::endl;
        return false;
    }
    
    // Test simple de rendu avant de lancer les threads
    std::cout << "Test de rendu initial..." << std::endl;
    SDL_SetRenderDrawColor(renderer, 0, 0, 255, 255);
    SDL_RenderClear(renderer);
    
    if (font) {
        std::cout << "Affichage d'un texte de test..." << std::endl;
        SDL_Color white = {255, 255, 255, 255};
        SDL_Surface* textSurface = TTF_RenderText_Solid(font, "TEST RENDU", white);
        if (textSurface) {
            SDL_Texture* textTexture = SDL_CreateTextureFromSurface(renderer, textSurface);
            SDL_Rect textRect = {windowWidth/2 - 100, windowHeight/2 - 20, 200, 40};
            SDL_RenderCopy(renderer, textTexture, NULL, &textRect);
            SDL_DestroyTexture(textTexture);
            SDL_FreeSurface(textSurface);
            std::cout << "Texte affiché" << std::endl;
        } else {
            std::cerr << "Erreur lors du rendu du texte de test: " << TTF_GetError() << std::endl;
        }
    } else {
        std::cout << "Pas de police chargée, dessin d'un rectangle à la place" << std::endl;
        SDL_Rect rect = {windowWidth/2 - 100, windowHeight/2 - 20, 200, 40};
        SDL_SetRenderDrawColor(renderer, 255, 255, 255, 255);
        SDL_RenderFillRect(renderer, &rect);
    }
    
    SDL_RenderPresent(renderer);
    std::cout << "Test de rendu initial terminé" << std::endl;
    
    running = true;
    
    try {
        std::cout << "Démarrage du thread réseau..." << std::endl;
        networkThread = std::thread(&Client::networkLoop, this);
        std::cout << "Thread réseau démarré avec succès" << std::endl;
        
        std::cout << "Démarrage du thread graphique..." << std::endl;
        graphicsThread = std::thread(&Client::graphicsLoop, this);
        std::cout << "Thread graphique démarré avec succès" << std::endl;
    } catch (const std::exception& e) {
        std::cerr << "Erreur critique lors du démarrage des threads: " << e.what() << std::endl;
        running = false;
        return false;
    }
    
    std::cout << "Client démarré avec succès" << std::endl;
    std::cout << "====== FIN DÉMARRAGE CLIENT ======" << std::endl;
    return true;
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
        return;
    }
    
    std::cout << "Paquet reçu: type=" << packetType << ", taille=" << dataSize << std::endl;
    
    // Traitement des paquets
    switch (packetType) {
        case MAP_DATA: {
            debugPrint("Réception des données de la carte");
            buffer[dataSize] = '\0';  // Assure que la chaîne est terminée
            
            std::string mapData(buffer, dataSize);
            std::cout << "=== DÉBUT CONTENU CARTE ===" << std::endl;
            std::cout << "Taille des données: " << dataSize << " octets" << std::endl;
            std::cout << "Premières 100 caractères: " << mapData.substr(0, 100) << std::endl;
            std::cout << "=== FIN CONTENU CARTE ===" << std::endl;
            
            std::lock_guard<std::mutex> lock(gameMutex);
            if (gameMap.fromString(mapData)) {
                debugPrint("Carte chargée avec succès");
                std::cout << "Dimensions: " << gameMap.getWidth() << "x" << gameMap.getHeight() << std::endl;
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
            
            // Mémoriser l'ancien état pour le débogage
            GameState oldState = gameState;
            
            // Met à jour l'état du jeu
            gameState = static_cast<GameState>(data.game_state);
            
            // Log détaillé du changement d'état
            std::cout << "État du jeu mis à jour: " << (int)gameState 
                      << " (ancien: " << (int)oldState 
                      << ", WAITING=" << WAITING 
                      << ", RUNNING=" << RUNNING 
                      << ", OVER=" << OVER << ")" << std::endl;
            
            // Met à jour les joueurs
            for (int i = 0; i < MAX_PLAYERS; i++) {
                int id = data.players[i].player_id;
                if (id >= 0 && id < MAX_PLAYERS) {
                    std::cout << "Mise à jour joueur " << id 
                              << " à position (" << data.players[i].x << "," << data.players[i].y 
                              << "), score: " << data.players[i].score 
                              << ", alive: " << data.players[i].alive << std::endl;
                              
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
            
            // Enregistrer l'ancien état
            GameState oldState = gameState;
            
            // Met à jour l'état du jeu
            gameState = OVER;
            
            // Log détaillé du changement d'état
            std::cout << "État du jeu mis à jour vers OVER (ancien: " << (int)oldState << ")" << std::endl;
            
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
        
        case WAITING_STATUS: {
            if (dataSize < (int)sizeof(int)) {
                debugPrint("Paquet WAITING_STATUS invalide");
                break;
            }
        
            int connectedCount;
            std::memcpy(&connectedCount, buffer, sizeof(int));
            debugPrint("En attente de joueurs: " + std::to_string(connectedCount) + "/2");
        
            std::lock_guard<std::mutex> lock(gameMutex);
            
            // Enregistrer l'ancien état
            GameState oldState = gameState;
            
            waitingPlayers = connectedCount;
            gameState = WAITING;
            
            // Log détaillé du changement d'état
            std::cout << "État du jeu mis à jour vers WAITING (ancien: " << (int)oldState << ")" << std::endl;
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
    std::cout << "======== DÉBUT INITIALISATION SDL ========" << std::endl;
    
    // Vérification des dossiers et fichiers
    std::cout << "Vérification du dossier assets..." << std::endl;
    const char* assetsDir = "assets";
    if (access(assetsDir, F_OK) != 0) {
        std::cerr << "ERROR: Le dossier assets n'existe pas!" << std::endl;
        std::cout << "Tentative de création du dossier assets..." << std::endl;
        if (mkdir(assetsDir, 0755) != 0) {
            std::cerr << "ERROR: Impossible de créer le dossier assets: " << strerror(errno) << std::endl;
        }
    } else {
        std::cout << "OK: Le dossier assets existe" << std::endl;
    }
    
    const char* files[] = {
        "assets/background.png",
        "assets/player_sprite_sheet.png",
        "assets/coins_sprite_sheet.png",
        "assets/zapper_sprite_sheet.png",
        "assets/jetpack_font.ttf"
    };
    
    for (const char* file : files) {
        std::cout << "Vérification de " << file << "... ";
        if (access(file, F_OK) != 0) {
            std::cerr << "MANQUANT!" << std::endl;
        } else {
            std::cout << "OK" << std::endl;
        }
    }
    
    // Initialisation SDL
    std::cout << "Initialisation de SDL..." << std::endl;
    if (SDL_Init(SDL_INIT_VIDEO | SDL_INIT_EVENTS) < 0) {
        std::cerr << "ERROR: SDL Init: " << SDL_GetError() << std::endl;
        return false;
    }
    std::cout << "SDL Video initialized" << std::endl;
    
    // Initialisation SDL_image
    std::cout << "Initialisation de SDL_image..." << std::endl;
    int imgFlags = IMG_INIT_PNG;
    if (!(IMG_Init(imgFlags) & imgFlags)) {
        std::cerr << "ERROR: SDL_image Init: " << IMG_GetError() << std::endl;
        SDL_Quit();
        return false;
    }
    std::cout << "SDL_image initialized" << std::endl;
    
    // Création de la fenêtre
    std::cout << "Création de la fenêtre..." << std::endl;
    window = SDL_CreateWindow("Jetpack Joyride", 
                             SDL_WINDOWPOS_UNDEFINED, SDL_WINDOWPOS_UNDEFINED,
                             windowWidth, windowHeight, SDL_WINDOW_SHOWN);
    if (!window) {
        std::cerr << "ERROR: Window creation: " << SDL_GetError() << std::endl;
        IMG_Quit();
        SDL_Quit();
        return false;
    }
    std::cout << "Window created" << std::endl;
    
    // Création du renderer
    std::cout << "Création du renderer..." << std::endl;
    renderer = SDL_CreateRenderer(window, -1, SDL_RENDERER_ACCELERATED | SDL_RENDERER_PRESENTVSYNC);
    if (!renderer) {
        std::cerr << "ERROR: Renderer creation: " << SDL_GetError() << std::endl;
        SDL_DestroyWindow(window);
        IMG_Quit();
        SDL_Quit();
        return false;
    }
    std::cout << "Renderer created" << std::endl;
    
    // Initialisation SDL_ttf et chargement de la police
    std::cout << "Initialisation de SDL_ttf..." << std::endl;
    if (TTF_Init() == -1) {
        std::cerr << "ERROR: SDL_ttf Init: " << TTF_GetError() << std::endl;
    } else {
        std::cout << "SDL_ttf initialized" << std::endl;
        
        std::cout << "Chargement de la police jetpack_font.ttf..." << std::endl;
        font = TTF_OpenFont("assets/jetpack_font.ttf", 28);
        if (!font) {
            std::cerr << "ERROR: Chargement police: " << TTF_GetError() << std::endl;
            // Utiliser une police par défaut du système comme fallback
            std::cout << "Tentative avec une police système..." << std::endl;
            font = TTF_OpenFont("/usr/share/fonts/truetype/dejavu/DejaVuSans.ttf", 28);
            if (!font) {
                std::cerr << "ERROR: Police fallback également échouée" << std::endl;
            } else {
                std::cout << "Police fallback chargée" << std::endl;
            }
        } else {
            std::cout << "Police chargée avec succès" << std::endl;
        }
    }
    
    // Force clear la fenêtre immédiatement
    std::cout << "Premier rendu pour effacer l'écran..." << std::endl;
    SDL_SetRenderDrawColor(renderer, 0, 0, 0, 255);
    SDL_RenderClear(renderer);
    SDL_RenderPresent(renderer);
    std::cout << "Premier rendu effectué" << std::endl;
    
    // Chargement des textures
    std::cout << "Chargement des textures..." << std::endl;
    
    textures["background"] = loadTexture("assets/background.png");
    std::cout << "Background texture loaded: " << (textures["background"] != nullptr) << std::endl;
    
    textures["player"] = loadTexture("assets/player_sprite_sheet.png");
    std::cout << "Player texture loaded: " << (textures["player"] != nullptr) << std::endl;
    
    textures["coin"] = loadTexture("assets/coins_sprite_sheet.png");
    std::cout << "Coin texture loaded: " << (textures["coin"] != nullptr) << std::endl;
    
    textures["electric"] = loadTexture("assets/zapper_sprite_sheet.png");
    std::cout << "Electric texture loaded: " << (textures["electric"] != nullptr) << std::endl;
    
    // Test de dessin d'un simple rectangle pour vérifier que le renderer fonctionne
    std::cout << "Test de dessin d'un rectangle..." << std::endl;
    SDL_SetRenderDrawColor(renderer, 255, 0, 0, 255);
    SDL_Rect testRect = {windowWidth / 2 - 50, windowHeight / 2 - 50, 100, 100};
    SDL_RenderFillRect(renderer, &testRect);
    SDL_RenderPresent(renderer);
    std::cout << "Rectangle de test dessiné" << std::endl;
    
    std::cout << "SDL initialized successfully" << std::endl;
    std::cout << "======== FIN INITIALISATION SDL ========" << std::endl;
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
    std::cout << "===== DÉBUT RENDU =====" << std::endl;
    std::cout << "État du jeu: " << gameState << std::endl;
    
    // Vérifier que le renderer existe
    if (!renderer) {
        std::cerr << "ERREUR: Renderer null dans render()" << std::endl;
        return;
    }

    std::lock_guard<std::mutex> lock(gameMutex);
    std::cout << "Mutex verrouillé" << std::endl;

    // Fond noir
    std::cout << "Effacement de l'écran..." << std::endl;
    SDL_SetRenderDrawColor(renderer, 0, 0, 0, 255);
    SDL_RenderClear(renderer);
    std::cout << "Écran effacé" << std::endl;

    // Déssin selon l'état du jeu
    if (gameState == WAITING) {
        std::cout << "Affichage de l'écran d'attente..." << std::endl;

        // Test du rendu d'un rectangle
        std::cout << "Dessin d'un rectangle de test..." << std::endl;
        SDL_SetRenderDrawColor(renderer, 255, 0, 0, 255);
        SDL_Rect testRect = {windowWidth / 4, windowHeight / 4, windowWidth / 2, windowHeight / 2};
        SDL_RenderFillRect(renderer, &testRect);
        std::cout << "Rectangle dessiné" << std::endl;

        // Affichage du texte d'attente
        if (font) {
            std::cout << "Affichage du texte d'attente..." << std::endl;
            std::string message = "En attente de joueurs (" + std::to_string(waitingPlayers) + "/2)";
            SDL_Color white = {255, 255, 255, 255};
            SDL_Surface* textSurface = TTF_RenderText_Solid(font, message.c_str(), white);
            if (textSurface) {
                SDL_Texture* textTexture = SDL_CreateTextureFromSurface(renderer, textSurface);
                SDL_Rect textRect = {
                    windowWidth / 2 - textSurface->w / 2,
                    windowHeight / 2 - textSurface->h / 2,
                    textSurface->w,
                    textSurface->h
                };
                SDL_RenderCopy(renderer, textTexture, NULL, &textRect);
                SDL_DestroyTexture(textTexture);
                SDL_FreeSurface(textSurface);
                std::cout << "Texte affiché" << std::endl;
            } else {
                std::cerr << "Erreur création surface texte: " << TTF_GetError() << std::endl;
            }
        } else {
            std::cerr << "Police non chargée, impossible d'afficher le texte" << std::endl;
            // Dessin d'un message fallback
            std::cout << "Dessin d'un rectangle pour remplacer le texte..." << std::endl;
            SDL_SetRenderDrawColor(renderer, 255, 255, 255, 255);
            SDL_Rect textRect = {windowWidth / 2 - 100, windowHeight / 2 - 15, 200, 30};
            SDL_RenderFillRect(renderer, &textRect);
            std::cout << "Rectangle fallback dessiné" << std::endl;
        }
    } else {
        // État de jeu: RUNNING ou OVER
        std::cout << "Rendu du jeu (état " << gameState << ")..." << std::endl;
        
        // Fond avec texture de background
        if (textures["background"]) {
            std::cout << "Affichage du fond..." << std::endl;
            SDL_Rect bgRect = {0, 0, windowWidth, windowHeight};
            SDL_RenderCopy(renderer, textures["background"], NULL, &bgRect);
            std::cout << "Fond affiché" << std::endl;
        } else {
            std::cerr << "Texture de fond manquante" << std::endl;
            // Dessin d'un fond simple
            SDL_SetRenderDrawColor(renderer, 50, 50, 100, 255);
            SDL_RenderFillRect(renderer, NULL);
        }

        // Calcul du viewport
        std::cout << "Calcul du viewport..." << std::endl;
        int mapWidth = gameMap.getWidth();
        int mapHeight = gameMap.getHeight();
        std::cout << "Dimensions carte: " << mapWidth << "x" << mapHeight << std::endl;

        float scaleX = static_cast<float>(windowWidth) / mapWidth;
        float scaleY = static_cast<float>(windowHeight) / mapHeight;
        std::cout << "Échelles: " << scaleX << "x" << scaleY << std::endl;

        float offsetX = 0;
        if (myPlayerId >= 0 && myPlayerId < MAX_PLAYERS && players[myPlayerId].alive) {
            float playerCenterX = players[myPlayerId].position.x + PLAYER_WIDTH / 2;
            offsetX = playerCenterX - windowWidth / (2 * scaleX);

            if (offsetX < 0) offsetX = 0;
            if (offsetX > mapWidth - windowWidth / scaleX)
                offsetX = mapWidth - windowWidth / scaleX;
                
            std::cout << "Offset X calculé: " << offsetX << std::endl;
        } else {
            std::cout << "Pas de joueur actif pour calculer l'offset" << std::endl;
        }

        // Animation des sprites
        int currentTime = SDL_GetTicks();
        if (currentTime > lastFrameTime + 100) {
            frameCountPlayer = (frameCountPlayer + 1) % 4;
            frameCountCoin = (frameCountCoin + 1) % 8;
            frameCountZapper = (frameCountZapper + 1) % 4;
            lastFrameTime = currentTime;
            std::cout << "Animation mise à jour - frames: " 
                     << frameCountPlayer << "/" << frameCountCoin << "/" << frameCountZapper << std::endl;
        }

        // Rendu des éléments de la carte
        std::cout << "Rendu des éléments de la carte..." << std::endl;
        int visibleStartX = static_cast<int>(offsetX);
        int visibleEndX = static_cast<int>(offsetX + windowWidth / scaleX);
        std::cout << "Zone visible: X de " << visibleStartX << " à " << visibleEndX << std::endl;

        int elementCount = 0;
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

                if (cell == COIN) {
                    elementCount++;
                    if (textures["coin"]) {
                        renderCoin(screenX, screenY, cellWidth, cellHeight);
                    } else {
                        SDL_SetRenderDrawColor(renderer, 255, 255, 0, 255);
                        SDL_Rect coinRect = {screenX, screenY, cellWidth, cellHeight};
                        SDL_RenderFillRect(renderer, &coinRect);
                    }
                } 
                else if (cell == ELECTRIC) {
                    elementCount++;
                    if (textures["electric"]) {
                        renderZapper(screenX, screenY, cellWidth, cellHeight * 2);
                    } else {
                        SDL_SetRenderDrawColor(renderer, 255, 0, 0, 255);
                        SDL_Rect elecRect = {screenX, screenY, cellWidth, cellHeight};
                        SDL_RenderFillRect(renderer, &elecRect);
                    }
                }
            }
        }
        std::cout << elementCount << " éléments rendus" << std::endl;

        std::cout << "Rendu des joueurs..." << std::endl;
        int playerCount = 0;
        for (int i = 0; i < MAX_PLAYERS; i++) {
            const Player& player = players[i];
            std::cout << "Joueur " << i << " - Position: (" << player.position.x 
                      << "," << player.position.y << ") Alive: " << player.alive 
                      << " Score: " << player.score << std::endl;
                      
            if (player.alive) {
                playerCount++;
                int screenX = static_cast<int>((player.position.x - offsetX) * scaleX);
                int screenY = static_cast<int>(player.position.y * scaleY);
                int playerWidth = static_cast<int>(PLAYER_WIDTH * scaleX);
                int playerHeight = static_cast<int>(PLAYER_HEIGHT * scaleY);
        
                std::cout << "Joueur " << i << " à l'écran: (" << screenX << "," << screenY 
                          << ") taille: " << playerWidth << "x" << playerHeight << std::endl;
        
                if (textures["player"]) {
                    renderPlayer(screenX, screenY, playerWidth, playerHeight, player.jetpackOn);
                } else {
                    SDL_SetRenderDrawColor(renderer, 0, 255, 0, 255);
                    SDL_Rect playerRect = {screenX, screenY, playerWidth, playerHeight};
                    SDL_RenderFillRect(renderer, &playerRect);
                }
            }
        }
        std::cout << playerCount << " joueurs rendus" << std::endl;
        
        // Scores en haut de l'écran
        if (font) {
            std::cout << "Affichage des scores..." << std::endl;
            for (int i = 0; i < MAX_PLAYERS; i++) {
                std::string scoreText = "Joueur " + std::to_string(i) + ": " + std::to_string(players[i].score);
                SDL_Color textColor = {255, 255, 255, 255};
                if (!players[i].alive) {
                    textColor = {128, 128, 128, 255};
                }
                
                SDL_Surface* textSurface = TTF_RenderText_Solid(font, scoreText.c_str(), textColor);
                if (textSurface) {
                    SDL_Texture* textTexture = SDL_CreateTextureFromSurface(renderer, textSurface);
                    SDL_Rect textRect = {10, 10 + i * 30, textSurface->w, textSurface->h};
                    SDL_RenderCopy(renderer, textTexture, NULL, &textRect);
                    SDL_DestroyTexture(textTexture);
                    SDL_FreeSurface(textSurface);
                }
            }
        }
        
        // Message de fin de partie
        if (gameState == OVER && font) {
            std::cout << "Affichage du message de fin..." << std::endl;
            std::string message = "Fin de partie!";
            SDL_Color white = {255, 255, 255, 255};
            SDL_Surface* textSurface = TTF_RenderText_Solid(font, message.c_str(), white);
            if (textSurface) {
                SDL_Texture* textTexture = SDL_CreateTextureFromSurface(renderer, textSurface);
                SDL_Rect textRect = {
                    windowWidth / 2 - textSurface->w / 2,
                    windowHeight / 2 - textSurface->h / 2,
                    textSurface->w,
                    textSurface->h
                };
                SDL_RenderCopy(renderer, textTexture, NULL, &textRect);
                SDL_DestroyTexture(textTexture);
                SDL_FreeSurface(textSurface);
            }
        }
    }

    // Mise à jour de l'écran
    std::cout << "Mise à jour de l'écran..." << std::endl;
    SDL_RenderPresent(renderer);
    std::cout << "Écran mis à jour" << std::endl;
    
    // RETIRER CETTE LIGNE: renderTestPattern();
    
    std::cout << "===== FIN RENDU =====" << std::endl;
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
   if (font) {
    TTF_CloseFont(font);
    font = nullptr;
   }
   TTF_Quit();
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


void Client::renderTestPattern() {
    if (!renderer) {
        std::cerr << "Renderer null!" << std::endl;
        return;
    }
    
    // Fond de couleur unie
    SDL_SetRenderDrawColor(renderer, 0, 0, 128, 255);
    SDL_RenderClear(renderer);
    
    // Dessiner une grille
    SDL_SetRenderDrawColor(renderer, 255, 255, 255, 255);
    for (int x = 0; x < windowWidth; x += 50) {
        SDL_RenderDrawLine(renderer, x, 0, x, windowHeight);
    }
    for (int y = 0; y < windowHeight; y += 50) {
        SDL_RenderDrawLine(renderer, 0, y, windowWidth, y);
    }
    
    // Dessiner des rectangles de couleur
    SDL_Rect rects[4] = {
        {100, 100, 100, 100},
        {300, 100, 100, 100},
        {100, 300, 100, 100},
        {300, 300, 100, 100}
    };
    
    SDL_Color colors[4] = {
        {255, 0, 0, 255},
        {0, 255, 0, 255},
        {0, 0, 255, 255},
        {255, 255, 0, 255}
    };
    
    for (int i = 0; i < 4; i++) {
        SDL_SetRenderDrawColor(renderer, colors[i].r, colors[i].g, colors[i].b, colors[i].a);
        SDL_RenderFillRect(renderer, &rects[i]);
    }
    
    // Si possible, afficher du texte
    if (font) {
        SDL_Color white = {255, 255, 255, 255};
        SDL_Surface* textSurface = TTF_RenderText_Solid(font, "Test Pattern", white);
        if (textSurface) {
            SDL_Texture* textTexture = SDL_CreateTextureFromSurface(renderer, textSurface);
            SDL_Rect textRect = {
                windowWidth / 2 - textSurface->w / 2,
                50,
                textSurface->w,
                textSurface->h
            };
            SDL_RenderCopy(renderer, textTexture, NULL, &textRect);
            SDL_DestroyTexture(textTexture);
            SDL_FreeSurface(textSurface);
        }
    }
    
    // Tester le chargement et l'affichage des textures
    for (const auto& pair : textures) {
        std::cout << "Texture '" << pair.first << "' chargée: " << (pair.second != nullptr) << std::endl;
    }
    
    // Si texture de fond existe, l'afficher dans le coin inférieur droit
    if (textures["background"]) {
        SDL_Rect bgRect = {
            windowWidth - 200, 
            windowHeight - 150, 
            200, 
            150
        };
        SDL_RenderCopy(renderer, textures["background"], NULL, &bgRect);
        std::cout << "Background affiché dans le coin inférieur droit" << std::endl;
    }
    
    // Si texture de joueur existe, l'afficher au centre
    if (textures["player"]) {
        SDL_Rect srcRect = {0, 0, 32, 32}; // Première frame
        SDL_Rect destRect = {
            windowWidth / 2 - 50,
            windowHeight / 2 - 50,
            100,
            100
        };
        SDL_RenderCopy(renderer, textures["player"], &srcRect, &destRect);
        std::cout << "Sprite joueur affiché au centre" << std::endl;
    }
    
    SDL_RenderPresent(renderer);
}