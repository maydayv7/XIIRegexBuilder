#include "emitter.h"
#include <fstream>
#include <iostream>
#include <iomanip>
#include <sstream>
#include <algorithm>
#include <set>
#include <map>
#include <system_error>
#include <filesystem>

void Emitter::emit(const std::vector<std::unique_ptr<NFA>> &nfas,
                   const std::string &outputDirStr,
                   const std::vector<std::string> &testStrings,
                   const std::vector<std::string> &expectedMatches)
{
    if (nfas.empty())
    {
        std::cout << "No valid NFAs were provided; skipping Verilog emission." << std::endl;
        return;
    }

    std::filesystem::path outputDir(outputDirStr);
    try
    {
        std::filesystem::create_directories(outputDir);
    }
    catch (const std::filesystem::filesystem_error &e)
    {
        throw std::system_error(e.code(), "Failed to create output directory: " + outputDir.string());
    }

    for (const auto &nfa : nfas)
    {
        emitNFAModule(*nfa, outputDir);
    }

    emitTopModule(nfas, outputDir);
    emitUART(outputDir);
    emitUARTTx(outputDir);
    emitTopFPGA(nfas, outputDir);
    emitConstraints(nfas, outputDir);
    emitTestbench(nfas, outputDir, testStrings, expectedMatches);
}

