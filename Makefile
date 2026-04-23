CC       = g++
CFLAGS   = -std=c++17 -Wall -Wextra -O2
INCLUDES = $(shell pkg-config --cflags raylib 2>/dev/null || echo "-I/usr/local/include")
LIBS     = $(shell pkg-config --libs   raylib 2>/dev/null || echo "-L/usr/local/lib -lraylib \
             -framework OpenGL -framework Cocoa -framework IOKit \
             -framework CoreAudio -framework CoreVideo")

TARGET = packet-path
SRC    = $(wildcard src/*.cpp)

all: $(TARGET)

$(TARGET): $(SRC)
	$(CC) $(CFLAGS) $(INCLUDES) $(SRC) $(LIBS) -o $(TARGET)

clean:
	rm -f $(TARGET)

.PHONY: all clean
