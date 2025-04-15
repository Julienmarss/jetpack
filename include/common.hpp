/*
** EPITECH PROJECT, 2025
** Tek 2 B-NWP-400-LIL-4-1-jetpack-julien.mars
** File description:
** common.hpp
*/

#ifndef COMMON_HPP
#define COMMON_HPP

#include <iostream>
#include <string>
#include <vector>
#include <array>
#include <memory>
#include <thread>
#include <mutex>
#include <atomic>
#include <arpa/inet.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <poll.h>
#include <unistd.h>
#include <cstring>
#include <fstream>
#include <sstream>

// Constantes
#define MAX_BUFFER_SIZE 4096
#define MAX_PLAYERS 2
#define POLL_TIMEOUT 50 // millisecondes

// Constantes du jeu
#define PLAYER_WIDTH 32
#define PLAYER_HEIGHT 32
#define COIN_SIZE 16
#define ELECTRIC_SIZE 16
#define GRAVITY 0.5f
#define JETPACK_FORCE 0.8f
#define HORIZONTAL_SPEED 5.0f

enum CellType {
    EMPTY = 0,
    COIN = 1,
    ELECTRIC = 2
};

enum GameState {
    WAITING = 0,
    RUNNING = 1,
    OVER = 2
};

enum PacketType {
    CONNECT = 0,
    READY = 1,
    MAP_DATA = 2,
    PLAYER_POS = 3,
    GAME_STATE = 4,
    GAME_OVER = 5
};

class Vector2 {
public:
    float x = 0.0f;
    float y = 0.0f;

    Vector2() = default;
    Vector2(float _x, float _y) : x(_x), y(_y) {}
};

class Player {
public:
    int id = -1;
    Vector2 position;
    float velocityY = 0.0f;
    bool jetpackOn = false;
    int score = 0;
    bool alive = true;
    std::string name;
};

extern bool debug_mode;
void debugPrint(const std::string& message);

#endif