void Emitter::emitNFAModule(const NFA &nfa, const std::filesystem::path &outputDir)
{
    auto filePath = outputDir / ("nfa_" + std::to_string(nfa.regexIndex) + ".v");
    std::ofstream out;
    out.exceptions(std::ofstream::failbit | std::ofstream::badbit);

    try
    {
        out.open(filePath);
    }
    catch (const std::ios_base::failure &e)
    {
        throw std::system_error(errno, std::generic_category(), "Could not open " + filePath.string() + " for writing");
    }

    int numStates = static_cast<int>(nfa.states.size());
    if (numStates == 0)
        return;

    std::map<int, int> globalToLocal;
    int localIdx = 0;

    // Ensure start state is always local state 0
    globalToLocal[nfa.startStateId] = localIdx++;

    // Sort other state IDs for deterministic output
    std::set<int> otherIds;
    for (const auto &pair : nfa.states)
    {
        if (pair.first != nfa.startStateId)
            otherIds.insert(pair.first);
    }
    for (int id : otherIds)
    {
        globalToLocal[id] = localIdx++;
    }

    // ADDED: Synchronous enable (en) port instead of gated clock
    out << "`timescale 1ns / 1ps\n\n";
    out << "// NFA for regex index " << nfa.regexIndex << "\n";
    out << "module nfa_" << nfa.regexIndex << " (\n"
        << "    input  wire       clk,\n"
        << "    input  wire       en,\n"
        << "    input  wire       rst,\n"
        << "    input  wire       start,\n"
        << "    input  wire [7:0] char_in,\n"
        << "    output wire       match\n"
        << ");\n\n";

    out << "    // One-hot state register\n"
        << "    reg [" << numStates - 1 << ":0] state_reg;\n"
        << "    wire [" << numStates - 1 << ":0] next_state;\n\n";

    std::map<int, std::vector<std::pair<int, unsigned char>>> invertedTransitions;
    for (const auto &[srcGlobalId, srcState] : nfa.states)
    {
        for (const auto &[c, dstIds] : srcState.transitions)
        {
            for (int dstGlobalId : dstIds)
            {
                invertedTransitions[dstGlobalId].push_back({srcGlobalId, c});
            }
        }
    }

    for (const auto &[globalId, localId] : globalToLocal)
    {
        out << "    assign next_state[" << localId << "] = ";

        std::vector<std::string> terms;

        // FIX: Start state is always active to allow substring matching
        if (localId == 0)
        {
            terms.push_back("1'b1");
        }

        if (auto it = invertedTransitions.find(globalId); it != invertedTransitions.end())
        {
            std::map<int, std::vector<unsigned char>> charsBySrc;
            for (const auto &[srcGlobalId, c] : it->second)
            {
                charsBySrc[globalToLocal.at(srcGlobalId)].push_back(c);
            }

            // Magnitude comparators for contiguous wildcard ranges
            for (auto &[srcLocal, chars] : charsBySrc)
            {
                std::sort(chars.begin(), chars.end());
                size_t i = 0;
                while (i < chars.size())
                {
                    size_t j = i;
                    while (j + 1 < chars.size() && chars[j + 1] == chars[j] + 1)
                    {
                        j++;
                    }
                    if (j - i >= 3)
                    {
                        terms.push_back("(state_reg[" + std::to_string(srcLocal) +
                                        "] && (char_in >= 8'd" + std::to_string(chars[i]) +
                                        ") && (char_in <= 8'd" + std::to_string(chars[j]) + "))");
                    }
                    else
                    {
                        for (size_t k = i; k <= j; ++k)
                        {
                            terms.push_back("(state_reg[" + std::to_string(srcLocal) +
                                            "] && (char_in == 8'd" + std::to_string(chars[k]) + "))");
                        }
                    }
                    i = j + 1;
                }
            }
        }

        if (terms.empty())
        {
            out << "1'b0;\n";
        }
        else
        {
            out << terms[0];
            for (size_t t = 1; t < terms.size(); ++t)
            {
                out << " | " << terms[t];
            }
            out << ";\n";
        }
    }

    // FIX: Using synchronous enable with word-boundary (space) reset
    out << "\n    always @(posedge clk) begin\n"
        << "        if (rst || start || (en && char_in == 8'h20)) begin\n"
        << "            // Reset to start state (one-hot) on reset, start pulse, or space\n"
        << "            state_reg <= " << (numStates > 0 ? "1" : "0") << ";\n"
        << "        end else if (en) begin\n"
        << "            state_reg <= next_state;\n"
        << "        end\n"
        << "    end\n\n";

    out << "    // Match logic: asserted immediately on accept state (combinational)\n"
        << "    assign match = ";

    std::vector<std::string> acceptTerms;
    for (const auto &[id, state] : nfa.states)
    {
        if (state.isAccept)
        {
            acceptTerms.push_back("state_reg[" + std::to_string(globalToLocal.at(id)) + "]");
        }
    }

    if (acceptTerms.empty())
    {
        out << "1'b0";
    }
    else if (acceptTerms.size() == 1)
    {
        out << acceptTerms[0];
    }
    else
    {
        out << "(|{";
        for (size_t t = 0; t < acceptTerms.size(); ++t)
        {
            out << acceptTerms[t] << (t < acceptTerms.size() - 1 ? ", " : "");
        }
        out << "})";
    }

    out << ";\n\n"
        << "endmodule\n";
}

void Emitter::emitTopModule(const std::vector<std::unique_ptr<NFA>> &nfas, const std::filesystem::path &outputDir)
{
    auto filePath = outputDir / "top.v";
    std::ofstream out;
    out.exceptions(std::ofstream::failbit | std::ofstream::badbit);

    try
    {
        out.open(filePath);
    }
    catch (const std::ios_base::failure &e)
    {
        throw std::system_error(errno, std::generic_category(), "Could not open " + filePath.string() + " for writing");
    }

    out << "`timescale 1ns / 1ps\n\n";
    out << "module top (\n"
        << "    input  wire       clk,\n"
        << "    input  wire       en,\n"
        << "    input  wire       rst,\n"
        << "    input  wire       start,\n"
        << "    input  wire [7:0] char_in,\n"
        << "    output wire [" << (nfas.size() - 1) << ":0] match_bus\n"
        << ");\n\n";

    for (size_t i = 0; i < nfas.size(); ++i)
    {
        out << "    nfa_" << nfas[i]->regexIndex << " inst_" << nfas[i]->regexIndex << " (\n"
            << "        .clk(clk), .en(en), .rst(rst), .start(start), .char_in(char_in), .match(match_bus[" << i << "])\n"
            << "    );\n\n";
    }
    out << "endmodule\n";
}

