# Compiler and flags
CXX = g++
CXXFLAGS = -std=c++11 -Wall -m32

# Directories
SRC_DIR = .
INCLUDE_DIR = include
BUILD_DIR = build

# Source files
SRC_FILES = $(wildcard *.cpp)
OBJ_FILES = $(patsubst %.cpp,$(BUILD_DIR)/%.o,$(SRC_FILES))

# Additional include and library directories for SDL
SDL_INCLUDE = -ISDL/include/
SDL_LIB = -LSDL/lib -lSDLmain -lSDL

GL_INCLUDE = -IGL/
GL_LIB  = -LGL

# Include directories
INCLUDES = -I$(INCLUDE_DIR) $(SDL_INCLUDE) $(GL_INCLUDE)

# Target executable
TARGET = my_program

# Build rule
$(BUILD_DIR)/%.o: $(SRC_DIR)/%.cpp
	@mkdir -p $(@D)
	$(CXX) $(CXXFLAGS) $(INCLUDES) -c $< -o $@

# Link rule
$(TARGET): $(OBJ_FILES)
	$(CXX) $(CXXFLAGS) $^ -o $@ $(SDL_LIB) $(GL_LIB)

# Clean rule
clean:
	rm -rf $(BUILD_DIR) $(TARGET)

# Phony target to avoid conflicts with files of the same name
.PHONY: clean