#include "client.hpp"
#include <chrono>
#include <thread>
#include <iostream>

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

bool Client::start() {
    if (clientSocket < 0) {
        std::cerr << "Non connecté au serveur" << std::endl;
        return false;
    }
    
    // Initialisation de la fenêtre et chargement des assets
    initWindow();
    if (!loadAssets()) {
        std::cerr << "Erreur lors du chargement des assets" << std::endl;
        return false;
    }
    
    running = true;
    
    try {
        networkThread = std::thread(&Client::networkLoop, this);
        // Exécuter la boucle graphique dans le thread principal
        graphicsLoop();
    } catch (const std::exception& e) {
        std::cerr << "Erreur: " << e.what() << std::endl;
        running = false;
        return false;
    }
    
    return true;
}

// Modifiez également stop() pour ne pas essayer de joindre graphicsThread
void Client::stop() {
    running = false;
    
    if (networkThread.joinable()) {
        networkThread.join();
    }
    
    // Ne pas essayer de joindre graphicsThread car il n'est pas utilisé
    
    if (clientSocket >= 0) {
        close(clientSocket);
        clientSocket = -1;
    }
    
    // Arrêter la musique
    backgroundMusic.stop();
    
    if (window.isOpen()) {
        window.close();
    }
    
    std::cout << "Client arrêté" << std::endl;
}

void Client::initWindow() {
    sf::ContextSettings settings;
    settings.attributeFlags = sf::ContextSettings::Default;
    window.create(sf::VideoMode(windowWidth, windowHeight), "Jetpack Game", sf::Style::Default, settings);
}

bool Client::loadAssets() {
    // Chargement de la police
    if (!font.loadFromFile("assets/jetpack_font.ttf")) {
        std::cerr << "Erreur lors du chargement de la police" << std::endl;
        return false;
    }
    
    // Chargement des textures
    const std::vector<std::string> textureFiles = {
        "background",
        "player_sprite_sheet",
        "coins_sprite_sheet",
        "zapper_sprite_sheet"
    };
    
    for (const auto& name : textureFiles) {
        textures[name] = sf::Texture();
        if (!textures[name].loadFromFile("assets/" + name + ".png")) {
            std::cerr << "Erreur lors du chargement de la texture: " << name << std::endl;
            return false;
        }
        
        textures[name].setSmooth(false);  // Pour conserver le style pixel art
        sprites[name] = sf::Sprite(textures[name]);
    }
    
    // Chargement des sons
    const std::vector<std::pair<std::string, std::string>> soundFiles = {
        {"jetpack_start", "jetpack_start.wav"},
        {"jetpack_loop", "jetpack_lp.wav"},
        {"jetpack_stop", "jetpack_stop.wav"},
        {"coin_pickup", "coin_pickup_1.wav"},
        {"zapper_hit", "dud_zapper_pop.wav"}
    };
    
    for (const auto& sound : soundFiles) {
        soundBuffers[sound.first] = sf::SoundBuffer();
        if (!soundBuffers[sound.first].loadFromFile("assets/" + sound.second)) {
            std::cerr << "Erreur lors du chargement du son: " << sound.second << std::endl;
            return false;
        }
    }
    
    // Configuration des sons
    jetpackSound.setBuffer(soundBuffers["jetpack_loop"]);
    jetpackSound.setLoop(true);
    
    coinSound.setBuffer(soundBuffers["coin_pickup"]);
    zapperSound.setBuffer(soundBuffers["zapper_hit"]);
    
    // Chargement de la musique de fond
    static bool musicStarted = false;
    if (!backgroundMusic.openFromFile("assets/theme.ogg")) {
        std::cerr << "Erreur lors du chargement de la musique de fond" << std::endl;
        return false;
    }
    
    if (!musicStarted) {
        backgroundMusic.setLoop(true);
        backgroundMusic.setVolume(50);
        backgroundMusic.play();
        musicStarted = true;
    }
    
    return true;
}

