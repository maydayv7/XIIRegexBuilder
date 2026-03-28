CC = g++
CFLAGS = -Wall -Wextra -std=c++17 -Isrc
TARGET = regex_builder
SRCS = src/main.cpp src/lexer.cpp src/parser.cpp
OBJS = $(SRCS:.cpp=.o)

all: $(TARGET)

$(TARGET): $(OBJS)
	$(CC) $(CFLAGS) -o $(TARGET) $(OBJS)

%.o: %.cpp
	$(CC) $(CFLAGS) -c $< -o $@

run: all
	./$(TARGET) regexes.txt

clean:
	rm -f $(OBJS) $(TARGET)

.PHONY: all run clean
