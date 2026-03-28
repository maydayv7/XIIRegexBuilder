CC = g++
CFLAGS = -Wall -Wextra -std=c++17 -Isrc
BUILD_DIR = build
TARGET = $(BUILD_DIR)/regex_builder
TESTER = $(BUILD_DIR)/nfa_tester
SRCS = src/main.cpp src/lexer.cpp src/parser.cpp src/nfa.cpp src/emitter.cpp
TEST_SRCS = src/parser_tester.cpp src/lexer.cpp src/parser.cpp src/nfa.cpp
OBJS = $(SRCS:.cpp=.o)
TEST_OBJS = $(TEST_SRCS:.cpp=.o)

INPUT_DIR = inputs
OUTPUT_DIR = output

all: $(TARGET)

$(TARGET): $(OBJS) | $(BUILD_DIR)
	$(CC) $(CFLAGS) -o $(TARGET) $(OBJS)

$(TESTER): $(TEST_OBJS) | $(BUILD_DIR)
	$(CC) $(CFLAGS) -o $(TESTER) $(TEST_OBJS)

$(BUILD_DIR):
	mkdir -p $(BUILD_DIR)

%.o: %.cpp
	$(CC) $(CFLAGS) -c $< -o $@

run: all
	./$(TARGET) $(INPUT_DIR)/regexes.txt $(INPUT_DIR)/test_strings.txt $(OUTPUT_DIR)

test: $(TESTER)
	./$(TESTER) $(INPUT_DIR)/regexes.txt $(INPUT_DIR)/test_strings.txt

clean:
	rm -f src/*.o
	rm -rf $(BUILD_DIR)
	rm -rf $(OUTPUT_DIR)

.PHONY: all run test clean
