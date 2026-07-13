CC       = g++
CFLAGS   = -std=c++17 -Wall -Wextra -O2 -isystem ./include
INCLUDES = $(shell pkg-config --cflags raylib 2>/dev/null || echo "-I/usr/local/include")
LIBS     = $(shell pkg-config --libs   raylib 2>/dev/null || echo "-L/usr/local/lib -lraylib \
             -framework OpenGL -framework Cocoa -framework IOKit \
             -framework CoreAudio -framework CoreVideo")

TARGET = packet-path
SRC    = $(wildcard src/*.cpp)
TEST_TARGET = packet-path-tests
TEST_SRC    = $(filter-out src/main.cpp,$(SRC)) tests/simulator_tests.cpp

all: $(TARGET)

$(TARGET): $(SRC)
	$(CC) $(CFLAGS) $(INCLUDES) $(SRC) $(LIBS) -o $(TARGET)

clean:
	rm -f $(TARGET) $(TEST_TARGET)
	rm -rf $(TARGET).dSYM $(TEST_TARGET).dSYM

test: $(TEST_TARGET)
	./$(TEST_TARGET)

$(TEST_TARGET): $(TEST_SRC)
	$(CC) $(CFLAGS) -I./src $(INCLUDES) $(TEST_SRC) $(LIBS) -o $(TEST_TARGET)

.PHONY: all clean test
