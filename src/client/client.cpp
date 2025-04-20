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
        if (deltaTime > 0.05f) deltaTime = 0.016f

        previousTime = currentTime;

        debugPrint("[GRAPHICS] deltaTime: " + std::to_string(deltaTime));

        updateCamera(deltaTime);
        simulateLocalPlayer(deltaTime);
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

    if (myPlayerId >= 0 && myPlayerId < MAX_PLAYERS && gameState == RUNNING) {
        Player& me = players[myPlayerId];

        if (jetpackActive) {
            me.velocityY -= JETPACK_FORCE;
            if (me.velocityY < MAX_UP_SPEED)
                me.velocityY = MAX_UP_SPEED;
        } else {
            me.velocityY += GRAVITY;
            if (me.velocityY > MAX_DOWN_SPEED)
                me.velocityY = MAX_DOWN_SPEED;
        }

        me.position.y += me.velocityY;

        const float CELL_SIZE = 32.0f;
        float floorY = (gameMap.getHeight() - 2) * CELL_SIZE - PLAYER_HEIGHT;

        if (me.position.y < 0) {
            me.position.y = 0;
            me.velocityY = 0;
        } else if (me.position.y > floorY) {
            me.position.y = floorY;
            me.velocityY = 0;
        }

        me.position.x += HORIZONTAL_SPEED * deltaTime;

        sendPlayerPosition(jetpackActive);
    }
}


void Client::updateCamera(float deltaTime) {
    if (gameState == RUNNING && myPlayerId >= 0 && myPlayerId < MAX_PLAYERS) {
        const float CELL_SIZE = 32.0f;
        float playerScreenX = players[myPlayerId].position.x; // position.x est déjà en pixels
        
        // La caméra doit garder le joueur proche du bord gauche de l'écran (environ 20%)
        float targetCameraX = playerScreenX - windowWidth * 0.2f;
        
        // Interpolation douce de la caméra pour éviter les mouvements brusques
        float cameraSpeed = 10.0f;
        cameraX += (targetCameraX - cameraX) * cameraSpeed * deltaTime;
        
        // Empêcher la caméra de dépasser le début de la carte
        if (cameraX < 0) {
            cameraX = 0;
        }
        
        // Empêcher la caméra de dépasser la fin de la carte
        float maxCameraX = gameMap.getWidth() * CELL_SIZE - windowWidth;
        if (cameraX > maxCameraX && maxCameraX > 0) {
            cameraX = maxCameraX;
        }
        
        debugPrint("Camera X: " + std::to_string(cameraX));
    }
}

void Client::sendPlayerPosition(bool jetpackOn) {
    if (myPlayerId < 0 || myPlayerId >= MAX_PLAYERS)
        return;

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
                int jetpackOn; // ✅ On ajoute ce champ
            } playerData[MAX_PLAYERS];
        
            int game_state;
            std::memcpy(&game_state, buffer, sizeof(int));
        
            size_t expectedSize = sizeof(int) + MAX_PLAYERS * sizeof(PlayerData);
            if (dataSize < (int)expectedSize) {
                debugPrint("Paquet GAME_STATE invalide (taille incorrecte)");
                break;
            }
        
            std::memcpy(&playerData, buffer + sizeof(int), sizeof(playerData));
        
            std::lock_guard<std::mutex> lock(gameMutex);
            gameState = static_cast<GameState>(game_state);
        
            for (int i = 0; i < MAX_PLAYERS; i++) {
                int id = playerData[i].player_id;
                if (id >= 0 && id < MAX_PLAYERS) {
                    players[id].id = id;
        
                    if (id != myPlayerId) {
                        players[id].position = {playerData[i].x, playerData[i].y};
                        players[id].jetpackOn = playerData[i].jetpackOn != 0; // ✅ On récupère l'état
                    }
        
                    players[id].score = playerData[i].score;
                    players[id].alive = playerData[i].alive != 0;
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
    const int COIN_WIDTH = 32;
    const int COIN_HEIGHT = 32;
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
    const int SPRITE_WIDTH = 32;
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
    const float MAP_OFFSET_Y = windowHeight - (CELL_SIZE * gameMap.getHeight());
    const float FLOOR_Y = (gameMap.getHeight() - 1) * CELL_SIZE;

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
                debugPrint("[RENDER] Coin détecté à (" + std::to_string(x) + "," + std::to_string(y) + ")");
                renderCoin(screenX, screenY, CELL_SIZE, CELL_SIZE);
            } else if (cell == ELECTRIC) {
                debugPrint("[RENDER] Zapper détecté à (" + std::to_string(x) + "," + std::to_string(y) + ")");
                renderZapper(screenX, screenY - (128 - 30), 32, 128);
            }
        }
    }
    

    for (int i = 0; i < MAX_PLAYERS; i++) {
        if (players[i].alive) {
            float screenX = players[i].position.x;
            float screenY = std::min(players[i].position.y + MAP_OFFSET_Y, FLOOR_Y + MAP_OFFSET_Y - PLAYER_HEIGHT + 10);
            renderPlayer(screenX, screenY, 0, 0, players[i].jetpackOn);

            debugPrint("Rendu du joueur " + std::to_string(i) + " à la position (" +
                       std::to_string(screenX) + "," + std::to_string(screenY) + ")");
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
            if (!jetpackActive) {
                jetpackActive = true;
                sendPlayerPosition(true); // Ensure the state is sent immediately
                sf::Sound startSound;
                startSound.setBuffer(soundBuffers["jetpack_start"]);
                startSound.play();
                jetpackSound.play();
            }
        }
        else if (event.type == sf::Event::KeyReleased && event.key.code == sf::Keyboard::Space) {
            if (jetpackActive) {
                jetpackActive = false;
                sendPlayerPosition(false); // Ensure the state is sent immediately
                jetpackSound.stop();
                sf::Sound stopSound;
                stopSound.setBuffer(soundBuffers["jetpack_stop"]);
                stopSound.play();
            }
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