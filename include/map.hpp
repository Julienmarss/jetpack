/*
** EPITECH PROJECT, 2025
** Tek 2 B-NWP-400-LIL-4-1-jetpack-julien.mars
** File description:
** map.hpp
*/

#ifndef MAP_HPP
#define MAP_HPP

#include "common.hpp"

class Map {
public:
    Map() = default;
    ~Map() = default;

    bool loadFromFile(const std::string& filename);
    CellType getCell(int x, int y) const;
    bool checkCollision(float x, float y, float width, float height, CellType cellType) const;
    std::string toString() const;
    bool fromString(const std::string& mapString);
    int getWidth() const { return width; }
    int getHeight() const { return height; }
    void setCell(int x, int y, CellType cellType);
    const std::vector<Vector2>& getStartPositions() const { return startPositions; }

private:
    int width = 0;
    int height = 0;
    std::vector<CellType> data;
    std::vector<Vector2> startPositions;

    void setupStartPositions();
};

#endif