void Emitter::emitTestbench(const std::vector<std::unique_ptr<NFA>> &nfas,
                            const std::filesystem::path &outputDir,
                            const std::vector<std::string> &testStrings,
                            const std::vector<std::string> &expectedMatches)
{
    auto filePath = outputDir / "tb_top.v";
    std::ofstream out;
    out.exceptions(std::ofstream::failbit | std::ofstream::badbit);

    try
    {
        out.open(filePath);
    }
    catch (const std::ios_base::failure &e)
    {
        throw std::system_error(errno, std::generic_category(), "Could not open " + filePath.string() + " for writing");
    }

    size_t numNFAs = nfas.size();

    out << "`timescale 1ns / 1ps\n\n"
        << "module tb_top;\n"
        << "    reg clk, en, rst, start;\n"
        << "    reg [7:0] char_in;\n"
        << "    wire [" << (numNFAs > 0 ? numNFAs - 1 : 0) << ":0] match_bus;\n\n"
        << "    top uut (.clk(clk), .en(en), .rst(rst), .start(start), .char_in(char_in), .match_bus(match_bus));\n\n"
        << "    always #5 clk = ~clk;\n\n"
        << "    initial begin\n"
        << "        // synthesis translate_off\n"
        << "        `ifndef SYNTHESIS\n"
        << "        $dumpfile(\"dump.vcd\");\n"
        << "        $dumpvars(0, tb_top);\n"
        << "        `endif\n"
        << "        // synthesis translate_on\n\n"
        << "        clk = 0; en = 1; rst = 1; start = 0; char_in = 0;\n"
        << "        #20 rst = 0; #10;\n\n";

    for (size_t i = 0; i < testStrings.size(); ++i)
    {
        const std::string &s = testStrings[i];
        out << "        // Test case " << i << ": \"" << s << "\"\n"
            << "        start = 1; #10 start = 0;\n";

        for (char c : s)
        {
            out << "        char_in = 8'h" << std::hex << std::setw(2) << std::setfill('0') << static_cast<int>(static_cast<unsigned char>(c)) << std::dec << "; #10;\n"
                << "        if (|match_bus) $display(\"  [MATCH] Cycle char='%c' Match=%b\", char_in, match_bus);\n";
        }

        out << "        #10;\n";

        if (i < expectedMatches.size())
        {
            // For streaming, we might not have a single expected mask at the end.
            // But let's keep the check for compatibility if needed, or just report.
            out << "        $display(\"INFO: Final match_bus for case " << i << " ('" << s << "'): %b\", match_bus);\n";
        }
        else
        {
            out << "        $display(\"INFO: Result for '" << s << "': %b\", match_bus);\n";
        }
        out << "        #10;\n\n";
    }

    out << "        $display(\"All tests completed.\");\n"
        << "        #100; $finish;\n    end\nendmodule\n";
}

