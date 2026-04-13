# Real-Time PII Guard Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Modify the hardware regex emitter to generate an always-on streaming matcher and a 32-byte shift-register redaction buffer for live PII scrubbing.

**Architecture:** The C++ emitter will be updated to output streaming-compatible NFAs (start state always active, immediate match pulse). It will also generate a `uart_tx` module and update `top_fpga.v` to include a 32-byte character delay line and a redaction multiplexer that replaces sensitive matches with 'X's based on a heuristic pattern length.

**Tech Stack:** C++, Verilog, Python, UART

---

### Task 1: Enable Streaming NFA Generation

**Files:**
- Modify: `src/emitter.cpp`

- [ ] **Step 1: Modify start state logic to be "always-on"**
In `src/emitter.cpp`, locate `emitNFAModule`. Find the commented out logic for `localId == 0` (around line 67) and uncomment it so the start state is always active.
```cpp
        // FIX: Start state is always active to allow substring matching
        if (localId == 0)
        {
            terms.push_back("1'b1");
        }
```

- [ ] **Step 2: Modify match logic to pulse immediately**
In `src/emitter.cpp` within `emitNFAModule`, remove the `if (end_of_str)` wrapper around the match assignment so it pulses immediately when an accept state is active.
Change from:
```cpp
        << "    // Match logic: asserted on cycle following end_of_str\n"
        << "    always @(posedge clk) begin\n"
        << "        if (rst || start) begin\n"
        << "            match <= 1'b0;\n"
        << "        end else if (en) begin\n"
        << "            if (end_of_str) begin\n"
        << "                match <= ";
```
To:
```cpp
        << "    // Match logic: asserted immediately on accept state\n"
        << "    always @(posedge clk) begin\n"
        << "        if (rst || start) begin\n"
        << "            match <= 1'b0;\n"
        << "        end else if (en) begin\n"
        << "            match <= ";
```
And remove the corresponding `end else begin match <= 1'b0; end` blocks below it.

- [ ] **Step 3: Commit**
```bash
git add src/emitter.cpp
git commit -m "feat: enable continuous streaming in NFA emission"
```

### Task 2: Add UART TX Generation

**Files:**
- Modify: `src/emitter.h`
- Modify: `src/emitter.cpp`

- [ ] **Step 1: Add emitUARTTx signature**
In `src/emitter.h`, add `static void emitUARTTx(const std::filesystem::path &outputDir);` to the `Emitter` class.

- [ ] **Step 2: Call emitUARTTx in main emit function**
In `src/emitter.cpp`, inside the `emit` function, add a call to `emitUARTTx(outputDir);` right after `emitUART(outputDir);`.

- [ ] **Step 3: Implement emitUARTTx**
In `src/emitter.cpp`, implement the UART transmitter generator:
```cpp
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
                if (tx_start) begin
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
```

- [ ] **Step 4: Commit**
```bash
git add src/emitter.h src/emitter.cpp
git commit -m "feat: generate UART TX module"
```

### Task 3: Implement Redaction Buffer in top_fpga.v

**Files:**
- Modify: `src/emitter.cpp`

- [ ] **Step 1: Add UART TX pin to top_fpga port list**
In `src/emitter.cpp` inside `emitTopFPGA`, update the module definition to include `uart_tx_pin`:
```cpp
    out << "module top_fpga (\n"
        << "    input  wire clk,        // 100 MHz system clock\n"
        << "    input  wire rst_btn,    // Physical reset button\n"
        << "    input  wire uart_rx_pin,// USB UART RX pin\n"
        << "    output wire uart_tx_pin,// USB UART TX pin\n"
        << "    output reg  [" << (numNFAs > 0 ? numNFAs - 1 : 0) << ":0] match_leds // LEDs for match output\n"
        << ");\n\n";
```

- [ ] **Step 2: Generate Redaction Length Heuristic Logic**
Before generating the `top_fpga` body, calculate the heuristic length for each NFA (based on state count) and generate a Verilog function inside `top_fpga`:
```cpp
    out << "    function [4:0] get_redaction_length;\n"
        << "        input [" << (numNFAs > 0 ? numNFAs - 1 : 0) << ":0] matches;\n"
        << "        begin\n"
        << "            get_redaction_length = 5'd0;\n";
    for (size_t i = 0; i < numNFAs; ++i) {
        // Heuristic: number of states - 1, capped at 31
        int len = nfas[i]->states.size() - 1;
        if (len > 31) len = 31;
        out << "            if (matches[" << i << "]) get_redaction_length = 5'd" << len << ";\n";
    }
    out << "        end\n"
        << "    endfunction\n\n";
```

