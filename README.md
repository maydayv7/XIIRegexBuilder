# XIIRegexBuilder: FPGA-Accelerated Regular Expression Matching Engine

Group Number: 6

## 1. System Description

Our project is a custom hardware accelerator for high-speed text processing. A C++ compiler translates regular expressions into parallel, one-hot encoded hardware Finite State Machines (FSMs) in Verilog. These FSMs are synthesised onto an FPGA to parse continuous ASCII character streams, bypassing the sequential bottleneck of software-based regex engines.

The host PC communicates with the FPGA over a standard USB-UART serial link at 115200 baud. An included Python terminal UI (`tui.py`) lets you type strings interactively and see per-regex match results, cumulative byte counts, and per-regex hit counters rendered in a colour-coded table.

## 2. Use Cases

- **FIX Protocol Parsing**: High-speed filtering of electronic trading messages (orders/fills) to route data before it reaches the software stack.
- **Market Data Feed Filtering**: Scanning millions of events per second to discard irrelevant instrument data at line-rate, saving CPU cycles.
- **Trade Surveillance**: Detecting malicious network patterns (e.g., spoofing, wash trading) continuously across live, high-speed data flows.

## 3. FPGA Relevance

- **Massive Parallelism**: An FPGA evaluates all N regex FSMs simultaneously in a single clock cycle, whereas software throughput degrades linearly as N grows.
- **Strict Determinism**: Hardware matching ensures a fixed, predictable number of clock cycles per match, eliminating OS scheduling and cache-miss latency variations.

## 4. System Architecture

```text
[Host PC]
    │  USB-UART 115200-8N1
    ▼
uart_rx  ──►  uart_rx_fifo (16-byte circular FIFO)
                   │
                   ▼
             Control FSM  ──►  top.v  (parallel NFA engine)
                   │                │
                   │           match_bus[N-1:0]
                   │           byte_count[31:0]
                   │           match_count_k[15:0]
                   ▼
             TX Serializer  ──►  uart_tx  ──►  [Host PC]
```

### Hardware Modules

| File                    | Description                                                         |
| ----------------------- | ------------------------------------------------------------------- |
| `output/uart_tx.v`      | 8-N-1 UART transmitter (parallel-in / serial-out)                   |
| `output/uart_rx_fifo.v` | 16-byte circular FIFO (distributed RAM) between RX and engine       |
| `output/top_fpga.v`     | Top-level integrating FIFO, NFA engine, counters, TX serializer FSM |

### Response Packet (FPGA → Host)

Every time the NFA engine finishes a string (newline received), the FPGA sends one ASCII line:

```text
MATCH=<N-bit binary> BYTES=<8 hex digits> HITS=<4hex per regex, comma-separated>\r\n
