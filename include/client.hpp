/*
 * * EPITECH PROJECT, 2025
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
#include <SFML/Audio.hpp>
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
    void updateCamera(float deltaTime);

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

    int currentPlayerFrame = 0;
    int currentCoinFrame = 0;
    int currentZapperFrame = 0;
    float cameraX = 0.0f;
    sf::Clock animationClock;

    sf::Sound jetpackSound;
    sf::Sound coinSound;
    sf::Sound zapperSound;
    sf::Music backgroundMusic;
    std::map<std::string, sf::SoundBuffer> soundBuffers;

    int windowWidth = 800;
    int windowHeight = 600;
    int waitingPlayers = 1;
    bool jetpackActive = false;

    void networkLoop();
    void graphicsLoop();
    void simulateLocalPlayer(float deltaTime);
    void handleServerMessage();
    void render();

    bool loadAssets();
    void initWindow();

    void renderPlayer(int x, int y, int width, int height, bool jetpackOn);
    void renderCoin(int x, int y, int width, int height);
    void renderZapper(int x, int y, int width, int height);
    void handleInput();
};

#endif /* CLIENT_HPP */
