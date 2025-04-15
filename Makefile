CXX = g++
CXXFLAGS = -Wall -Wextra -g -std=c++17
LDFLAGS = -pthread
INCLUDE = -I./include

SRC_DIR = src
COMMON_DIR = $(SRC_DIR)/common
SERVER_DIR = $(SRC_DIR)/server
CLIENT_DIR = $(SRC_DIR)/client
OBJ_DIR = obj

COMMON_SRC = $(wildcard $(COMMON_DIR)/*.cpp)
SERVER_SRC = $(wildcard $(SERVER_DIR)/*.cpp)
CLIENT_SRC = $(wildcard $(CLIENT_DIR)/*.cpp)

COMMON_OBJ = $(patsubst $(SRC_DIR)/%.cpp, $(OBJ_DIR)/%.o, $(COMMON_SRC))
SERVER_OBJ = $(patsubst $(SRC_DIR)/%.cpp, $(OBJ_DIR)/%.o, $(SERVER_SRC))
CLIENT_OBJ = $(patsubst $(SRC_DIR)/%.cpp, $(OBJ_DIR)/%.o, $(CLIENT_SRC))

SERVER_BIN = jetpack_server
CLIENT_BIN = jetpack_client

CLIENT_LIBS = -lSDL2 -lSDL2_image

all: server client

server: $(SERVER_BIN)

client: $(CLIENT_BIN)

$(SERVER_BIN): $(SERVER_OBJ) $(COMMON_OBJ) | $(BIN_DIR)
	$(CXX) $(CXXFLAGS) $(INCLUDE) -o $@ $(SERVER_OBJ) $(COMMON_OBJ) $(LDFLAGS)

$(CLIENT_BIN): $(CLIENT_OBJ) $(COMMON_OBJ) | $(BIN_DIR)
	$(CXX) $(CXXFLAGS) $(INCLUDE) -o $@ $(CLIENT_OBJ) $(COMMON_OBJ) $(LDFLAGS) $(CLIENT_LIBS)

$(OBJ_DIR)/common/%.o: $(COMMON_DIR)/%.cpp | $(OBJ_DIR)/common
	$(CXX) $(CXXFLAGS) $(INCLUDE) -c $< -o $@

$(OBJ_DIR)/server/%.o: $(SERVER_DIR)/%.cpp | $(OBJ_DIR)/server
	$(CXX) $(CXXFLAGS) $(INCLUDE) -c $< -o $@

$(OBJ_DIR)/client/%.o: $(CLIENT_DIR)/%.cpp | $(OBJ_DIR)/client
	$(CXX) $(CXXFLAGS) $(INCLUDE) -c $< -o $@

$(BIN_DIR) $(OBJ_DIR) $(OBJ_DIR)/server $(OBJ_DIR)/client $(OBJ_DIR)/common:
	mkdir -p $@

clean:
	rm -rf $(OBJ_DIR)

fclean: clean
	rm -rf $(BIN_DIR)

re: fclean all

.PHONY: all server client clean fclean re