CXX		= g++
CXXFLAGS	= -Wall -Wextra -g -std=c++17
LDFLAGS		= -pthread
INCLUDE		= -I./include

# Directories
SRC_DIR		= src
COMMON_DIR	= $(SRC_DIR)/common
SERVER_DIR	= $(SRC_DIR)/server
CLIENT_DIR	= $(SRC_DIR)/client
OBJ_DIR		= obj
BIN_DIR		= bin

# Files
COMMON_SRC	= $(wildcard $(COMMON_DIR)/*.cpp)
SERVER_SRC	= $(wildcard $(SERVER_DIR)/*.cpp)
CLIENT_SRC	= $(wildcard $(CLIENT_DIR)/*.cpp)

# Objects
COMMON_OBJ	= $(COMMON_SRC:$(SRC_DIR)/%.cpp=$(OBJ_DIR)/%.o)
SERVER_OBJ	= $(SERVER_SRC:$(SRC_DIR)/%.cpp=$(OBJ_DIR)/%.o) $(COMMON_OBJ)
CLIENT_OBJ	= $(CLIENT_SRC:$(SRC_DIR)/%.cpp=$(OBJ_DIR)/%.o) $(COMMON_OBJ)

# Binaries
SERVER_BIN	= $(BIN_DIR)/jetpack_server
CLIENT_BIN	= $(BIN_DIR)/jetpack_client

# SDL flags for client
CLIENT_LIBS	= -lSDL2 -lSDL2_image

# Rules
all: server client

server: $(SERVER_BIN)

client: $(CLIENT_BIN)

$(SERVER_BIN): $(SERVER_OBJ) | $(BIN_DIR)
	$(CXX) $(CXXFLAGS) $(INCLUDE) -o $@ $^ $(LDFLAGS)

$(CLIENT_BIN): $(CLIENT_OBJ) | $(BIN_DIR)
	$(CXX) $(CXXFLAGS) $(INCLUDE) -o $@ $^ $(LDFLAGS) $(CLIENT_LIBS)

$(OBJ_DIR)/%.o: $(SRC_DIR)/%.cpp | $(OBJ_DIR) $(OBJ_DIR)/server $(OBJ_DIR)/client $(OBJ_DIR)/common
	$(CXX) $(CXXFLAGS) $(INCLUDE) -c -o $@ $

$(BIN_DIR) $(OBJ_DIR) $(OBJ_DIR)/server $(OBJ_DIR)/client $(OBJ_DIR)/common:
	mkdir -p $@

clean:
	rm -rf $(OBJ_DIR)

fclean: clean
	rm -rf $(BIN_DIR)

re: fclean all

.PHONY: all server client clean fclean re