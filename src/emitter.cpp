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

// =============================================================================
// Emitter::emit — orchestration
// =============================================================================
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
    emitUARTRX(outputDir);        // uart_rx.v
    emitUARTTX(outputDir);        // uart_tx.v
    emitFIFO(outputDir);          // uart_rx_fifo.v
    emitTopFPGA(nfas, outputDir); // top_fpga.v
    emitConstraints(nfas, outputDir);
    emitTestbench(nfas, outputDir, testStrings, expectedMatches);
}

// =============================================================================
// emitNFAModule — one file per regex
// =============================================================================
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

    out << "`timescale 1ns / 1ps\n\n";
    out << "// NFA for regex index " << nfa.regexIndex << "\n";
    out << "module nfa_" << nfa.regexIndex << " (\n"
        << "    input  wire       clk,\n"
        << "    input  wire       en,\n"
        << "    input  wire       rst,\n"
        << "    input  wire       start,\n"
        << "    input  wire       end_of_str,\n"
        << "    input  wire [7:0] char_in,\n"
        << "    output reg        match\n"
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

    out << "\n    always @(posedge clk) begin\n"
        << "        if (rst || start) begin\n"
        << "            // Reset to start state (one-hot)\n"
        << "            state_reg <= 1 << " << globalToLocal.at(nfa.startStateId) << ";\n"
        << "        end else if (en) begin\n"
        << "            state_reg <= next_state;\n"
        << "        end\n"
        << "    end\n\n"
        << "    // Match logic: asserted on cycle following end_of_str\n"
        << "    always @(posedge clk) begin\n"
        << "        if (rst || start) begin\n"
        << "            match <= 1'b0;\n"
        << "        end else if (en) begin\n"
        << "            if (end_of_str) begin\n"
        << "                match <= ";

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

    out << ";\n"
        << "            end else begin\n"
        << "                match <= 1'b0;\n"
        << "            end\n"
        << "        end\n"
        << "    end\n\n"
        << "endmodule\n";
}

// =============================================================================
// emitTopModule — top.v wrapper
// =============================================================================
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
        << "    input  wire       end_of_str,\n"
        << "    input  wire [7:0] char_in,\n"
        << "    output wire [" << (nfas.size() - 1) << ":0] match_bus\n"
        << ");\n\n";

    for (size_t i = 0; i < nfas.size(); ++i)
    {
        out << "    nfa_" << nfas[i]->regexIndex << " inst_" << nfas[i]->regexIndex << " (\n"
            << "        .clk(clk), .en(en), .rst(rst), .start(start), .end_of_str(end_of_str), .char_in(char_in), .match(match_bus[" << i << "])\n"
            << "    );\n\n";
    }
    out << "endmodule\n";
}

