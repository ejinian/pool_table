CXX = clang++
CXXFLAGS = -std=c++17 -O2 -Wall -I/opt/homebrew/include
LDFLAGS = -L/opt/homebrew/lib -lglfw -framework OpenGL -framework Cocoa -framework IOKit

TARGET = pool
SRC = main.cpp

all: $(TARGET)

$(TARGET): $(SRC) vec3.hpp physics.hpp table.hpp render.hpp
	$(CXX) $(CXXFLAGS) $(SRC) -o $(TARGET) $(LDFLAGS)

clean:
	rm -f $(TARGET)

run: $(TARGET)
	./$(TARGET)

.PHONY: all clean run