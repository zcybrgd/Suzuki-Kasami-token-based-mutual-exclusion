#automating compilation and linking in the project

CXX = g++ #setting the compiler for c++
CXXFLAGS = -std=c++17 -Wall -pthread ##enabling all common warnings and link with the POSIX lib for using threads
INCLUDES = -Iinclude

# Répertoires
SRC_DIR = src
BUILD_DIR = build
INCLUDE_DIR = include

# Fichiers sources et objets
SRCS = $(SRC_DIR)/main.cpp $(SRC_DIR)/process.cpp
OBJS = $(patsubst $(SRC_DIR)/%.cpp,$(BUILD_DIR)/%.o,$(SRCS))

TARGET = ra_simulation #our executable aka simulator

all: $(BUILD_DIR) $(TARGET) #this to ensure that the build exist and the target is built

$(BUILD_DIR):
	mkdir -p $(BUILD_DIR)

$(TARGET): $(OBJS)
	$(CXX) $(CXXFLAGS) $(INCLUDES) -o $@ $^

$(BUILD_DIR)/%.o: $(SRC_DIR)/%.cpp
	$(CXX) $(CXXFLAGS) $(INCLUDES) -c $< -o $@

clean:
	rm -rf $(BUILD_DIR) $(TARGET)

run: $(TARGET)
	./$(TARGET)

.PHONY: all clean run