void Client::graphicsLoop() {
    debugPrint("Thread graphique démarré");
    
    sf::Clock frameClock;
    
    while (running && window.isOpen()) {
        // Gestion des événements
        handleInput();
        
        // Rendu
        render();
        
        // Animation timing
        sf::Time frameTime = frameClock.restart();
        if (animationClock.getElapsedTime().asMilliseconds() > 100) {
            animationClock.restart();
            currentPlayerFrame = (currentPlayerFrame + 1) % 4;
            currentCoinFrame = (currentCoinFrame + 1) % 6;
            currentZapperFrame = (currentZapperFrame + 1) % 4;
        }
        
        // Limiter la fréquence de mise à jour
        std::this_thread::sleep_for(std::chrono::milliseconds(16)); // ~60 FPS
    }
    
    debugPrint("Thread graphique terminé");
}

void Client::handleInput() {
    sf::Event event;
    while (window.pollEvent(event)) {
        if (event.type == sf::Event::Closed) {
            window.close();
            running = false;
        } else if (event.type == sf::Event::KeyPressed) {
            if (event.key.code == sf::Keyboard::Space && !jetpackActive) {
                jetpackActive = true;
                sendPlayerPosition(true);
                
                // Jouer le son de démarrage du jetpack
                sf::Sound startSound;
                startSound.setBuffer(soundBuffers["jetpack_start"]);
                startSound.play();
                
                // Démarrer le son en continu
                jetpackSound.play();
            }
        } else if (event.type == sf::Event::KeyReleased) {
            if (event.key.code == sf::Keyboard::Space && jetpackActive) {
                jetpackActive = false;
                sendPlayerPosition(false);
                
                // Arrêter le son du jetpack
                jetpackSound.stop();
                
                // Jouer le son d'arrêt
                sf::Sound stopSound;
                stopSound.setBuffer(soundBuffers["jetpack_stop"]);
                stopSound.play();
            }
        }
    }
}

void Client::sendPlayerPosition(bool jetpackOn) {
    if (myPlayerId < 0 || myPlayerId >= MAX_PLAYERS) {
        return;
    }
    
    std::lock_guard<std::mutex> lock(gameMutex);
    if (clientSocket >= 0 && gameState == RUNNING) {
        players[myPlayerId].jetpackOn = jetpackOn;
        Protocol::sendPlayerPosition(clientSocket, myPlayerId, players[myPlayerId].position, jetpackOn);
    }
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
        running = false;
        return;
    }
    
    if (dataSize == 0) {
        debugPrint("Connexion fermée par le serveur");
        running = false;
        return;
    }
    
    debugPrint("Paquet reçu: type=" + std::to_string(packetType) + ", taille=" + std::to_string(dataSize));
    
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
            
            // Met à jour l'état du jeu
            GameState previousState = gameState;
            gameState = static_cast<GameState>(data.game_state);
            
            if (previousState == WAITING && gameState == RUNNING) {
                debugPrint("La partie commence!");
                
                // Jouer un son de démarrage
                if (backgroundMusic.getStatus() != sf::Music::Playing) {
                    backgroundMusic.play();
                }
            }
            
            // Met à jour les joueurs
            for (int i = 0; i < MAX_PLAYERS; i++) {
                int id = data.players[i].player_id;
                if (id >= 0 && id < MAX_PLAYERS) {
                    // Vérifier si le joueur a collecté une pièce
                    int previousScore = players[id].score;
                    
                    players[id].id = id;
                    players[id].position.x = data.players[i].x;
                    players[id].position.y = data.players[i].y;
                    players[id].score = data.players[i].score;
                    players[id].alive = data.players[i].alive != 0;
                    
                    // Jouer un son si le joueur a collecté une pièce
                    if (id == myPlayerId && players[id].score > previousScore) {
                        coinSound.play();
                    }
                    
                    // Détermine mon ID de joueur si pas encore défini
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
                message += "Joueur " + std::to_string(data.winner_id + 1) + " a gagné avec " + 
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
            waitingPlayers = connectedCount;
            gameState = WAITING;
            break;
        }

        default:
            debugPrint("Type de paquet non géré: " + std::to_string(packetType));
            break;
    }
}

