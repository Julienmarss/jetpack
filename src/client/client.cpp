#include "client.hpp"
#include <chrono>
#include <thread>
#include <iostream>
#include <math.h>

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

    initWindow();
    if (!loadAssets()) {
        std::cerr << "Erreur lors du chargement des assets" << std::endl;
        return false;
    }

    running = true;

    try {
        networkThread = std::thread(&Client::networkLoop, this);
        graphicsLoop(); 
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
    if (!font.loadFromFile("assets/jetpack_font.ttf")) {
        std::cerr << "Erreur lors du chargement de la police" << std::endl;
        return false;
    }
    
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
        
        textures[name].setSmooth(false);
        sprites[name] = sf::Sprite(textures[name]);
        
        debugPrint("Texture " + name + " chargée: " + 
                  std::to_string(textures[name].getSize().x) + "x" + 
                  std::to_string(textures[name].getSize().y));
    }

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
    
    jetpackSound.setBuffer(soundBuffers["jetpack_loop"]);
    jetpackSound.setLoop(true);
    coinSound.setBuffer(soundBuffers["coin_pickup"]);
    zapperSound.setBuffer(soundBuffers["zapper_hit"]);
    
    if (!backgroundMusic.openFromFile("assets/theme.ogg")) {
        std::cerr << "Erreur lors du chargement de la musique de fond" << std::endl;
        return false;
    }
    
    backgroundMusic.setLoop(true);
    backgroundMusic.setVolume(50);
    backgroundMusic.play();
    
    return true;
}

void Client::graphicsLoop() {
    debugPrint("Thread graphique démarré");

    sf::Clock frameClock;
    float previousTime = frameClock.getElapsedTime().asSeconds();

    while (running && window.isOpen()) {
        handleInput();

        float currentTime = frameClock.getElapsedTime().asSeconds();
        float deltaTime = currentTime - previousTime;

        if (deltaTime < 0.005f || std::isnan(deltaTime)) deltaTime = 0.016f;
        if (deltaTime > 0.05f) deltaTime = 0.016f; // Corrigé le point-virgule manquant

        previousTime = currentTime;

        debugPrint("[GRAPHICS] deltaTime: " + std::to_string(deltaTime));

        // Mettre à jour la caméra pour suivre le joueur
        updateCamera(deltaTime);
        
        // Le serveur gère toute la physique, pas besoin de simulateLocalPlayer
        
        render();

        if (animationClock.getElapsedTime().asSeconds() > 0.1f) {
            animationClock.restart();
            currentPlayerFrame = (currentPlayerFrame + 1) % 4;
            currentCoinFrame = (currentCoinFrame + 1) % 6;
            currentZapperFrame = (currentZapperFrame + 1) % 4;

            debugPrint("[ANIMATION] Frame joueur: " + std::to_string(currentPlayerFrame) +
                       ", coin: " + std::to_string(currentCoinFrame) +
                       ", zapper: " + std::to_string(currentZapperFrame));
        }

        std::this_thread::sleep_for(std::chrono::milliseconds(16));
    }

    debugPrint("Thread graphique terminé");
}

