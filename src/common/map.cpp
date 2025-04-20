#include "map.hpp"

bool Map::loadFromFile(const std::string& filename) {
    std::ifstream file(filename);
    if (!file) {
        debugPrint("Impossible d'ouvrir le fichier de carte: " + filename);
        return false;
    }
    
    std::vector<std::string> lines;
    std::string line;
    
    while (std::getline(file, line)) {
        lines.push_back(line);
    }
    
    if (lines.empty()) {
        debugPrint("Carte vide!");
        return false;
    }
    height = lines.size();
    width = lines[0].length();
    data.clear();
    data.resize(width * height, EMPTY);
    
    for (size_t y = 0; y < lines.size(); y++) {
        const std::string& currentLine = lines[y];
        for (size_t x = 0; x < currentLine.length() && x < static_cast<size_t>(width); x++) {
            CellType cell = EMPTY;
            
            switch (currentLine[x]) {
                case 'c': 
                    cell = COIN;
                    break;
                case 'e': 
                    cell = ELECTRIC;
                    break;
                default: 
                    cell = EMPTY;
                    break;
            }
            
            data[y * width + x] = cell;
        }
    }
    
    setupStartPositions();
    debugPrint("Carte chargée: " + std::to_string(width) + "x" + std::to_string(height));
    return true;
}

CellType Map::getCell(int x, int y) const
{
    if (x < 0 || x >= width || y < 0 || y >= height) {
        return EMPTY;
    }
    return data[y * width + x];
}

bool Map::checkCollision(float x, float y, float width, float height, CellType cellType) const {
    int startX = static_cast<int>(x);
    int startY = static_cast<int>(y);
    int endX = static_cast<int>(x + width);
    int endY = static_cast<int>(y + height);
    
    for (int cy = startY; cy <= endY; ++cy) {
        for (int cx = startX; cx <= endX; ++cx) {
            if (getCell(cx, cy) == cellType) {
                return true;
            }
        }
    }
    
    return false;
}

void Map::setupStartPositions() {
    startPositions.clear();

    int startX = 2;
    int startY = height - 3;

    Vector2 sameStart(startX, startY);
    for (int i = 0; i < MAX_PLAYERS; ++i) {
        startPositions.push_back(sameStart);
        debugPrint("Position de départ du joueur " + std::to_string(i) + ": (" +
                   std::to_string(startX) + "," + std::to_string(startY) + ")");
    }
}

std::string Map::toString() const {
    std::string result;
    
    result += std::to_string(width) + "," + std::to_string(height) + "\n";
    
    for (int y = 0; y < height; y++) {
        for (int x = 0; x < width; x++) {
            CellType cell = getCell(x, y);
            
            switch (cell) {
                case COIN: 
                    result += 'c';
                    break;
                case ELECTRIC: 
                    result += 'e'; 
                    break;
                default: 
                    result += '_'; 
                    break;
            }
        }
        result += '\n';
    }
    
    return result;
}

bool Map::fromString(const std::string& mapString) {
    std::istringstream stream(mapString);
    std::string line;
    int y = 0;
    int coinCount = 0;
    int electricCount = 0;
    
    if (!std::getline(stream, line)) {
        debugPrint("Format de carte invalide: impossible de lire les dimensions");
        return false;
    }
    
    size_t commaPos = line.find(',');
    if (commaPos == std::string::npos) {
        debugPrint("Format de carte invalide: dimensions mal formatées");
        return false;
    }
    
    width = std::stoi(line.substr(0, commaPos));
    height = std::stoi(line.substr(commaPos + 1));
    data.clear();
    data.resize(width * height, EMPTY);
    
    while (std::getline(stream, line) && y < height) {
        for (size_t x = 0; x < line.length() && x < static_cast<size_t>(width); x++) {
            CellType cell = EMPTY;
            
            switch (line[x]) {
                case 'c': 
                    cell = COIN; 
                    coinCount++; 
                    break;
                case 'e': 
                    cell = ELECTRIC; 
                    electricCount++; 
                    break;
                default: 
                    cell = EMPTY; 
                    break;
            }
            data[y * width + x] = cell;
        }
        y++;
    }
    setupStartPositions();
    const std::vector<Vector2>& positions = getStartPositions();
    for (size_t i = 0; i < positions.size(); i++) {
        Vector2 pos = positions[i];
        CellType cellAtStart = getCell(pos.x, pos.y);
    }
    return true;
}