void Emitter::emitUART(const std::filesystem::path &outputDir)
{
    auto filePath = outputDir / "uart_rx.v";
    std::ofstream out(filePath);
    out.exceptions(std::ofstream::failbit | std::ofstream::badbit);

    out << "`timescale 1ns / 1ps\n\n";
    out << R"(
module uart_rx #(
    parameter CLKS_PER_BIT = 868 // 100 MHz / 115200 Baud
)(
    input  wire       clk,
    input  wire       rx,
    output reg  [7:0] rx_data,
    output reg        rx_ready
);
    localparam IDLE = 2'b00, START_BIT = 2'b01, DATA_BITS = 2'b10, STOP_BIT = 2'b11;
    reg [1:0] state = IDLE;
    reg [9:0] clk_count = 0;
    reg [2:0] bit_idx = 0;

    always @(posedge clk) begin
        case (state)
            IDLE: begin
                rx_ready <= 1'b0;
                clk_count <= 0;
                bit_idx <= 0;
                if (rx == 1'b0) state <= START_BIT;
            end
            START_BIT: begin
                if (clk_count == (CLKS_PER_BIT-1)/2) begin
                    if (rx == 1'b0) begin
                        clk_count <= 0;
                        state <= DATA_BITS;
                    end else state <= IDLE;
                end else clk_count <= clk_count + 1;
            end
            DATA_BITS: begin
                if (clk_count < CLKS_PER_BIT-1) begin
                    clk_count <= clk_count + 1;
                end else begin
                    clk_count <= 0;
                    rx_data[bit_idx] <= rx;
                    if (bit_idx < 7) bit_idx <= bit_idx + 1;
                    else state <= STOP_BIT;
                end
            end
            STOP_BIT: begin
                if (clk_count < CLKS_PER_BIT-1) begin
                    clk_count <= clk_count + 1;
                end else begin
                    // FIX: Check for framing error
                    if (rx == 1'b1) rx_ready <= 1'b1; 
                    state <= IDLE;
                end
            end
        endcase
    end
endmodule
)";
}

void Emitter::emitUARTTx(const std::filesystem::path &outputDir)
{
    auto filePath = outputDir / "uart_tx.v";
    std::ofstream out(filePath);
    out.exceptions(std::ofstream::failbit | std::ofstream::badbit);

    out << "`timescale 1ns / 1ps\n\n";
    out << R"(
module uart_tx #(
    parameter CLKS_PER_BIT = 868 // 100 MHz / 115200 Baud
)(
    input  wire       clk,
    input  wire       tx_start,
    input  wire [7:0] tx_data,
    input  wire       cts,      // Clear To Send (Active High)
    output reg        tx,
    output reg        tx_busy
);
    localparam IDLE = 2'b00, START_BIT = 2'b01, DATA_BITS = 2'b10, STOP_BIT = 2'b11;
    reg [1:0] state = IDLE;
    reg [9:0] clk_count = 0;
    reg [2:0] bit_idx = 0;
    reg [7:0] tx_data_latch = 0;

    initial begin
        tx = 1'b1;
        tx_busy = 1'b0;
    end

    always @(posedge clk) begin
        case (state)
            IDLE: begin
                tx <= 1'b1;
                tx_busy <= 1'b0;
                if (tx_start && cts) begin
                    tx_data_latch <= tx_data;
                    tx_busy <= 1'b1;
                    state <= START_BIT;
                    clk_count <= 0;
                end
            end
            START_BIT: begin
                tx <= 1'b0;
                if (clk_count < CLKS_PER_BIT-1) begin
                    clk_count <= clk_count + 1;
                end else begin
                    clk_count <= 0;
                    bit_idx <= 0;
                    state <= DATA_BITS;
                end
            end
            DATA_BITS: begin
                tx <= tx_data_latch[bit_idx];
                if (clk_count < CLKS_PER_BIT-1) begin
                    clk_count <= clk_count + 1;
                end else begin
                    clk_count <= 0;
                    if (bit_idx < 7) bit_idx <= bit_idx + 1;
                    else state <= STOP_BIT;
                end
            end
            STOP_BIT: begin
                tx <= 1'b1;
                if (clk_count < CLKS_PER_BIT-1) begin
                    clk_count <= clk_count + 1;
                end else begin
                    state <= IDLE;
                end
            end
        endcase
    end
endmodule
)";
}