void Client::simulateLocalPlayer(float deltaTime) {
    if (deltaTime <= 0.001f || std::isnan(deltaTime)) return;
    
    if (myPlayerId < 0 || myPlayerId >= MAX_PLAYERS || gameState != RUNNING) {
        return;
    }
    
    std::lock_guard<std::mutex> lock(gameMutex);
    Player& me = players[myPlayerId];
    
    if (!me.alive) return;

    // Notifier le serveur que le jetpack est activé/désactivé
    // Le serveur appliquera la physique complète
    sendPlayerPosition(jetpackActive);
    
    // Application locale simplifiée de la physique pour réduire l'impression de latence
    // mais sans contredire le serveur
    if (jetpackActive) {
        // Simuler une montée douce pour un feedback immédiat
        float estimatedVelocity = -10.0f; // Valeur arbitraire pour effet visuel
        float estimatedNewY = me.position.y + estimatedVelocity * deltaTime;
        
        // Ne pas monter au-dessus du plafond
        if (estimatedNewY < 0) {
            estimatedNewY = 0;
        }
        
        // Application locale limitée (sera écrasée par le serveur à la prochaine mise à jour)
        me.velocityY = estimatedVelocity;
        me.position.y = estimatedNewY;
        
        debugPrint("Simulation locale: jetpack ON, vY=" + std::to_string(me.velocityY));
    } else {
        // Simuler une descente douce pour un feedback immédiat
        float estimatedVelocity = 10.0f; // Valeur arbitraire pour effet visuel
        float estimatedNewY = me.position.y + estimatedVelocity * deltaTime;
        
        // Ne pas descendre en-dessous du sol
        const float CELL_SIZE = 32.0f;
        float floorY = (gameMap.getHeight() - 2) * CELL_SIZE - PLAYER_HEIGHT;
        if (estimatedNewY > floorY) {
            estimatedNewY = floorY;
            estimatedVelocity = 0.0f;
        }
        
        // Application locale limitée (sera écrasée par le serveur à la prochaine mise à jour)
        me.velocityY = estimatedVelocity;
        me.position.y = estimatedNewY;
        
        debugPrint("Simulation locale: jetpack OFF, vY=" + std::to_string(me.velocityY));
    }
}


void Client::updateCamera(float deltaTime) {
    if (gameState == RUNNING && myPlayerId >= 0 && myPlayerId < MAX_PLAYERS) {
        // Obtenir la position horizontale du joueur
        float playerX = players[myPlayerId].position.x;
        
        // Garder le joueur à environ 30% de la largeur de l'écran à gauche
        float targetCameraX = playerX - windowWidth * 0.3f;
        
        // Lisser le mouvement de la caméra
        float cameraSpeed = 5.0f;
        cameraX += (targetCameraX - cameraX) * cameraSpeed * deltaTime;
        
        // Limites de la caméra
        if (cameraX < 0) {
            cameraX = 0;
        }
        
        const float CELL_SIZE = 32.0f;
        float maxCameraX = gameMap.getWidth() * CELL_SIZE - windowWidth;
        if (maxCameraX > 0 && cameraX > maxCameraX) {
            cameraX = maxCameraX;
        }
        
        debugPrint("[CAMERA] Position: " + std::to_string(cameraX) + 
                   ", Joueur: " + std::to_string(playerX));
    }
}

void Client::sendPlayerPosition(bool jetpackOn) {
    if (myPlayerId < 0 || myPlayerId >= MAX_PLAYERS) {
        return;
    }

    if (clientSocket < 0 || gameState != RUNNING) {
        return;
    }
    
    // Données à envoyer sans structure
    int player_id = myPlayerId;
    float x = players[myPlayerId].position.x;
    float y = players[myPlayerId].position.y;
    int jetpack_on = jetpackOn ? 1 : 0;  // Utiliser la vraie valeur, pas toujours 1
    
    // Construction du buffer manuellement
    char buffer[16]; // 4 + 4 + 4 + 4 bytes
    int offset = 0;
    
    // Ajouter l'ID du joueur
    std::memcpy(buffer + offset, &player_id, sizeof(int));
    offset += sizeof(int);
    
    // Ajouter la position X
    std::memcpy(buffer + offset, &x, sizeof(float));
    offset += sizeof(float);
    
    // Ajouter la position Y
    std::memcpy(buffer + offset, &y, sizeof(float));
    offset += sizeof(float);
    
    // Ajouter l'état du jetpack
    std::memcpy(buffer + offset, &jetpack_on, sizeof(int));
    
    // Envoyer le paquet
    Protocol::sendPacket(clientSocket, PLAYER_POS, buffer, 16);
    
    debugPrint("[CLIENT] Envoi PLAYER_POS: id=" + std::to_string(player_id) + 
              ", pos=(" + std::to_string(x) + "," + std::to_string(y) + ")" +
              ", jetpack=" + std::to_string(jetpack_on));
    debugPrint("[TEST] FORCE JETPACK ACTIVÉ POUR TEST - Envoi: id=" + 
                std::to_string(player_id) + ", jetpack=" + std::to_string(jetpack_on));
}