// =============================================================================
// emitTestbench — tb_top.v
// =============================================================================
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
        << "    reg clk, en, rst, start, end_of_str;\n"
        << "    reg [7:0] char_in;\n"
        << "    wire [" << (numNFAs > 0 ? numNFAs - 1 : 0) << ":0] match_bus;\n\n"
        << "    top uut (.clk(clk), .en(en), .rst(rst), .start(start), .end_of_str(end_of_str), .char_in(char_in), .match_bus(match_bus));\n\n"
        << "    always #5 clk = ~clk;\n\n"
        << "    initial begin\n"
        << "        // synthesis translate_off\n"
        << "        `ifndef SYNTHESIS\n"
        << "        $dumpfile(\"dump.vcd\");\n"
        << "        $dumpvars(0, tb_top);\n"
        << "        `endif\n"
        << "        // synthesis translate_on\n\n"
        << "        clk = 0; en = 1; rst = 1; start = 0; end_of_str = 0; char_in = 0;\n"
        << "        #20 rst = 0; #10;\n\n";

    for (size_t i = 0; i < testStrings.size(); ++i)
    {
        const std::string &s = testStrings[i];
        out << "        // Test case " << i << ": \"" << s << "\"\n"
            << "        start = 1; #10 start = 0;\n";

        for (char c : s)
        {
            out << "        char_in = 8'h" << std::hex << std::setw(2) << std::setfill('0')
                << static_cast<int>(static_cast<unsigned char>(c)) << std::dec << "; #10;\n";
        }

        out << "        end_of_str = 1; #10;\n";
        out << "        // Match output is valid on the cycle immediately following end_of_str assertion\n";

        if (i < expectedMatches.size())
        {
            if (expectedMatches[i].length() != numNFAs)
            {
                throw std::runtime_error(
                    "Testbench generation error: expected_matches[" + std::to_string(i) +
                    "] has length " + std::to_string(expectedMatches[i].length()) +
                    " but " + std::to_string(numNFAs) + " NFAs exist.");
            }
            out << "        if (match_bus === " << numNFAs << "'b" << expectedMatches[i] << ") begin\n"
                << "            $display(\"PASS: Test case " << i << " ('" << s << "') matches expected mask " << expectedMatches[i] << "\");\n"
                << "        end else begin\n"
                << "            $display(\"FAIL: Test case " << i << " ('" << s << "') expected " << expectedMatches[i] << ", got %b\", match_bus);\n"
                << "        end\n";
        }
        else
        {
            out << "        $display(\"INFO: Result for '" << s << "': %b\", match_bus);\n";
        }
        out << "        end_of_str = 0; #10;\n\n";
    }

    out << "        $display(\"All tests completed.\");\n"
        << "        #100; $finish;\n    end\nendmodule\n";
}

// =============================================================================
// emitUARTRX — uart_rx.v
// =============================================================================
void Emitter::emitUARTRX(const std::filesystem::path &outputDir)
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
                    if (rx == 1'b1) rx_ready <= 1'b1;
                    state <= IDLE;
                end
            end
        endcase
    end
endmodule
)";
}

// =============================================================================
// emitUARTTX - uart_tx.v
// =============================================================================
void Emitter::emitUARTTX(const std::filesystem::path &outputDir)
{
    auto filePath = outputDir / "uart_tx.v";
    std::ofstream out(filePath);
    out.exceptions(std::ofstream::failbit | std::ofstream::badbit);

    out << "`timescale 1ns / 1ps\n\n";
    out << R"(
module uart_tx #(
    parameter CLKS_PER_BIT = 868   // 100 MHz / 115200 baud
)(
    input  wire       clk,
    input  wire       rst,
    input  wire [7:0] tx_data,
    input  wire       tx_start,
    output reg        tx_busy,
    output reg        tx
);
    localparam S_IDLE      = 3'd0;
    localparam S_START_BIT = 3'd1;
    localparam S_DATA_BITS = 3'd2;
    localparam S_STOP_BIT  = 3'd3;

    reg [2:0]  state    = S_IDLE;
    reg [9:0]  clk_cnt  = 10'd0;
    reg [2:0]  bit_idx  = 3'd0;
    reg [7:0]  tx_shift = 8'd0;

    always @(posedge clk) begin
        if (rst) begin
            state    <= S_IDLE;
            tx       <= 1'b1;
            tx_busy  <= 1'b0;
            clk_cnt  <= 10'd0;
            bit_idx  <= 3'd0;
            tx_shift <= 8'd0;
        end else begin
            case (state)
                S_IDLE: begin
                    tx      <= 1'b1;
                    tx_busy <= 1'b0;
                    clk_cnt <= 10'd0;
                    bit_idx <= 3'd0;
                    if (tx_start) begin
                        tx_shift <= tx_data;
                        tx_busy  <= 1'b1;
                        state    <= S_START_BIT;
                    end
                end
                S_START_BIT: begin
                    tx <= 1'b0;
                    if (clk_cnt < CLKS_PER_BIT - 1)
                        clk_cnt <= clk_cnt + 1;
                    else begin
                        clk_cnt <= 10'd0;
                        state   <= S_DATA_BITS;
                    end
                end
                S_DATA_BITS: begin
                    tx <= tx_shift[0];
                    if (clk_cnt < CLKS_PER_BIT - 1)
                        clk_cnt <= clk_cnt + 1;
                    else begin
                        clk_cnt  <= 10'd0;
                        tx_shift <= tx_shift >> 1;
                        if (bit_idx < 7)
                            bit_idx <= bit_idx + 1;
                        else begin
                            bit_idx <= 3'd0;
                            state   <= S_STOP_BIT;
                        end
                    end
                end
                S_STOP_BIT: begin
                    tx <= 1'b1;
                    if (clk_cnt < CLKS_PER_BIT - 1)
                        clk_cnt <= clk_cnt + 1;
                    else begin
                        clk_cnt <= 10'd0;
                        state   <= S_IDLE;
                    end
                end
                default: state <= S_IDLE;
            endcase
        end
    end