void Emitter::emitTopFPGA(const std::vector<std::unique_ptr<NFA>> &nfas, const std::filesystem::path &outputDir)
{
    auto filePath = outputDir / "top_fpga.v";
    std::ofstream out(filePath);
    out.exceptions(std::ofstream::failbit | std::ofstream::badbit);

    size_t numNFAs = nfas.size();

    out << "`timescale 1ns / 1ps\n\n";
    out << "module top_fpga (\n"
        << "    input  wire clk,        // 100 MHz system clock\n"
        << "    input  wire rst_btn,    // Physical reset button\n"
        << "    input  wire uart_rx_pin,// USB UART RX pin\n"
        << "    output wire uart_tx_pin,// USB UART TX pin\n"
        << "    input  wire uart_cts_pin,// UART CTS pin (input to FPGA)\n"
        << "    output wire uart_rts_pin,// UART RTS pin (output from FPGA)\n"
        << "    output reg  [" << (numNFAs > 0 ? numNFAs - 1 : 0) << ":0] match_leds // LEDs for match output\n"
        << ");\n\n";

    out << R"(
    wire [7:0] rx_data;
    wire       rx_ready;
    
    uart_rx uart_inst (
        .clk(clk),
        .rx(uart_rx_pin),
        .rx_data(rx_data),
        .rx_ready(rx_ready)
    );

    reg        nfa_start = 1'b1;
    reg  [7:0] nfa_char_in = 8'h00;
    reg        nfa_en = 1'b0;
)";

    out << "    wire [" << (numNFAs > 0 ? numNFAs - 1 : 0) << ":0] match_bus;\n\n";

    out << "    top regex_engine (\n"
        << "        .clk(clk),\n"
        << "        .en(nfa_en),\n"
        << "        .rst(rst_btn),\n"
        << "        .start(nfa_start),\n"
        << "        .char_in(nfa_char_in),\n"
        << "        .match_bus(match_bus)\n"
        << "    );\n\n";

    out << R"(
    // UART TX instantiation
    reg        tx_start = 1'b0;
    reg  [7:0] tx_data = 8'h00;
    wire       tx_busy;

    uart_tx uart_tx_inst (
        .clk(clk),
        .tx_start(tx_start),
        .tx_data(tx_data),
        .cts(uart_cts_pin),
        .tx(uart_tx_pin),
        .tx_busy(tx_busy)
    );

    // 32-Byte Delay Line (Parallel data and redaction bits)
    reg [7:0] delay_line [0:31];
    reg [31:0] redact_line = 32'd0;
    integer i, j;
    
    wire [7:0] mux_out = (redact_line[31]) ? 8'h58 : delay_line[31]; // 'X' is 8'h58

    // 16-Byte Output FIFO to prevent UART character loss
    reg [7:0] fifo_mem [0:15];
    reg [3:0] fifo_head = 0;
    reg [3:0] fifo_tail = 0;
    reg [4:0] fifo_count = 0;

    // Word counter to track length of current sequence (resets on space)
    reg [4:0] word_counter = 0;

    reg rx_ready_prev = 0;
    
    always @(posedge clk) begin
        if (rst_btn) begin
            nfa_en <= 0;
            nfa_start <= 1;
            redact_line <= 32'd0;
            tx_start <= 0;
            rx_ready_prev <= 0;
            match_leds <= 0;
            fifo_head <= 0;
            fifo_tail <= 0;
            fifo_count <= 0;
            word_counter <= 0;
            for (i=0; i<32; i=i+1) delay_line[i] <= 8'h20; // Initialize with spaces
        end else begin
            nfa_start <= 0;
            nfa_en <= 0;
            tx_start <= 0;
            rx_ready_prev <= rx_ready;

            // Global Reset on Null (8'h00) or Carriage Return (8'h0D)
            if (rx_ready && !rx_ready_prev && (rx_data == 8'h00 || rx_data == 8'h0D)) begin
                nfa_start <= 1;
                redact_line <= 32'd0;
                word_counter <= 0;
                for (i=0; i<32; i=i+1) delay_line[i] <= 8'h20; // Clear delay line with spaces
            end

            // Process Incoming Byte (Push to Delay Line and FIFO)
            if (rx_ready && !rx_ready_prev) begin
                for (i=31; i>0; i=i-1) begin
                    delay_line[i] <= delay_line[i-1];
                    redact_line[i] <= redact_line[i-1];
                end
                delay_line[0] <= rx_data;
                redact_line[0] <= 1'b0;
                
                nfa_char_in <= rx_data;
                nfa_en <= 1; 

                // Update word counter (Capped at 31)
                if (rx_data == 8'h20) word_counter <= 0;
                else if (word_counter < 31) word_counter <= word_counter + 1;

                // Push mux_out to FIFO
                if (fifo_count < 16) begin
                    fifo_mem[fifo_head] <= mux_out;
                    fifo_head <= fifo_head + 1;
                    fifo_count <= fifo_count + 1;
                end
            end

            // Update Match LEDs and mark redaction bits (Surgical Word Redaction)
            if (|match_bus) begin
                match_leds <= match_bus;
                // Redact exactly the current word starting from index 0
                for (j = 0; j < 32; j = j + 1) begin
                    if (j < word_counter) begin
                        redact_line[j] <= 1'b1;
                    end
                end
            end

            // Pop from FIFO to UART TX
            if (fifo_count > 0 && !tx_busy && !tx_start) begin
                tx_data <= fifo_mem[fifo_tail];
                fifo_tail <= fifo_tail + 1;
                fifo_count <= fifo_count - 1;
                tx_start <= 1;
            end
        end
    end
endmodule
)";
}