void Client::handleServerMessage() {
    if (clientSocket < 0) {
        std::cerr << "Socket invalide dans handleServerMessage" << std::endl;
        return;
    }

    char buffer[MAX_BUFFER_SIZE];
    int packetType;

    struct pollfd pfd = {clientSocket, POLLIN, 0};
    int ret = poll(&pfd, 1, 100);

    if (ret < 0) {
        std::cerr << "Erreur de poll: " << strerror(errno) << std::endl;
        return;
    }

    if (ret == 0) return;

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

    switch (packetType) {
        case ASSIGN_PLAYER_ID: {
            if (dataSize < (int)sizeof(int)) {
                debugPrint("Paquet ASSIGN_PLAYER_ID invalide");
                break;
            }

            int assignedId;
            std::memcpy(&assignedId, buffer, sizeof(int));

            std::lock_guard<std::mutex> lock(gameMutex);
            myPlayerId = assignedId;
            debugPrint("[INIT] Mon playerId assigné par le serveur: " + std::to_string(myPlayerId));
            break;
        }

        case MAP_DATA: {
            buffer[dataSize] = '\0';
            std::lock_guard<std::mutex> lock(gameMutex);
            std::string mapString(buffer, dataSize);
            debugPrint("[MAP] Données reçues:\n" + mapString);

            if (gameMap.fromString(mapString)) {
                debugPrint("Carte chargée avec succès");
            } else {
                debugPrint("Erreur lors du chargement de la carte");
            }
            break;
        }

        case GAME_STATE: {
            struct PlayerData {
                int player_id;
                float x, y;
                int score;
                int alive;
                int jetpackOn;
            } playerData[MAX_PLAYERS];
        
            int game_state;
            std::memcpy(&game_state, buffer, sizeof(int));
        
            for (int i = 0; i < MAX_PLAYERS; i++) {
                std::memcpy(&playerData[i], buffer + sizeof(int) + i * sizeof(PlayerData), sizeof(PlayerData));
            }
        
            std::lock_guard<std::mutex> lock(gameMutex);
            gameState = static_cast<GameState>(game_state);
        
            for (int i = 0; i < MAX_PLAYERS; i++) {
                int id = playerData[i].player_id;
                if (id >= 0 && id < MAX_PLAYERS) {
                    players[id].id = id;
                    
                    // Mettre à jour les états, positions, etc.
                    players[id].position = {playerData[i].x, playerData[i].y};
                    players[id].score = playerData[i].score;
                    players[id].alive = playerData[i].alive != 0;
                    
                    // Pour les autres joueurs, prendre en compte l'état de leur jetpack
                    if (id != myPlayerId) {
                        players[id].jetpackOn = playerData[i].jetpackOn != 0;
                    }
                    
                    if (myPlayerId == -1) {
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

void Client::renderPlayer(int x, int y, int, int, bool jetpackOn) {
    const int FRAME_WIDTH = 135;
    const int FRAME_HEIGHT = 135;
    const int SPRITES_PER_ROW = 4;
    int row = jetpackOn ? 1 : 0;
    int col = currentPlayerFrame % SPRITES_PER_ROW;
    int sourceX = col * FRAME_WIDTH;
    int sourceY = row * FRAME_HEIGHT;

    sf::Sprite playerSprite = sprites["player_sprite_sheet"];
    playerSprite.setTextureRect(sf::IntRect(sourceX, sourceY, FRAME_WIDTH, FRAME_HEIGHT));
    playerSprite.setOrigin(FRAME_WIDTH / 2.f, FRAME_HEIGHT);
    playerSprite.setScale(0.5f, 0.5f);
    playerSprite.setPosition(static_cast<float>(x), static_cast<float>(y));
    window.draw(playerSprite);
}


void Client::renderCoin(int x, int y, int width, int height) {
    const int COIN_WIDTH = 180;
    const int COIN_HEIGHT = 180;
    int col = currentCoinFrame % 6;
    int sourceX = col * COIN_WIDTH;
    int sourceY = 0;
    
    sf::Sprite coinSprite = sprites["coins_sprite_sheet"];
    coinSprite.setTextureRect(sf::IntRect(sourceX, sourceY, COIN_WIDTH, COIN_HEIGHT));
    coinSprite.setPosition(x, y);
    coinSprite.setScale(
        static_cast<float>(width) / COIN_WIDTH,
        static_cast<float>(height) / COIN_HEIGHT
    );
    
    window.draw(coinSprite);
}

void Client::renderZapper(int x, int y, int displayWidth, int displayHeight)
{
    const int SPRITE_WIDTH = 100;
    const int SPRITE_HEIGHT = 128;
    const int NUM_FRAMES = 5;
    int col = currentZapperFrame % NUM_FRAMES;
    int sourceX = col * SPRITE_WIDTH;
    int sourceY = 0;

    debugPrint("Découpe du zapper : (" + std::to_string(sourceX) + ", " + std::to_string(sourceY) + ")");
    sf::Sprite zapperSprite = sprites["zapper_sprite_sheet"];
    zapperSprite.setTextureRect(sf::IntRect(sourceX, sourceY, SPRITE_WIDTH, SPRITE_HEIGHT));
    float finalHeight = static_cast<float>(displayHeight - 30);
    float scaleX = static_cast<float>(displayWidth) / SPRITE_WIDTH;
    float scaleY = finalHeight / SPRITE_HEIGHT;
    zapperSprite.setPosition(static_cast<float>(x), static_cast<float>(y));
    zapperSprite.setScale(scaleX, scaleY);
    window.draw(zapperSprite);
}

void Client::render() {
    window.clear(sf::Color::Black);

    if (gameState == WAITING) {
        sf::Text waitingText;
        waitingText.setFont(font);
        waitingText.setString("En attente de joueurs (" + std::to_string(waitingPlayers) + "/2)");
        waitingText.setCharacterSize(24);
        waitingText.setFillColor(sf::Color::White);
        waitingText.setPosition(windowWidth / 2 - waitingText.getGlobalBounds().width / 2, windowHeight / 2 - 12);
        window.draw(waitingText);
        window.display();
        return;
    }

    const float CELL_SIZE = 32.0f;
    // Ajuster cette valeur pour positionner la carte plus haut ou plus bas
    const float MAP_OFFSET_Y = windowHeight - (gameMap.getHeight() * CELL_SIZE) - 50.0f;
    
    sf::View gameView(sf::FloatRect(cameraX, 0, windowWidth, windowHeight));
    window.setView(gameView);

    sprites["background"].setPosition(cameraX, 0);
    sprites["background"].setScale(
        float(windowWidth) / textures["background"].getSize().x,
        float(windowHeight) / textures["background"].getSize().y
    );
    window.draw(sprites["background"]);

    int visibleStartX = static_cast<int>(cameraX / CELL_SIZE);
    int visibleEndX = static_cast<int>((cameraX + windowWidth) / CELL_SIZE) + 1;

    for (int y = 0; y < gameMap.getHeight(); y++) {
        for (int x = visibleStartX; x < visibleEndX && x < gameMap.getWidth(); x++) {
            if (x < 0 || y < 0) continue;
            CellType cell = gameMap.getCell(x, y);
            float screenX = x * CELL_SIZE;
            float screenY = y * CELL_SIZE + MAP_OFFSET_Y;
    
            if (cell == COIN) {
                renderCoin(screenX, screenY, CELL_SIZE, CELL_SIZE);
            } else if (cell == ELECTRIC) {
                renderZapper(screenX, screenY - (128 - 30), 32, 128);
            }
        }
    }
    
    for (int i = 0; i < MAX_PLAYERS; i++) {
        if (players[i].alive) {
            // Calculer la position à l'écran pour le rendu
            float screenX = players[i].position.x;
            // Position verticale ajustée avec l'offset
            float screenY = players[i].position.y + MAP_OFFSET_Y;
            
            // Utiliser l'état du jetpack local pour notre joueur
            bool isJetpackActive = (i == myPlayerId) ? jetpackActive : players[i].jetpackOn;
            renderPlayer(screenX, screenY, 0, 0, isJetpackActive);

            debugPrint("Rendu du joueur " + std::to_string(i) + " à la position (" +
                       std::to_string(screenX) + "," + std::to_string(screenY) + ") jetpack: " +
                       std::to_string(isJetpackActive));
        }
    }

    window.setView(window.getDefaultView());

    for (int i = 0; i < MAX_PLAYERS; i++) {
        sf::Text scoreText;
        scoreText.setFont(font);
        scoreText.setString("PLAYER " + std::to_string(i + 1) + " " + std::to_string(players[i].score));
        scoreText.setCharacterSize(18);
        scoreText.setFillColor(sf::Color::White);
        scoreText.setPosition(10, 10 + i * 30);
        window.draw(scoreText);
    }

    if (gameState == OVER) {
        sf::Text endText;
        endText.setFont(font);
        endText.setString("Fin de partie!");
        endText.setCharacterSize(36);
        endText.setFillColor(sf::Color::White);
        endText.setPosition(windowWidth / 2 - endText.getGlobalBounds().width / 2, windowHeight / 2 - 18);
        window.draw(endText);
    }

    window.display();
}

void Client::handleInput() {
    sf::Event event;
    while (window.pollEvent(event)) {
        if (event.type == sf::Event::Closed) {
            window.close();
            running = false;
        }
        else if (event.type == sf::Event::KeyPressed && event.key.code == sf::Keyboard::Space) {
            if (!jetpackActive && gameState == RUNNING) {
                jetpackActive = true;
                
                // Sons pour le feedback immédiat
                sf::Sound startSound;
                startSound.setBuffer(soundBuffers["jetpack_start"]);
                startSound.play();
                jetpackSound.play();
                
                // Envoyer immédiatement l'état au serveur
                sendPlayerPosition(true);
                
                debugPrint("Jetpack activé par l'utilisateur");
            }
        }
        else if (event.type == sf::Event::KeyReleased && event.key.code == sf::Keyboard::Space) {
            if (jetpackActive) {
                jetpackActive = false;
                
                // Sons pour le feedback immédiat
                jetpackSound.stop();
                sf::Sound stopSound;
                stopSound.setBuffer(soundBuffers["jetpack_stop"]);
                stopSound.play();
                
                // Envoyer immédiatement l'état au serveur
                sendPlayerPosition(false);
                
                debugPrint("Jetpack désactivé par l'utilisateur");
            }
        }
    }
    
    // Vérification continue de la touche espace pour plus de réactivité
    if (gameState == RUNNING && myPlayerId >= 0 && players[myPlayerId].alive) {
        bool spacePressed = sf::Keyboard::isKeyPressed(sf::Keyboard::Space);
        
        // Si l'état a changé entre deux événements
        if (spacePressed != jetpackActive) {
            jetpackActive = spacePressed;
            
            // Mise à jour des sons
            if (jetpackActive) {
                if (jetpackSound.getStatus() != sf::Sound::Playing) {
                    sf::Sound startSound;
                    startSound.setBuffer(soundBuffers["jetpack_start"]);
                    startSound.play();
                    jetpackSound.play();
                }
            } else {
                if (jetpackSound.getStatus() == sf::Sound::Playing) {
                    jetpackSound.stop();
                    sf::Sound stopSound;
                    stopSound.setBuffer(soundBuffers["jetpack_stop"]);
                    stopSound.play();
                }
            }
            
            // Envoi immédiat au serveur
            sendPlayerPosition(jetpackActive);
            
            debugPrint("État du jetpack mis à jour en continu: " + std::to_string(jetpackActive));
        }
    }
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