- [ ] **Step 3: Add Delay Line, UART TX, and Mux to top_fpga**
Replace the existing Control State Machine in `top_fpga` with streaming logic:
```cpp
    out << R"(
    // UART TX instantiation
    reg        tx_start = 1'b0;
    reg  [7:0] tx_data = 8'h00;
    wire       tx_busy;

    uart_tx uart_tx_inst (
        .clk(clk),
        .tx_start(tx_start),
        .tx_data(tx_data),
        .tx(uart_tx_pin),
        .tx_busy(tx_busy)
    );

    // 32-Byte Delay Line
    reg [7:0] delay_line [0:31];
    integer i;
    
    reg [4:0] redact_counter = 0;
    wire [7:0] mux_out = (redact_counter > 0) ? 8'h58 : delay_line[31]; // 'X' is 8'h58

    reg rx_ready_prev = 0;
    
    always @(posedge clk) begin
        if (rst_btn) begin
            nfa_en <= 0;
            nfa_start <= 1; // Reset NFA
            redact_counter <= 0;
            tx_start <= 0;
            rx_ready_prev <= 0;
        end else begin
            nfa_start <= 0; // Release NFA reset
            nfa_en <= 0;
            tx_start <= 0;
            rx_ready_prev <= rx_ready;

            // Handle Redaction Counter Decay
            if (redact_counter > 0 && rx_ready && !rx_ready_prev) begin
                redact_counter <= redact_counter - 1;
            end

            // Match Event: Overrides decay to extend the window
            if (|match_bus) begin
                redact_counter <= get_redaction_length(match_bus);
                match_leds <= match_bus; // Visual feedback
            end

            // Process Incoming Byte
            if (rx_ready && !rx_ready_prev) begin
                // Shift delay line
                for (i=31; i>0; i=i-1) delay_line[i] <= delay_line[i-1];
                delay_line[0] <= rx_data;
                
                // Feed NFA
                nfa_char_in <= rx_data;
                nfa_en <= 1; // Pulse NFA enable

                // Transmit the delayed byte out
                tx_data <= mux_out;
                tx_start <= 1;
            end
        end
    end
endmodule
)";
```

- [ ] **Step 4: Update Constraints generation**
In `src/emitter.cpp` inside `emitConstraints`, add the UART TX pin constraint below the RX pin:
```cpp
    out << "set_property PACKAGE_PIN D4 [get_ports uart_tx_pin]\n"
        << "set_property IOSTANDARD LVCMOS33 [get_ports uart_tx_pin]\n\n";
```

- [ ] **Step 5: Commit**
```bash
git add src/emitter.cpp
git commit -m "feat: implement redaction buffer and TX routing in top_fpga"
```

### Task 4: Python Demo Script

**Files:**
- Create: `pii_guard_demo.py`

- [ ] **Step 1: Write Demo Script**
Create `pii_guard_demo.py` in the project root:
```python
import serial
import time
import sys

# Update the port depending on your OS/Setup (e.g., COM3 on Windows)
PORT = '/dev/ttyUSB1' 

try:
    ser = serial.Serial(PORT, 115200, timeout=1)
except Exception as e:
    print(f"Failed to open {PORT}: {e}")
    print("Mocking stream output for demonstration purposes.")
    ser = None

def stream_text(text):
    print(f"Original Stream: {text}\n")
    print("Redacted Stream: ", end="", flush=True)
    
    for char in text:
        if ser:
            ser.write(char.encode())
            scrubbed = ser.read(1).decode()
            print(scrubbed, end="", flush=True)
            time.sleep(0.05) # Visual effect
        else:
            # Mock behavior if FPGA is unplugged
            print(char, end="", flush=True)
            time.sleep(0.05)
    
    print("\n\nDone.")

if __name__ == "__main__":
    demo_text = "Hello! My CC is 4111222233334444 and my SSN is 123-45-6789. Contact user@example.com."
    stream_text(demo_text)
```

- [ ] **Step 2: Commit**
```bash
git add pii_guard_demo.py
git commit -m "feat: add python pii guard streaming demo script"
```