void Client::renderPlayer(int x, int y, int width, int height, bool jetpackOn) {
    sf::Sprite playerSprite = sprites["player_sprite_sheet"];
    
    // Sélectionner le bon frame dans la spritesheet
    int frameWidth = 32;  // Largeur d'un frame dans la texture
    int frameHeight = 32; // Hauteur d'un frame dans la texture
    
    int frameOffsetX = currentPlayerFrame * frameWidth;
    int frameOffsetY = jetpackOn ? frameHeight : 0;  // Ligne 1 pour jetpack actif
    
    playerSprite.setTextureRect(sf::IntRect(frameOffsetX, frameOffsetY, frameWidth, frameHeight));
    playerSprite.setPosition(x, y);
    playerSprite.setScale(
        static_cast<float>(width) / frameWidth,
        static_cast<float>(height) / frameHeight
    );
    
    window.draw(playerSprite);
}

void Client::renderCoin(int x, int y, int width, int height) {
    sf::Sprite coinSprite = sprites["coins_sprite_sheet"];
    
    // Sélectionner le bon frame dans la spritesheet
    int frameWidth = 16;  // Largeur d'un coin dans la texture
    int frameHeight = 16; // Hauteur d'un coin dans la texture
    
    coinSprite.setTextureRect(sf::IntRect(currentCoinFrame * frameWidth, 0, frameWidth, frameHeight));
    coinSprite.setPosition(x, y);
    coinSprite.setScale(
        static_cast<float>(width) / frameWidth,
        static_cast<float>(height) / frameHeight
    );
    
    window.draw(coinSprite);
}

void Client::renderZapper(int x, int y, int width, int height) {
    sf::Sprite zapperSprite = sprites["zapper_sprite_sheet"];
    
    // Sélectionner le bon frame dans la spritesheet
    int frameWidth = 16;  // Largeur d'un zapper dans la texture
    int frameHeight = 32; // Hauteur d'un zapper dans la texture (plus long)
    
    zapperSprite.setTextureRect(sf::IntRect(currentZapperFrame * frameWidth, 0, frameWidth, frameHeight));
    zapperSprite.setPosition(x, y);
    zapperSprite.setScale(
        static_cast<float>(width) / frameWidth,
        static_cast<float>(height) / frameHeight
    );
    
    window.draw(zapperSprite);
}

void Client::render() {
    window.clear(sf::Color::Black);
    
    if (gameState == WAITING) {
        sf::Text text;
        text.setFont(font);
        text.setString("En attente de joueurs (" + std::to_string(waitingPlayers) + "/2)");
        text.setCharacterSize(24);
        text.setFillColor(sf::Color::White);
        text.setPosition(windowWidth/2 - text.getGlobalBounds().width/2, windowHeight/2 - 12);
        
        window.draw(text);
        window.display();
        return;
    }
    
    // Fond
    sprites["background"].setPosition(0, 0);
    sprites["background"].setScale(
        float(windowWidth) / textures["background"].getSize().x,
        float(windowHeight) / textures["background"].getSize().y
    );
    window.draw(sprites["background"]);
    
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
    
    // Rendu des éléments de la carte
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
            
            if (cell == COIN) {
                renderCoin(screenX, screenY, cellWidth, cellHeight);
            } 
            else if (cell == ELECTRIC) {
                renderZapper(screenX, screenY, cellWidth, cellHeight * 2);
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
            
            renderPlayer(screenX, screenY, playerWidth, playerHeight, player.jetpackOn);
        }
    }
    
    // Scores
    for (int i = 0; i < MAX_PLAYERS; i++) {
        sf::Text scoreText;
        scoreText.setFont(font);
        scoreText.setString("Player " + std::to_string(i+1) + ": " + std::to_string(players[i].score));
        scoreText.setCharacterSize(18);
        scoreText.setFillColor(players[i].alive ? sf::Color::White : sf::Color(128, 128, 128));
        scoreText.setPosition(10, 10 + i * 30);
        window.draw(scoreText);
    }
    
    // Message de fin
    if (gameState == OVER) {
        sf::Text endText;
        endText.setFont(font);
        endText.setString("Fin de partie!");
        endText.setCharacterSize(36);
        endText.setFillColor(sf::Color::White);
        endText.setPosition(
            windowWidth/2 - endText.getGlobalBounds().width/2, 
            windowHeight/2 - 18
        );
        window.draw(endText);
    }
    
    window.display();
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