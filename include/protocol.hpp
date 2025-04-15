/*
** EPITECH PROJECT, 2025
** Tek 2 B-NWP-400-LIL-4-1-jetpack-julien.mars
** File description:
** protocol.hpp
*/

#ifndef PROTOCOL_HPP
#define PROTOCOL_HPP

#include "common.hpp"
#include "map.hpp"

struct PacketHeader {
    int type;
    int length;
};

class Protocol {
public:
    static bool sendPacket(int socket, int packetType, const void* data = nullptr, int dataLength = 0);
    static int receivePacket(int socket, int& packetType, void* buffer, int bufferSize);
    static bool sendMap(int socket, const Map& map);
    static bool receiveMap(int socket, Map& map);
    static bool sendPlayerPosition(int socket, int playerId, const Vector2& position, bool jetpackOn);
    static bool sendGameState(int socket, GameState state, const std::array<Player, MAX_PLAYERS>& players);
    static bool sendGameOver(int socket, int winnerId, const std::array<int, MAX_PLAYERS>& scores);
};

#endif /* PROTOCOL_HPP */