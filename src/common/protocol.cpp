#include "protocol.hpp"

bool Protocol::sendPacket(int socket, int packetType, const void* data, int dataLength)
{
    PacketHeader header;
    header.type = packetType;
    header.length = dataLength;
    
    if (send(socket, &header, sizeof(header), 0) != sizeof(header)) {
        debugPrint("Erreur lors de l'envoi de l'en-tête du paquet");
        return false;
    }
    if (data && dataLength > 0) {
        if (send(socket, data, dataLength, 0) != dataLength) {
            debugPrint("Erreur lors de l'envoi des données du paquet");
            return false;
        }
    }
    return true;
}

int Protocol::receivePacket(int socket, int& packetType, void* buffer, int bufferSize)
{
    PacketHeader header;
    int received = recv(socket, &header, sizeof(header), MSG_WAITALL);
    packetType = header.type;
    int dataLength = header.length;

    if (received <= 0) {
        if (received == 0) {
            debugPrint("Connexion fermée (recv = 0)");
        } else {
            debugPrint("Erreur lors de la réception de l'en-tête du paquet : " + std::string(strerror(errno)));
        }
        return received;
    }

    if (dataLength > bufferSize) {
        debugPrint("Données de paquet trop grandes pour le buffer");
        return -1;
    }

    if (dataLength > 0) {
        received = recv(socket, buffer, dataLength, 0);
        
        if (received <= 0) {
            debugPrint("Erreur lors de la réception des données du paquet : " + std::string(strerror(errno)));
            return received;
        }
        if (received < dataLength) {
            debugPrint("Données de paquet incomplètes reçues");
            return -1;
        }
    }

    return dataLength;
}


bool Protocol::sendMap(int socket, const Map& map)
{
    std::string mapString = map.toString();
    debugPrint("Envoi de la carte: " + std::to_string(mapString.length()) + " octets");

    if (socket < 0) {
        debugPrint("Socket invalide dans sendMap");
        return false;
    }

    return sendPacket(socket, MAP_DATA, mapString.c_str(), mapString.length());
}


bool Protocol::receiveMap(int socket, Map& map)
{
    char buffer[MAX_BUFFER_SIZE];
    int packetType;
    int dataSize = receivePacket(socket, packetType, buffer, MAX_BUFFER_SIZE - 1);
    
    if (dataSize <= 0) {
        return false;
    }
    if (packetType != MAP_DATA) {
        debugPrint("Type de paquet inattendu pour la carte");
        return false;
    }
    buffer[dataSize] = '\0';
    return map.fromString(std::string(buffer, dataSize));
}

bool Protocol::sendPlayerPosition(int socket, int playerId, const Vector2& position, bool jetpackOn)
{
    struct {
        int player_id;
        float x;
        float y;
        int jetpack_on;
    } data;
    
    data.player_id = playerId;
    data.x = position.x;
    data.y = position.y;
    data.jetpack_on = jetpackOn ? 1 : 0;
    
    debugPrint("Envoi du paquet PLAYER_POS: id=" + std::to_string(playerId) +
              ", jetpack=" + std::to_string(data.jetpack_on));
    
    return sendPacket(socket, PLAYER_POS, &data, sizeof(data));
}

bool Protocol::sendGameState(int socket, GameState state, const std::array<Player, MAX_PLAYERS>& players)
{
    struct {
        int game_state;
        struct {
            int player_id;
            float x;
            float y;
            int score;
            int alive;
            int jetpackOn;
        } players[MAX_PLAYERS];
    } data;

    data.game_state = state;

    for (int i = 0; i < MAX_PLAYERS; ++i) {
        data.players[i].player_id = players[i].id;
        data.players[i].x = players[i].position.x;
        data.players[i].y = players[i].position.y;
        data.players[i].score = players[i].score;
        data.players[i].alive = players[i].alive ? 1 : 0;
        data.players[i].jetpackOn = players[i].jetpackOn ? 1 : 0;
    }
    bool result = sendPacket(socket, GAME_STATE, &data, sizeof(data));
    return result;
}

bool Protocol::sendGameOver(int socket, int winnerId, const std::array<int, MAX_PLAYERS>& scores)
{
    struct {
        int winner_id;
        int scores[MAX_PLAYERS];
    } data;
    
    data.winner_id = winnerId;
    
    for (int i = 0; i < MAX_PLAYERS; ++i) {
        data.scores[i] = scores[i];
    }
    
    return sendPacket(socket, GAME_OVER, &data, sizeof(data));
}

bool Protocol::sendWaitingStatus(int socket, int connectedPlayers)
{
    return sendPacket(socket, WAITING_STATUS, &connectedPlayers, sizeof(int));
}

inline bool sendInt(int socket, int type, int value)
{
    char payload[sizeof(int)];
    std::memcpy(payload, &value, sizeof(int));
    return Protocol::sendPacket(socket, type, payload, sizeof(int));
}