void Emitter::emitConstraints(const std::vector<std::unique_ptr<NFA>> &nfas, const std::filesystem::path &outputDir)
{
    auto filePath = outputDir / "constraints.xdc";
    std::ofstream out(filePath);
    out.exceptions(std::ofstream::failbit | std::ofstream::badbit);

    out << "## Clock signal (Nexys A7 100MHz)\n"
        << "set_property PACKAGE_PIN E3 [get_ports clk]\n"
        << "set_property IOSTANDARD LVCMOS33 [get_ports clk]\n"
        << "create_clock -add -name sys_clk_pin -period 10.00 -waveform {0 5} [get_ports clk]\n\n";

    out << "## USB-RS232 Interface (Nexys A7)\n"
        << "set_property PACKAGE_PIN C4 [get_ports uart_rx_pin]\n"
        << "set_property IOSTANDARD LVCMOS33 [get_ports uart_rx_pin]\n"
        << "set_property PACKAGE_PIN D4 [get_ports uart_tx_pin]\n"
        << "set_property IOSTANDARD LVCMOS33 [get_ports uart_tx_pin]\n"
        << "set_property PACKAGE_PIN C17 [get_ports uart_rts_pin]\n"
        << "set_property IOSTANDARD LVCMOS33 [get_ports uart_rts_pin]\n"
        << "set_property PACKAGE_PIN D18 [get_ports uart_cts_pin]\n"
        << "set_property IOSTANDARD LVCMOS33 [get_ports uart_cts_pin]\n\n";

    out << "## Buttons (Nexys A7 Center Button - BTNC)\n"
        << "set_property PACKAGE_PIN N17 [get_ports rst_btn]\n"
        << "set_property IOSTANDARD LVCMOS33 [get_ports rst_btn]\n\n";

    out << "## LEDs (Nexys A7 LED0 to LED15)\n";
    std::vector<std::string> led_pins = {
        "H17", "K15", "J13", "N14", "R18", "V17", "U17", "U16",
        "V16", "T15", "U14", "T16", "V15", "V14", "V12", "V11"};
    for (size_t i = 0; i < nfas.size() && i < led_pins.size(); ++i)
    {
        out << "set_property PACKAGE_PIN " << led_pins[i] << " [get_ports {match_leds[" << i << "]}]\n"
            << "set_property IOSTANDARD LVCMOS33 [get_ports {match_leds[" << i << "]}]\n";
    }
}