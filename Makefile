CC = g++
CFLAGS = -Wall -Wextra -std=c++17 -Isrc -static
BUILD_DIR = build
TARGET = $(BUILD_DIR)/regex_builder
TESTER = $(BUILD_DIR)/nfa_tester
GOLDEN = $(BUILD_DIR)/golden
SRCS = src/main.cpp src/lexer.cpp src/parser.cpp src/nfa.cpp src/emitter.cpp
TEST_SRCS = src/parser_tester.cpp src/lexer.cpp src/parser.cpp src/nfa.cpp
GOLDEN_SRCS = src/golden.cpp
OBJS = $(SRCS:.cpp=.o)
TEST_OBJS = $(TEST_SRCS:.cpp=.o)
GOLDEN_OBJS = $(GOLDEN_SRCS:.cpp=.o)

INPUT_DIR = inputs
OUTPUT_DIR = output

# --- Vivado Simulation Tools ---
VIVADO_PATH = vivado
XVLOG = xvlog
XELAB = xelab
XSIM  = xsim
SIM_TOP = tb_top
SNAPSHOT = regex_sim

all: $(TARGET) $(GOLDEN)

$(TARGET): $(OBJS) | $(BUILD_DIR)
	$(CC) $(CFLAGS) -o $(TARGET) $(OBJS)

$(TESTER): $(TEST_OBJS) | $(BUILD_DIR)
	$(CC) $(CFLAGS) -o $(TESTER) $(TEST_OBJS)

$(GOLDEN): $(GOLDEN_OBJS) | $(BUILD_DIR)
	$(CC) $(CFLAGS) -o $(GOLDEN) $(GOLDEN_OBJS)

$(BUILD_DIR):
	mkdir -p $(BUILD_DIR)

$(OUTPUT_DIR):
	mkdir -p $(OUTPUT_DIR)

%.o: %.cpp
	$(CC) $(CFLAGS) -c $< -o $@

run: all | $(OUTPUT_DIR)
	./$(TARGET) $(INPUT_DIR)/regexes.txt $(INPUT_DIR)/test_strings.txt $(OUTPUT_DIR)

test: $(TESTER)
	./$(TESTER) $(INPUT_DIR)/regexes.txt $(INPUT_DIR)/test_strings.txt

golden: $(GOLDEN) | $(OUTPUT_DIR)
	./$(GOLDEN) $(INPUT_DIR)/regexes.txt $(INPUT_DIR)/test_strings.txt $(OUTPUT_DIR)/expected_matches.txt

# --- Hardware / FPGA Targets ---
synth: run
	@echo "--- Launching Vivado Synthesis Flow ---"
	$(VIVADO_PATH) -mode batch -source synth.tcl

program:
	@echo "--- Launching Vivado Programming Flow ---"
	$(VIVADO_PATH) -mode batch -source program.tcl

sim: run
	@echo "--- 1. Compiling Verilog files with xvlog ---"
	$(XVLOG) $(OUTPUT_DIR)/*.v 
	@echo "--- 2. Elaborating design with xelab ---"
	$(XELAB) -top $(SIM_TOP) -snapshot $(SNAPSHOT) -debug typical
	@echo "--- 3. Running simulation with xsim ---"
	$(XSIM) $(SNAPSHOT) -R

clean:
	rm -f src/*.o
	rm -rf $(BUILD_DIR)
	rm -rf $(OUTPUT_DIR)
	rm -f *.bit *.log *.jou *.pb *.wdb *.str usage_statistics_webtalk.*
	rm -rf xsim.dir .Xil

.PHONY: all run test golden clean

# Simulation target
sim:
	iverilog -o sim_out output/*.v
	vvp sim_out
