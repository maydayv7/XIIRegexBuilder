CC = g++
CFLAGS = -Wall -Wextra -std=c++17 -Isrc
TARGET = regex_builder
TESTER = nfa_tester
SRCS = src/main.cpp src/lexer.cpp src/parser.cpp src/nfa.cpp src/emitter.cpp
TEST_SRCS = src/parser_tester.cpp src/lexer.cpp src/parser.cpp src/nfa.cpp
OBJS = $(SRCS:.cpp=.o)
TEST_OBJS = $(TEST_SRCS:.cpp=.o)

all: $(TARGET)

$(TARGET): $(OBJS)
	$(CC) $(CFLAGS) -o $(TARGET) $(OBJS)

$(TESTER): $(TEST_OBJS)
	$(CC) $(CFLAGS) -o $(TESTER) $(TEST_OBJS)

%.o: %.cpp
	$(CC) $(CFLAGS) -c $< -o $@

run: all
	./$(TARGET) regexes.txt output

test: $(TESTER)
	./$(TESTER) regexes.txt test_strings.txt

clean:
	rm -f src/*.o $(TARGET) $(TESTER)
	rm -rf output

.PHONY: all run test clean
