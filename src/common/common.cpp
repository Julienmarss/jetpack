#include "common.hpp"

bool debug_mode = false;

void debugPrint(const std::string& message) {
    if (!debug_mode) {
        return;
    }
    
    std::cout << "[DEBUG] " << message << std::endl;
}