#include "client.hpp"
#include <chrono>
#include <thread>
#include <iostream>

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
   networkThread = std::thread(&Client::networkLoop, this);
   graphicsThread = std::thread(&Client::graphicsLoop, this);
   
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

void Client::networkLoop() {
   debugPrint("Thread réseau démarré");
   
   bool idAssigned = false;
   
   while (running) {
       handleServerMessage();
       if (idAssigned) {
           std::lock_guard<std::mutex> lock(gameMutex);
           sendPlayerPosition(players[myPlayerId].jetpackOn);
       } else if (myPlayerId >= 0 && myPlayerId < MAX_PLAYERS) {
           idAssigned = true;
       }
       std::this_thread::sleep_for(std::chrono::milliseconds(16));
   }
   
   debugPrint("Thread réseau terminé");
}

void Client::graphicsLoop() {
   debugPrint("Thread graphique démarré");
   
   bool quit = false;
   SDL_Event event;
   
   Uint32 lastTime = SDL_GetTicks();
   
   while (running && !quit) {
       while (SDL_PollEvent(&event)) {
           if (event.type == SDL_QUIT) {
               quit = true;
               running = false;
           } else if (event.type == SDL_KEYDOWN) {
               if (event.key.keysym.sym == SDLK_ESCAPE) {
                   quit = true;
                   running = false;
               } else if (event.key.keysym.sym == SDLK_SPACE) {
                   std::lock_guard<std::mutex> lock(gameMutex);
                   if (myPlayerId >= 0 && myPlayerId < MAX_PLAYERS) {
                       players[myPlayerId].jetpackOn = true;
                   }
               }
           } else if (event.type == SDL_KEYUP) {
               if (event.key.keysym.sym == SDLK_SPACE) {
                   std::lock_guard<std::mutex> lock(gameMutex);
                   if (myPlayerId >= 0 && myPlayerId < MAX_PLAYERS) {
                       players[myPlayerId].jetpackOn = false;
                   }
               }
           }
       }
       render();
       Uint32 currentTime = SDL_GetTicks();
       Uint32 elapsed = currentTime - lastTime;
       if (elapsed < 16) {  // ~60 FPS
           SDL_Delay(16 - elapsed);
       }
       lastTime = SDL_GetTicks();
   }
   debugPrint("Thread graphique terminé");
   running = false;
}