endmodule
)";
}

// =============================================================================
// emitFIFO - uart_rx_fifo.v
// =============================================================================
void Emitter::emitFIFO(const std::filesystem::path &outputDir)
{
    auto filePath = outputDir / "uart_rx_fifo.v";
    std::ofstream out(filePath);
    out.exceptions(std::ofstream::failbit | std::ofstream::badbit);

    out << "`timescale 1ns / 1ps\n\n";
    out << R"(
module uart_rx_fifo #(
    parameter DEPTH_LOG2 = 4        // depth = 2^DEPTH_LOG2 = 16
)(
    input  wire       clk,
    input  wire       rst,
    input  wire [7:0] wr_data,
    input  wire       wr_en,
    output wire       full,
    output wire [7:0] rd_data,
    input  wire       rd_en,
    output wire       empty
);
    localparam DEPTH = 1 << DEPTH_LOG2;

    reg [7:0] mem [0:DEPTH-1];
    reg [DEPTH_LOG2-1:0] wr_ptr = {DEPTH_LOG2{1'b0}};
    reg [DEPTH_LOG2-1:0] rd_ptr = {DEPTH_LOG2{1'b0}};
    reg [DEPTH_LOG2  :0] count  = {(DEPTH_LOG2+1){1'b0}};

    assign full    = (count == DEPTH[DEPTH_LOG2:0]);
    assign empty   = (count == {(DEPTH_LOG2+1){1'b0}});
    assign rd_data = mem[rd_ptr];

    always @(posedge clk) begin
        if (rst) begin
            wr_ptr <= {DEPTH_LOG2{1'b0}};
            rd_ptr <= {DEPTH_LOG2{1'b0}};
            count  <= {(DEPTH_LOG2+1){1'b0}};
        end else begin
            if (wr_en && !full && rd_en && !empty) begin
                mem[wr_ptr] <= wr_data;
                wr_ptr      <= wr_ptr + 1;
                rd_ptr      <= rd_ptr + 1;
            end else if (wr_en && !full) begin
                mem[wr_ptr] <= wr_data;
                wr_ptr      <= wr_ptr + 1;
                count       <= count + 1;
            end else if (rd_en && !empty) begin
                rd_ptr <= rd_ptr + 1;
                count  <= count - 1;
            end
        end
    end
endmodule
)";
}

// =============================================================================
// emitTopFPGA - top_fpga.v
// =============================================================================
void Emitter::emitTopFPGA(const std::vector<std::unique_ptr<NFA>> &nfas, const std::filesystem::path &outputDir)
{
    auto filePath = outputDir / "top_fpga.v";
    std::ofstream out(filePath);
    out.exceptions(std::ofstream::failbit | std::ofstream::badbit);

    const size_t numNFAs = nfas.size();
    const std::string busHigh = std::to_string(numNFAs > 0 ? numNFAs - 1 : 0);

    // File Header
    out << "`timescale 1ns / 1ps\n\n"
        << "// =============================================================================\n"
        << "// top_fpga.v — FPGA Top-Level\n"
        << "// Regex count: " << numNFAs << "\n"
        << "//\n"
        << "// Architecture:\n"
        << "//   uart_rx → uart_rx_fifo → Control FSM → top (NFA engine)\n"
        << "//                                         → uart_tx → host PC\n"
        << "//\n"
        << "// UART response packet (one line per newline received from host):\n"
        << "//   \"MATCH=<" << numNFAs << "-bit binary> BYTES=<8 hex> HITS=<4 hex per regex,comma-sep>\\r\\n\"\n"
        << "//\n"
        << "// Send '?' (0x3F) to query counters without feeding the NFA.\n"
        << "// =============================================================================\n\n";

    // Module Declaration
    out << "module top_fpga #(\n"
        << "    parameter NUM_REGEX    = " << numNFAs << ",\n"
        << "    parameter CLKS_PER_BIT = 868   // 100 MHz / 115200 baud\n"
        << ")(\n"
        << "    input  wire clk,\n"
        << "    input  wire rst_btn,\n"
        << "    input  wire uart_rx_pin,\n"
        << "    output wire uart_tx_pin,\n"
        << "    output reg  [" << busHigh << ":0] match_leds\n"
        << ");\n\n";

    // uart_rx
    out << "    // UART RX\n"
        << "    wire [7:0] rx_data;\n"
        << "    wire       rx_ready;\n\n"
        << "    uart_rx #(.CLKS_PER_BIT(CLKS_PER_BIT)) uart_rx_inst (\n"
        << "        .clk     (clk),\n"
        << "        .rx      (uart_rx_pin),\n"
        << "        .rx_data (rx_data),\n"
        << "        .rx_ready(rx_ready)\n"
        << "    );\n\n";

    // FIFO
    out << "    // Input FIFO\n"
        << "    wire [7:0] fifo_rd_data;\n"
        << "    wire       fifo_empty;\n"
        << "    wire       fifo_full;\n"
        << "    reg        fifo_rd_en = 1'b0;\n\n"
        << "    uart_rx_fifo #(.DEPTH_LOG2(4)) rx_fifo (\n"
        << "        .clk    (clk),\n"
        << "        .rst    (rst_btn),\n"
        << "        .wr_data(rx_data),\n"
        << "        .wr_en  (rx_ready && !fifo_full),\n"
        << "        .full   (fifo_full),\n"
        << "        .rd_data(fifo_rd_data),\n"
        << "        .rd_en  (fifo_rd_en),\n"
        << "        .empty  (fifo_empty)\n"
        << "    );\n\n";

    // NFA engine
    out << "    // NFA Engine\n"
        << "    reg                  nfa_start      = 1'b1;\n"
        << "    reg                  nfa_end_of_str = 1'b0;\n"
        << "    reg  [7:0]           nfa_char_in    = 8'h00;\n"
        << "    reg                  nfa_en         = 1'b0;\n"
        << "    wire [" << busHigh << ":0] match_bus;\n\n"
        << "    top regex_engine (\n"
        << "        .clk        (clk),\n"
        << "        .en         (nfa_en),\n"
        << "        .rst        (rst_btn),\n"
        << "        .start      (nfa_start),\n"
        << "        .end_of_str (nfa_end_of_str),\n"
        << "        .char_in    (nfa_char_in),\n"
        << "        .match_bus  (match_bus)\n"
        << "    );\n\n";

    // Hardware Counters
    out << "    // Hardware Counters\n"
        << "    reg [31:0] byte_count = 32'd0;\n"
        << "    // One 16-bit hit counter per regex\n"
        << "    reg [15:0] match_count [0:15];\n"
        << "    integer ci;\n"
        << "    initial begin\n"
        << "        for (ci = 0; ci < 16; ci = ci + 1)\n"
        << "            match_count[ci] = 16'd0;\n"
        << "    end\n\n";

    // uart_tx + TX drain sub-FSM
    out << "    // UART TX & Drain Sub-FSM\n"
        << "    localparam TX_BUF_LEN = 128;\n"
        << "    reg [7:0] tx_buf [0:TX_BUF_LEN-1];\n"
        << "    reg [6:0] tx_len  = 7'd0;\n"
        << "    reg [6:0] tx_ptr  = 7'd0;\n"
        << "    reg       tx_send = 1'b0;  // pulse: start draining tx_buf\n\n"
        << "    wire      tx_busy;\n"
        << "    reg       tx_start_r = 1'b0;\n"
        << "    reg [7:0] tx_data_r  = 8'd0;\n\n"
        << "    uart_tx #(.CLKS_PER_BIT(CLKS_PER_BIT)) uart_tx_inst (\n"
        << "        .clk     (clk),\n"
        << "        .rst     (rst_btn),\n"
        << "        .tx_data (tx_data_r),\n"
        << "        .tx_start(tx_start_r),\n"
        << "        .tx_busy (tx_busy),\n"
        << "        .tx      (uart_tx_pin)\n"
        << "    );\n\n"
        << "    // hex nibble → ASCII character\n"
        << "    function [7:0] hex_char;\n"
        << "        input [3:0] nibble;\n"
        << "        begin\n"
        << "            hex_char = (nibble < 4'd10) ? (8'd48 + nibble) : (8'd55 + nibble);\n"
        << "        end\n"
        << "    endfunction\n\n"
        << "    localparam TX_IDLE = 2'd0, TX_LOAD = 2'd1, TX_WAIT = 2'd2, TX_NEXT = 2'd3;\n"
        << "    reg [1:0] tx_state = TX_IDLE;\n\n"
        << "    always @(posedge clk) begin\n"
        << "        tx_start_r <= 1'b0;\n"
        << "        if (rst_btn) begin\n"
        << "            tx_state <= TX_IDLE;\n"
        << "            tx_ptr   <= 7'd0;\n"
        << "        end else begin\n"
        << "            case (tx_state)\n"
        << "                TX_IDLE: if (tx_send) begin tx_ptr <= 7'd0; tx_state <= TX_LOAD; end\n"
        << "                TX_LOAD: if (!tx_busy) begin\n"
        << "                    tx_data_r  <= tx_buf[tx_ptr];\n"
        << "                    tx_start_r <= 1'b1;\n"
        << "                    tx_state   <= TX_WAIT;\n"
        << "                end\n"
        << "                TX_WAIT: if (tx_busy)  tx_state <= TX_NEXT;\n"
        << "                TX_NEXT: if (!tx_busy) begin\n"
        << "                    if (tx_ptr + 1 < tx_len) begin\n"
        << "                        tx_ptr   <= tx_ptr + 1;\n"
        << "                        tx_state <= TX_LOAD;\n"
        << "                    end else\n"
        << "                        tx_state <= TX_IDLE;\n"
        << "                end\n"
        << "            endcase\n"
        << "        end\n"
        << "    end\n\n";

    // build_response Task
    // The task is parameterised on the actual regex count at code-gen time
    out << "    // build_response Task\n"
        << "    // Fills tx_buf with the ASCII response packet in one clock cycle.\n"
        << "    // Format: \"MATCH=<bits> BYTES=<8hex> HITS=<4hex per regex,csv>\\r\\n\"\n"
        << "    reg [6:0]  bp;         // build pointer (local to task scope)\n"
        << "    reg [15:0] tmp16;\n"
        << "    integer    ri;\n\n"
        << "    task build_response;\n"
        << "        input [" << busHigh << ":0] mbits;\n"
        << "        input [31:0]          bcount;\n"
        << "        integer k;\n"
        << "        begin : build_task\n"
        << "            integer p;\n"
        << "            p = 0;\n"
        << "            // \"MATCH=\"\n"
        << "            tx_buf[p]=8'h4D; p=p+1;  // M\n"
        << "            tx_buf[p]=8'h41; p=p+1;  // A\n"
        << "            tx_buf[p]=8'h54; p=p+1;  // T\n"
        << "            tx_buf[p]=8'h43; p=p+1;  // C\n"
        << "            tx_buf[p]=8'h48; p=p+1;  // H\n"
        << "            tx_buf[p]=8'h3D; p=p+1;  // =\n"
        << "            // match bits, MSB first\n";

    // Unroll the match-bit loop at C++ code-gen time for exact bit count
    for (int b = static_cast<int>(numNFAs) - 1; b >= 0; --b)
    {
        out << "            tx_buf[p] = (mbits[" << b << "]) ? 8'h31 : 8'h30; p=p+1;\n";
    }

    out << "            // \" BYTES=\"\n"
        << "            tx_buf[p]=8'h20; p=p+1;\n"
        << "            tx_buf[p]=8'h42; p=p+1;  // B\n"
        << "            tx_buf[p]=8'h59; p=p+1;  // Y\n"
        << "            tx_buf[p]=8'h54; p=p+1;  // T\n"
        << "            tx_buf[p]=8'h45; p=p+1;  // E\n"
        << "            tx_buf[p]=8'h53; p=p+1;  // S\n"
        << "            tx_buf[p]=8'h3D; p=p+1;  // =\n"
        << "            tx_buf[p]=hex_char(bcount[31:28]); p=p+1;\n"
        << "            tx_buf[p]=hex_char(bcount[27:24]); p=p+1;\n"
        << "            tx_buf[p]=hex_char(bcount[23:20]); p=p+1;\n"
        << "            tx_buf[p]=hex_char(bcount[19:16]); p=p+1;\n"
        << "            tx_buf[p]=hex_char(bcount[15:12]); p=p+1;\n"
        << "            tx_buf[p]=hex_char(bcount[11: 8]); p=p+1;\n"
        << "            tx_buf[p]=hex_char(bcount[ 7: 4]); p=p+1;\n"
        << "            tx_buf[p]=hex_char(bcount[ 3: 0]); p=p+1;\n"
        << "            // \" HITS=\"\n"
        << "            tx_buf[p]=8'h20; p=p+1;\n"
        << "            tx_buf[p]=8'h48; p=p+1;  // H\n"
        << "            tx_buf[p]=8'h49; p=p+1;  // I\n"
        << "            tx_buf[p]=8'h54; p=p+1;  // T\n"
        << "            tx_buf[p]=8'h53; p=p+1;  // S\n"
        << "            tx_buf[p]=8'h3D; p=p+1;  // =\n";

    // Unroll the per-regex hits loop at C++ code-gen time
    for (size_t k = 0; k < numNFAs; ++k)
    {
        out << "            tmp16 = match_count[" << k << "];\n"
            << "            tx_buf[p]=hex_char(tmp16[15:12]); p=p+1;\n"
            << "            tx_buf[p]=hex_char(tmp16[11: 8]); p=p+1;\n"
            << "            tx_buf[p]=hex_char(tmp16[ 7: 4]); p=p+1;\n"
            << "            tx_buf[p]=hex_char(tmp16[ 3: 0]); p=p+1;\n";
        if (k < numNFAs - 1)
            out << "            tx_buf[p]=8'h2C; p=p+1;  // ','\n";
    }

    out << "            tx_buf[p]=8'h0D; p=p+1;  // CR\n"
        << "            tx_buf[p]=8'h0A; p=p+1;  // LF\n"
        << "            tx_len = p[6:0];\n"
        << "        end\n"
        << "    endtask\n\n";

    // Main Control FSM
    out << "    // Main Control FSM\n"
        << "    localparam S_IDLE      = 4'd0;\n"
        << "    localparam S_FETCH     = 4'd1;\n"
        << "    localparam S_DECODE    = 4'd2;\n"
        << "    localparam S_CHAR_LOAD = 4'd3;\n"
        << "    localparam S_CHAR_STEP = 4'd4;\n"
        << "    localparam S_EOL_END   = 4'd5;\n"
        << "    localparam S_EOL_MATCH = 4'd6;\n"
        << "    localparam S_EOL_LATCH = 4'd7;\n"
        << "    localparam S_TX_ARM    = 4'd8;\n"
        << "    localparam S_TX_WAIT   = 4'd9;\n"
        << "    localparam S_RESET_NFA = 4'd10;\n"
