################################
# Makefile for XIIRegexBuilder #
################################

# OS Detection
ifeq ($(OS),Windows_NT)
    EXE := .exe
    RM := del /f /q
    RMDIR := rmdir /s /q
    MKDIR = if not exist $(subst /,\,$1) mkdir $(subst /,\,$1)
    FIX_PATH = $(subst /,\,$1)
else
    EXE :=
    RM := rm -f
    RMDIR := rm -rf
    MKDIR = mkdir -p $1
    FIX_PATH = $1
endif

CC = g++
CFLAGS = -Wall -Wextra -std=c++17 -Isrc -static
BUILD_DIR = build
TARGET = $(BUILD_DIR)/regex_builder$(EXE)
TESTER = $(BUILD_DIR)/nfa_tester$(EXE)
GOLDEN = $(BUILD_DIR)/golden$(EXE)
SRCS = src/main.cpp src/lexer.cpp src/parser.cpp src/nfa.cpp src/table_gen.cpp
TEST_SRCS = src/parser_tester.cpp src/lexer.cpp src/parser.cpp src/nfa.cpp
GOLDEN_SRCS = src/golden.cpp
OBJS = $(SRCS:.cpp=.o)
TEST_OBJS = $(TEST_SRCS:.cpp=.o)
GOLDEN_OBJS = $(GOLDEN_SRCS:.cpp=.o)

INPUT_DIR = inputs
OUTPUT_DIR = output

# Vivado Simulation Tools
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
	$(call MKDIR,$(BUILD_DIR))

$(OUTPUT_DIR):
	$(call MKDIR,$(OUTPUT_DIR))

%.o: %.cpp
	$(CC) $(CFLAGS) -c $< -o $@

run: all | $(OUTPUT_DIR)
	$(TARGET) $(INPUT_DIR)/regexes.txt $(INPUT_DIR)/test_strings.txt $(OUTPUT_DIR)

test: $(TESTER)
	$(TESTER) $(INPUT_DIR)/regexes.txt $(INPUT_DIR)/test_strings.txt


golden: $(GOLDEN) | $(OUTPUT_DIR)
	$(GOLDEN) $(INPUT_DIR)/regexes.txt $(INPUT_DIR)/test_strings.txt $(OUTPUT_DIR)/expected_matches.txt

sim: run
	@echo "1. Compiling Verilog files..."
	$(XVLOG) $(OUTPUT_DIR)/*.v 
	@echo "2. Elaborating design..."
	$(XELAB) -top $(SIM_TOP) -snapshot $(SNAPSHOT) -debug typical
	@echo "3. Running simulation..."
	$(XSIM) $(SNAPSHOT) -R

# FPGA Targets
synth: run
	@echo "Launching Vivado Synthesis Flow..."
	$(VIVADO_PATH) -mode batch -source $(call FIX_PATH,scripts/synth.tcl)

program:
	@echo "Launching Vivado Programming Flow..."
	$(VIVADO_PATH) -mode batch -source $(call FIX_PATH,scripts/program.tcl)


clean:
	-$(RM) $(call FIX_PATH,src/*.o)
	-$(RMDIR) $(call FIX_PATH,$(BUILD_DIR))
	-$(RMDIR) $(call FIX_PATH,$(OUTPUT_DIR))
	-$(RM) *.bit *.log *.jou *.pb *.wdb *.str usage_statistics_webtalk.*
	-$(RM) clockInfo.txt dfx_runtime.txt
	-$(RMDIR) xsim.dir .Xil

.PHONY: all run test golden clean