void Client::handleServerMessage() {
   char buffer[MAX_BUFFER_SIZE];
   int packetType;
   
   int dataSize = Protocol::receivePacket(clientSocket, packetType, buffer, MAX_BUFFER_SIZE);
   
   if (dataSize <= 0) {
       running = false;
       return;
   }
   
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
           
           gameState = static_cast<GameState>(data.game_state);
           
           for (int i = 0; i < MAX_PLAYERS; i++) {
               int id = data.players[i].player_id;
               if (id >= 0 && id < MAX_PLAYERS) {
                   players[id].id = id;
                   players[id].position.x = data.players[i].x;
                   players[id].position.y = data.players[i].y;
                   players[id].score = data.players[i].score;
                   players[id].alive = data.players[i].alive != 0;
                   
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
           gameState = OVER;
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
       std::cerr << "Erreur lors de l'initialisation de SDL: " << SDL_GetError() << std::endl;
       return false;
   }
   
   int imgFlags = IMG_INIT_PNG;
   if (!(IMG_Init(imgFlags) & imgFlags)) {
       std::cerr << "Erreur lors de l'initialisation de SDL_image: " << IMG_GetError() << std::endl;
       SDL_Quit();
       return false;
   }
   
   window = SDL_CreateWindow("Jetpack Joyride", SDL_WINDOWPOS_UNDEFINED, SDL_WINDOWPOS_UNDEFINED,
                           windowWidth, windowHeight, SDL_WINDOW_SHOWN);
   if (!window) {
       std::cerr << "Erreur lors de la création de la fenêtre: " << SDL_GetError() << std::endl;
       IMG_Quit();
       SDL_Quit();
       return false;
   }
   
   renderer = SDL_CreateRenderer(window, -1, SDL_RENDERER_ACCELERATED);
   if (!renderer) {
       std::cerr << "Erreur lors de la création du renderer: " << SDL_GetError() << std::endl;
       SDL_DestroyWindow(window);
       IMG_Quit();
       SDL_Quit();
       return false;
   }
   
   textures["background"] = loadTexture("assets/background.png");
   textures["player"] = loadTexture("assets/player_sprite_sheet.png");
   textures["coin"] = loadTexture("assets/coins_sprite_sheet.png");
   textures["electric"] = loadTexture("assets/zapper_sprite_sheet.png");
   
   if (!textures["background"] || !textures["player"] || !textures["coin"] || !textures["electric"]) {
       std::cerr << "Erreur lors du chargement des textures" << std::endl;
       cleanupSDL();
       return false;
   }
   
   std::cout << "SDL initialisé avec succès" << std::endl;
   return true;
}

SDL_Texture* Client::loadTexture(const std::string& path) {
   SDL_Surface* surface = IMG_Load(path.c_str());
   if (!surface) {
       std::cerr << "Impossible de charger l'image " << path << ": " << IMG_GetError() << std::endl;
       return nullptr;
   }
   
   SDL_Texture* texture = SDL_CreateTextureFromSurface(renderer, surface);
   if (!texture) {
       std::cerr << "Impossible de créer la texture à partir de " << path << ": " << SDL_GetError() << std::endl;
   }
   
   SDL_FreeSurface(surface);
   
   return texture;
}

void Client::render() {
   std::lock_guard<std::mutex> lock(gameMutex);
   
   SDL_SetRenderDrawColor(renderer, 0, 0, 0, 255);
   SDL_RenderClear(renderer);
   
   if (textures["background"]) {
       SDL_Rect bgRect = {0, 0, windowWidth, windowHeight};
       SDL_RenderCopy(renderer, textures["background"], NULL, &bgRect);
   }
   
   int mapWidth = gameMap.getWidth();
   int mapHeight = gameMap.getHeight();
   
   float scaleX = static_cast<float>(windowWidth) / mapWidth;
   float scaleY = static_cast<float>(windowHeight) / mapHeight;
   
   float offsetX = 0;
   if (myPlayerId >= 0 && myPlayerId < MAX_PLAYERS && players[myPlayerId].alive) {
       float playerCenterX = players[myPlayerId].position.x + PLAYER_WIDTH / 2;
       offsetX = playerCenterX - windowWidth / (2 * scaleX);
       
       if (offsetX < 0) offsetX = 0;
       if (offsetX > mapWidth - windowWidth / scaleX) offsetX = mapWidth - windowWidth / scaleX;
   }
   
   int visibleStartX = static_cast<int>(offsetX);
   int visibleEndX = static_cast<int>(offsetX + windowWidth / scaleX);
   
   for (int y = 0; y < mapHeight; y++) {
       for (int x = visibleStartX; x < visibleEndX; x++) {
           if (x < 0 || x >= mapWidth || y < 0 || y >= mapHeight) {
               continue;
           }
           
           CellType cell = gameMap.getCell(x, y);
           
           SDL_Rect destRect = {
               static_cast<int>((x - offsetX) * scaleX),
               static_cast<int>(y * scaleY),
               static_cast<int>(scaleX),
               static_cast<int>(scaleY)
           };
           
           if (cell == COIN && textures["coin"]) {
               SDL_RenderCopy(renderer, textures["coin"], NULL, &destRect);
           } else if (cell == ELECTRIC && textures["electric"]) {
               SDL_RenderCopy(renderer, textures["electric"], NULL, &destRect);
           }
       }
   }
   
   for (const Player& player : players) {
       if (player.alive && textures["player"]) {
           SDL_Rect playerRect = {
               static_cast<int>((player.position.x - offsetX) * scaleX),
               static_cast<int>(player.position.y * scaleY),
               static_cast<int>(PLAYER_WIDTH * scaleX),
               static_cast<int>(PLAYER_HEIGHT * scaleY)
           };
           
           SDL_RenderCopy(renderer, textures["player"], NULL, &playerRect);
       }
   }
   
   //faire un système pour afficher le score avec les coins (le faire refresh et le save aussi pour faire un système de "meilleur score")
   
   SDL_RenderPresent(renderer);
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