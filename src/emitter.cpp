#include "emitter.h"
#include <fstream>
#include <iostream>
#include <map>
#include <set>
#include <iomanip>
#include <cstdlib> // for system()

Emitter::Emitter(const std::vector<std::unique_ptr<NFA>>& nfas) : nfas(nfas) {}

void Emitter::emit(const std::string& outputDir) {
    // Portable way to create directory on POSIX/Windows (via shell)
    std::string mkdirCmd = "mkdir -p " + outputDir;
    int ret = std::system(mkdirCmd.c_str());
    (void)ret; // Suppress unused result warning

    for (const auto& nfa : nfas) {
        emitNFAModule(*nfa, outputDir);
    }

    emitTopModule(outputDir);
    emitTestbench(outputDir);
}

void Emitter::emitNFAModule(const NFA& nfa, const std::string& outputDir) {
    std::string filename = outputDir + "/nfa_" + std::to_string(nfa.regexIndex) + ".v";
    std::ofstream f(filename);
    if (!f.is_open()) return;

    int numStates = nfa.states.size();
    
    // We map global state IDs to local indices 0..numStates-1 for the one-hot register
    std::map<int, int> globalToLocal;
    int idx = 0;
    // Ensure start state is index 0
    globalToLocal[nfa.startStateId] = idx++;
    for (const auto& [id, state] : nfa.states) {
        if (id != nfa.startStateId) {
            globalToLocal[id] = idx++;
        }
    }

    f << "module nfa_" << nfa.regexIndex << " (\n"
      << "    input wire clk,\n"
      << "    input wire rst,\n"
      << "    input wire start,\n"
      << "    input wire end_of_str,\n"
      << "    input wire [7:0] char_in,\n"
      << "    output reg match\n"
      << ");\n\n";

    f << "    // One-hot state register\n"
      << "    reg [" << numStates - 1 << ":0] state_reg;\n"
      << "    wire [" << numStates - 1 << ":0] next_state;\n\n";

    // Next state logic
    for (int j = 0; j < numStates; ++j) {
        f << "    assign next_state[" << j << "] = ";
        
        bool first = true;
        // Find all transitions into local state j
        for (const auto& [srcId, srcState] : nfa.states) {
            int srcLocal = globalToLocal[srcId];
            for (const auto& [c, dstIds] : srcState.transitions) {
                for (int dstId : dstIds) {
                    if (globalToLocal[dstId] == j) {
                        if (!first) f << " || ";
                        f << "(state_reg[" << srcLocal << "] && (char_in == 8'd" << (int)c << "))";
                        first = false;
                    }
                }
            }
        }
        if (first) f << "1'b0"; // No transitions into this state
        f << ";\n";
    }

    f << "\n    always @(posedge clk) begin\n"
      << "        if (rst) begin\n"
      << "            state_reg <= " << numStates << "'b0;\n"
      << "            state_reg[0] <= 1'b1; // Start state\n"
      << "        end else if (start) begin\n"
      << "            state_reg <= " << numStates << "'b0;\n"
      << "            state_reg[0] <= 1'b1;\n"
      << "        end else begin\n"
      << "            state_reg <= next_state;\n"
      << "        end\n"
      << "    end\n\n";

    // Match logic
    f << "    always @(posedge clk) begin\n"
      << "        if (rst || start) begin\n"
      << "            match <= 1'b0;\n"
      << "        end else if (end_of_str) begin\n"
      << "            match <= ";
    
    bool firstAccept = true;
    for (const auto& [id, state] : nfa.states) {
        if (state.isAccept) {
            if (!firstAccept) f << " || ";
            f << "next_state[" << globalToLocal[id] << "]";
            firstAccept = false;
        }
    }
    if (firstAccept) f << "1'b0";
    f << ";\n"
      << "        end else begin\n"
      << "            match <= 1'b0;\n"
      << "        end\n"
      << "    end\n\n"
      << "endmodule\n";
}

void Emitter::emitTopModule(const std::string& outputDir) {
    std::string filename = outputDir + "/top.v";
    std::ofstream f(filename);
    if (!f.is_open()) return;

    int numNFAs = nfas.size();

    f << "module top (\n"
      << "    input wire clk,\n"
      << "    input wire rst,\n"
      << "    input wire start,\n"
      << "    input wire end_of_str,\n"
      << "    input wire [7:0] char_in,\n"
      << "    output wire [" << (numNFAs > 0 ? numNFAs - 1 : 0) << ":0] match_bus\n"
      << ");\n\n";

    for (const auto& nfa : nfas) {
        f << "    nfa_" << nfa->regexIndex << " inst_" << nfa->regexIndex << " (\n"
          << "        .clk(clk),\n"
          << "        .rst(rst),\n"
          << "        .start(start),\n"
          << "        .end_of_str(end_of_str),\n"
          << "        .char_in(char_in),\n"
          << "        .match(match_bus[" << nfa->regexIndex << "])\n"
          << "    );\n\n";
    }

    f << "endmodule\n";
}

void Emitter::emitTestbench(const std::string& outputDir) {
    std::string filename = outputDir + "/tb_top.v";
    std::ofstream f(filename);
    if (!f.is_open()) return;

    int numNFAs = nfas.size();

    f << "`timescale 1ns / 1ps\n\n"
      << "module tb_top();\n\n"
      << "    reg clk;\n"
      << "    reg rst;\n"
      << "    reg start;\n"
      << "    reg end_of_str;\n"
      << "    reg [7:0] char_in;\n"
      << "    wire [" << (numNFAs > 0 ? numNFAs - 1 : 0) << ":0] match_bus;\n\n"
      << "    top uut (\n"
      << "        .clk(clk),\n"
      << "        .rst(rst),\n"
      << "        .start(start),\n"
      << "        .end_of_str(end_of_str),\n"
      << "        .char_in(char_in),\n"
      << "        .match_bus(match_bus)\n"
      << "    );\n\n"
      << "    always #5 clk = ~clk;\n\n"
      << "    initial begin\n"
      << "        $dumpfile(\"dump.vcd\");\n"
      << "        $dumpvars(0, tb_top);\n\n"
      << "        clk = 0;\n"
      << "        rst = 1;\n"
      << "        start = 0;\n"
      << "        end_of_str = 0;\n"
      << "        char_in = 0;\n\n"
      << "        #20 rst = 0;\n"
      << "        #20;\n\n"
      << "        // Add test cases here\n"
      << "        $display(\"Starting simulation...\");\n\n"
      << "        // Example: match 'abc'\n"
      << "        // start = 1; #10 start = 0;\n"
      << "        // char_in = \"a\"; #10;\n"
      << "        // char_in = \"b\"; #10;\n"
      << "        // char_in = \"c\"; end_of_str = 1; #10 end_of_str = 0;\n"
      << "        // #10 $display(\"Match bus: %b\", match_bus);\n\n"
      << "        #100;\n"
      << "        $finish;\n"
      << "    end\n\n"
      << "endmodule\n";
}
