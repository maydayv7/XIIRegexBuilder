# Ethernet Migration & Benchmarking Improvement Plan

**Project:** XIIRegexBuilder — FPGA-Accelerated Regex Matcher  
**Board:** Nexys A7 (Artix-7), LAN8720A PHY  
**Host OS:** Linux (direct Ethernet cable, static IP)  
**Goal:** Replace the UART data path with UDP/Ethernet for ~1,000× throughput improvement, and build a comprehensive benchmarking suite with accurate latency/throughput separation.

---

## Table of Contents

1. [Scope — What Changes vs. What Stays](#1-scope)
2. [Phase 1 — Ethernet IP Core Selection](#2-phase-1--ethernet-ip-core)
3. [Phase 2 — Network & Packet Protocol Design](#3-phase-2--network--packet-protocol)
4. [Phase 3 — New FPGA Verilog Modules](#4-phase-3--new-fpga-verilog-modules)
5. [Phase 4 — emitter.cpp Changes](#5-phase-4--emittercpp-changes)
6. [Phase 5 — Benchmarking Suite](#6-phase-5--benchmarking-suite)
7. [Phase 6 — synth.tcl Updates](#7-phase-6--synthtcl-updates)
8. [Phase 7 — Recommended Implementation Order](#8-phase-7--recommended-implementation-order)
9. [Open Questions to Confirm Before Starting](#9-open-questions)

---

## 1. Scope

### What Changes

| File / Component | Change |
|---|---|
| `output-eg/top_fpga.v` (and `emitter.cpp` that generates it) | Replace UART RX path with Ethernet MAC + UDP stack; keep UART TX for programming |
| `output-eg/constraints.xdc` | Add LAN8720A PHY pin assignments and clock constraints |
| `emitter.cpp` / `emitter.h` | Emit new `top_fpga.v`, `packet_parser_fsm.v`, `result_assembler.v` |
| `benchmarks/bench_fpga_uart.py` | Kept as-is; demoted to optional UART baseline runner |
| `benchmarks/bench_fpga_eth.py` | **New** — Python quick-test + correctness client |
| `benchmarks/bench_fpga_eth.cpp` | **New** — C++ peak-performance client (Linux POSIX sockets) |
| `benchmarks/run_all.sh` | **Overhauled** — unified comparison suite with summary table |
| `scripts/synth.tcl` | Add `read_verilog` calls for `verilog-ethernet` library + new modules |
| `lib/verilog-ethernet/` | **New directory** — Forencich library, checked in verbatim |

### What Does Not Change

| File / Component | Reason |
|---|---|
| `output-eg/top.v` and all `nfa_N.v` | NFA engine interface is fully preserved |
| `processor/uart.v` | UART stays for programming instruction memory |
| `processor/src/prog_fpga.py` | UART programming path untouched |
| `src/` (lexer, parser, NFA, emitter logic) | Regex compilation pipeline unchanged |
| `benchmarks/bench_cpp.cpp`, `bench_python.py` | Software baselines unchanged |

### The NFA Engine Interface (Preserved Exactly)

The existing `top.v` interface that the new Ethernet path must drive identically to the current UART path:

```verilog
module top (
    input  wire       clk,          // 100 MHz system clock
    input  wire       en,           // enable
    input  wire       rst,          // reset
    input  wire       start,        // pulse HIGH for 1 clk before first char
    input  wire       end_of_str,   // pulse HIGH for 1 clk after last char
    input  wire [7:0] char_in,      // one ASCII byte per clock cycle
    output wire [5:0] match_bus     // one bit per regex, valid 1 clk after end_of_str
);
```

The new `packet_parser_fsm.v` must drive these signals exactly as the current UART control FSM does. Nothing else about the NFA changes.

---

## 2. Phase 1 — Ethernet IP Core

### 2.1 Why Forencich's `verilog-ethernet`

The Nexys A7's **LAN8720A PHY** runs **RMII at 100 Mbps**, giving ~12 MB/s usable UDP bandwidth — roughly **1,000× more** than UART at 115,200 baud (~11.5 KB/s). Forencich's `verilog-ethernet` library (MIT licensed) has a proven, Artix-7-tested module for exactly this scenario: `eth_mac_mii_fifo.v`, which handles the MII clock domain crossing internally so your 100 MHz system logic never has to deal with the 25 MHz MII clocks directly.

Clone into your repo:

```bash
git clone https://github.com/alexforencich/verilog-ethernet lib/verilog-ethernet
```

### 2.2 Required Library Files

Place under `lib/verilog-ethernet/rtl/`. You need these specific modules:

| File | Purpose |
|---|---|
| `eth_mac_mii_fifo.v` | Top-level MII MAC with async FIFOs and CDC |
| `eth_mac_mii.v` | Core MII MAC |
| `mii_phy_if.v` | PHY interface shim |
| `eth_axis_rx.v` / `eth_axis_tx.v` | Ethernet frame ↔ AXI-Stream adapters |
| `arp.v`, `arp_cache.v` | ARP responder (Linux auto-resolves FPGA MAC from its IP) |
| `ip.v`, `ip_eth_rx.v`, `ip_eth_tx.v` | IP layer |
| `udp.v`, `udp_ip_rx.v`, `udp_ip_tx.v` | UDP layer |
| `axis_fifo.v` | Generic AXI-Stream FIFO used throughout |

All modules connect via **AXI-Stream** (`tdata / tvalid / tready / tlast`). Each layer hands a byte stream to the next — no custom glue logic needed.

### 2.3 Clock Architecture

The LAN8720A requires a **50 MHz reference clock driven from the FPGA** on `ETH_REFCLK`. Derive this from the existing 100 MHz system clock using a MMCM primitive. The resulting clock domains are:

| Domain | Frequency | Driven By | Used For |
|---|---|---|---|
| `clk_100` | 100 MHz | Board oscillator | All system logic, NFA engine |
| `clk_50` | 50 MHz | MMCM from `clk_100` | Output to LAN8720A `ETH_REFCLK` |
| `clk_25_rx` | 25 MHz | Input from PHY | MII RX — internal to `eth_mac_mii_fifo` |
| `clk_25_tx` | 25 MHz | FPGA-driven | MII TX — internal to `eth_mac_mii_fifo` |

The CDC between the 25 MHz MII clocks and `clk_100` is entirely internal to `eth_mac_mii_fifo`. Your new control logic only ever touches `clk_100`.

### 2.4 PHY Reset Sequence

The LAN8720A requires a hardware reset on power-up. Add a simple FSM in `top_fpga.v`:

| State | Duration | Action |
|---|---|---|
| `RESET_HOLD` | 10 ms (1,000,000 cycles @ 100 MHz) | Assert `eth_rstn = 0` |
| `RESET_RELEASE` | 5 ms (500,000 cycles) | Deassert `eth_rstn = 1`, wait for PHY to initialise |
| `RUNNING` | — | Normal operation, enable MAC |

---

## 3. Phase 2 — Network & Packet Protocol

### 3.1 Static IP Configuration

Using `192.168.2.x` (avoids collision with typical `192.168.1.x` home router subnets):

| Parameter | Value |
|---|---|
| FPGA IP | `192.168.2.10` |
| FPGA MAC | `02:00:00:00:00:01` (locally administered, safe) |
| Host (Linux) IP | `192.168.2.100` |
| FPGA listen port (RX) | `7777` |
| FPGA response port (TX) | `7778` |

**Linux host setup** (run once, or add to `/etc/network/interfaces`):

```bash
sudo ip addr add 192.168.2.100/24 dev eth0
sudo ip link set eth0 up
```

Linux will ARP for the FPGA's MAC automatically when the first packet is sent. The FPGA's ARP module (from `verilog-ethernet`) handles the reply.

### 3.2 Adaptive Batching Strategy

To optimise both throughput and latency realistically, use a **dual-trigger batching policy** — the same approach used in production network stacks:

A packet is transmitted when **either** condition is met, whichever comes first:
1. **Size trigger**: payload reaches **1,400 bytes** (leaves headroom below the 1,472 byte UDP MTU limit)
2. **Coalescing timer**: **200 µs** has elapsed since the first string was added to the current batch

At high load, the size trigger dominates → maximum throughput.  
At low load or end of dataset, the timer fires → bounded latency, no strings stranded in a buffer.  
Both thresholds are configurable at runtime via CLI flags.

### 3.3 Packet Format

#### Host → FPGA (Request Packet)

```
 0        1        2        3        4        5
 ┌────────┬────────┬────────┬────────┬────────┬────────┐
 │           SEQ_NUM (uint32 BE)     │  NUM_STRINGS    │
 └────────┴────────┴────────┴────────┴────────┴────────┘
 6+  [ STR_LEN : 1 byte ][ STRING_DATA : STR_LEN bytes ] × NUM_STRINGS
```

| Field | Size | Description |
|---|---|---|
| `SEQ_NUM` | 4 bytes | Monotonically increasing packet counter |
| `NUM_STRINGS` | 2 bytes | Number of strings packed in this packet |
| `STR_LEN` | 1 byte per string | Length of the following string (max 255) |
| `STRING_DATA` | `STR_LEN` bytes | ASCII characters, no null terminator |

- Max payload: **1,472 bytes** (1,500 MTU − 20 IP − 8 UDP)
- Strings packed greedily up to 1,400 bytes, then size trigger fires

#### FPGA → Host (Response Packet)

```
 0        1        2        3        4        5        6–9
 ┌────────┬────────┬────────┬────────┬────────┬────────┬──────────────────┐
 │           SEQ_NUM (uint32 BE)     │  NUM_RESULTS    │ FPGA_CYCLE_STAMP │
 └────────┴────────┴────────┴────────┴────────┴────────┴──────────────────┘
 10+  [ MATCH_BITS : 1 byte ][ HIT_COUNTS : NUM_REGEX × 2 bytes ] × NUM_RESULTS
```

| Field | Size | Description |
|---|---|---|
| `SEQ_NUM` | 4 bytes | Echoed from request (enables RTT measurement + reorder detection) |
| `NUM_RESULTS` | 2 bytes | Equals `NUM_STRINGS` from the corresponding request |
| `FPGA_CYCLE_STAMP` | 4 bytes | Free-running 100 MHz counter snapshot at TX time |
| `MATCH_BITS` | 1 byte per result | One bit per regex (bit 0 = regex 0, etc.) |
| `HIT_COUNTS` | `NUM_REGEX × 2` bytes per result | Per-regex cumulative match count (matches current UART format) |

#### Why `FPGA_CYCLE_STAMP` Matters

This field enables separating FPGA compute time from network transit time:

```
FPGA processing time = (FPGA_CYCLE_STAMP_rx - FPGA_CYCLE_STAMP_tx) × 10 ns
Network RTT          = (host_recv_wall_time - host_send_wall_time) - FPGA_processing_time
```

Without this, you can only measure end-to-end RTT and cannot tell whether latency is coming from the NFA computation or from network/host overhead.

### 3.4 Ordering & Correctness Guarantee

Since results must map back to input strings for `--verify` correctness checking:

- Host assigns `SEQ_NUM` in strict ascending order, one per packet
- Within a packet, strings are processed sequentially through the single NFA pipeline — internal order is guaranteed by hardware
- Host response collector maintains a `results[seq_num]` map and drains it in strict SEQ_NUM order before writing output — no result is committed until all lower SEQ_NUMs have arrived
- Any packet unanswered after **500 ms** is retransmitted once with the same SEQ_NUM
- Persistent loss (after retransmit) is flagged explicitly rather than silently producing corrupt output

On a direct cable with static IP, loss and reordering essentially never occur — but the mechanism makes correctness unconditional.

---

## 4. Phase 3 — New FPGA Verilog Modules

### 4.1 New Signal Flow in `top_fpga.v`

```
ETH PHY (LAN8720A, RMII pins on Nexys A7)
   ↕
eth_mac_mii_fifo        ← handles MII 25MHz clocks, FIFOs, CDC internally
   ↕  AXI-Stream
udp_rx                  ← strips IP/UDP headers, exposes raw payload
   ↕  UDP payload bytes
packet_parser_fsm       ← NEW — drives top.v exactly as UART FSM did
   ↕  clk/en/rst/start/end_of_str/char_in
top.v  (NFA engine)     ← COMPLETELY UNCHANGED
   ↕  match_bus[5:0]
result_assembler        ← NEW — buffers results, assembles response packet
   ↕  AXI-Stream
udp_tx                  ← wraps result in IP/UDP headers
   ↕
eth_mac_mii_fifo (TX)   ← same MAC instance, TX path

uart_tx                 ← KEPT — feeds prog_fpga.py instruction memory loads
```

### 4.2 `packet_parser_fsm.v` — State Machine

This is the most critical new piece of Verilog. It replaces the UART-based character feeding logic in the current `top_fpga.v` control FSM.

#### Port Interface

```verilog
module packet_parser_fsm #(
    parameter NUM_REGEX = 6
)(
    input  wire        clk,
    input  wire        rst,

    // UDP payload input (AXI-Stream from udp_rx)
    input  wire [7:0]  udp_payload_tdata,
    input  wire        udp_payload_tvalid,
    input  wire        udp_payload_tlast,
    output reg         udp_payload_tready,

    // NFA engine interface (drives top.v — identical to current UART FSM outputs)
    output reg         nfa_en,
    output reg         nfa_rst,
    output reg         nfa_start,
    output reg         nfa_end_of_str,
    output reg  [7:0]  nfa_char_in,

    // Result capture interface (to result_assembler)
    input  wire [NUM_REGEX-1:0]        match_bus,       // from top.v
    output reg                         result_valid,    // pulse when result ready
    output reg  [NUM_REGEX-1:0]        result_match,
    output reg  [31:0]                 result_seq_num,
    output reg  [15:0]                 result_num_strings,

    // Cycle counter (for FPGA_CYCLE_STAMP in response)
    input  wire [31:0] cycle_counter
);
```

#### State Encoding

| State | Action |
|---|---|
| `IDLE` | Wait for `udp_payload_tvalid`. Assert `tready`. |
| `READ_SEQ_0..3` | Consume 4 bytes, assemble `seq_num` register (big-endian) |
| `READ_NUM_STR_HI` | Consume byte → `num_strings[15:8]` |
| `READ_NUM_STR_LO` | Consume byte → `num_strings[7:0]`; init `str_idx = 0` |
| `READ_STR_LEN` | Consume 1 byte → latch `cur_str_len`; init `char_idx = 0` |
| `STREAM_CHAR` | Feed `udp_payload_tdata` to `nfa_char_in`; pulse `nfa_start` when `char_idx == 0`; advance `char_idx` each clock |
| `WAIT_NFA_DONE` | After last char of string, pulse `nfa_end_of_str`; wait 1 clock for `match_bus` valid |
| `STORE_RESULT` | Latch `match_bus` into local result buffer; pulse `result_valid`; increment `str_idx` |
| `NEXT_OR_DONE` | If `str_idx < num_strings` → `READ_STR_LEN`; else → `TRIGGER_TX` |
| `TRIGGER_TX` | Pulse `assemble_send` to `result_assembler`; return to `IDLE` |

> **Key point:** `STREAM_CHAR` drives `nfa_char_in` and `nfa_start`/`nfa_end_of_str` with exactly the same timing as the current UART control FSM. The NFA does not know or care that its data now comes from UDP instead of UART.

### 4.3 `result_assembler.v` — Design

A buffer module that accumulates per-string results and fires them as a single UDP response packet.

#### Behaviour

- Accepts `(result_match, result_seq_num)` entries from `packet_parser_fsm` one per clock via `result_valid` pulse
- Stores entries in an internal buffer (256 entries deep — safely fits in one BRAM18 block)
- On `assemble_send` pulse: constructs the full response packet in AXI-Stream format:
  1. Header: `SEQ_NUM (4B) | NUM_RESULTS (2B) | FPGA_CYCLE_STAMP (4B)`
  2. Per result: `MATCH_BITS (1B) | HIT_COUNTS (NUM_REGEX × 2B)`
- Streams the assembled packet out via AXI-Stream to `udp_tx`

#### BRAM Usage

With 6 regexes: each result entry = 1 byte match + 6 × 2 byte hit counts = 13 bytes.  
256 entries × 13 bytes = ~3.3 KB → fits in two BRAM18 blocks. Nexys A7 has 50 BRAM36 blocks total — negligible usage.

### 4.4 Free-Running Cycle Counter

Add to `top_fpga.v`:

```verilog
reg [31:0] cycle_counter;
always @(posedge clk) begin
    if (rst) cycle_counter <= 32'b0;
    else     cycle_counter <= cycle_counter + 1;
end
```

This wraps every ~42.9 seconds at 100 MHz — more than sufficient for any single benchmark run. Pass it to both `packet_parser_fsm` (for per-string timing) and `result_assembler` (for the response timestamp).

### 4.5 `constraints.xdc` Additions

Add after the existing UART constraints:

```tcl
## LAN8720A Ethernet PHY (Nexys A7)
set_property PACKAGE_PIN C9  [get_ports eth_mdc]
set_property PACKAGE_PIN A9  [get_ports eth_mdio]
set_property PACKAGE_PIN D9  [get_ports eth_rstn]
set_property PACKAGE_PIN B3  [get_ports eth_crsdv]
set_property PACKAGE_PIN C3  [get_ports eth_rxerr]
set_property PACKAGE_PIN C10 [get_ports {eth_rxd[0]}]
set_property PACKAGE_PIN C11 [get_ports {eth_rxd[1]}]
set_property PACKAGE_PIN D10 [get_ports eth_txen]
set_property PACKAGE_PIN A10 [get_ports {eth_txd[0]}]
set_property PACKAGE_PIN A8  [get_ports {eth_txd[1]}]
set_property PACKAGE_PIN D11 [get_ports eth_refclk]

set_property IOSTANDARD LVCMOS33 [get_ports {eth_mdc eth_mdio eth_rstn eth_crsdv
                                              eth_rxerr eth_rxd eth_txen eth_txd
                                              eth_refclk}]

## Ethernet clock constraints
create_clock -period 40.0 -name eth_rx_clk [get_ports eth_crsdv]
set_clock_groups -asynchronous \
    -group [get_clocks sys_clk_pin] \
    -group [get_clocks eth_rx_clk]
set_false_path -from [get_clocks eth_rx_clk] -to [get_clocks sys_clk_pin]
set_false_path -from [get_clocks sys_clk_pin] -to [get_clocks eth_rx_clk]
```

> **Always cross-check these pin numbers against the [Digilent Nexys A7 master XDC file](https://github.com/Digilent/digilent-xdc) before synthesis.** The names above match the documented Nexys A7 Ethernet pinout, but one wrong pin wastes a full synthesis run (~10 minutes).

---

## 5. Phase 4 — `emitter.cpp` Changes

The emitter generates `top_fpga.v`. Two targeted modifications are required.

### 5.1 Update `emitTopFPGA()`

1. **Port list**: remove `uart_rx_pin`; add all `eth_*` ports; keep `uart_tx_pin`
2. **Internal instantiations**: remove `uart_rx_inst`, `rx_fifo`, and UART-based control FSM; add `eth_mac_mii_fifo`, `udp_rx`, `udp_tx`, `packet_parser_fsm`, `result_assembler`, `phy_reset_fsm`, `cycle_counter`
3. **NFA instantiation block**: copy verbatim — zero changes
4. **Top-level parameters**: emit `LOCAL_IP`, `LOCAL_MAC`, `LOCAL_PORT`, `HOST_PORT`, `NUM_REGEX` as Verilog `parameter` declarations

### 5.2 Add Two New Emit Methods

```cpp
// In emitter.h
static void emitPacketParserFSM(const std::filesystem::path &outputDir, int numRegex);
static void emitResultAssembler(const std::filesystem::path &outputDir, int numRegex);
```

Both methods are parametric on `numRegex` for field width sizing in the generated Verilog. They write `packet_parser_fsm.v` and `result_assembler.v` into `outputDir`.

The `verilog-ethernet` library files are static — they live in `lib/verilog-ethernet/rtl/` and are never touched by the emitter.

---

## 6. Phase 5 — Benchmarking Suite

### Recommended Structure: Separate Tools

The recommended structure is **two separate tools** rather than a unified CLI. The motivation:

- Python is fast to iterate, easy to read, and sufficient for correctness checking and day-to-day use
- C++ eliminates interpreter overhead and GIL constraints, giving you the actual hardware ceiling
- A unified tool would add complexity (argument routing, build system coupling) with no practical benefit when `run_all.sh` orchestrates them anyway

---

### 6.1 Python Quick-Test Client: `bench_fpga_eth.py`

**Purpose:** fast iteration, correctness verification, human-readable output. Not intended to measure peak throughput (Python socket overhead caps it well below the hardware ceiling — and that's fine).

#### CLI

```bash
python3 benchmarks/bench_fpga_eth.py \
    [--fpga-ip    192.168.2.10]   \   # FPGA static IP
    [--fpga-port  7777]           \   # FPGA listen port
    [--host-port  7778]           \   # port to bind on host
    [--input      inputs/large_test_strings.txt] \
    [--regex      inputs/regexes.txt]            \
    [--window     16]             \   # sliding window: packets in flight
    [--coalesce   200]            \   # batch coalescing timer (µs)
    [--max-batch  1400]           \   # size trigger (bytes)
    [--warmup     50]             \   # warmup packets (not measured)
    [--timeout    0.5]            \   # retransmit timeout (seconds)
    [--verify]                        # compare against C++ golden output
```

#### Internal Architecture

```
test_strings.txt
     │
     ▼
  Packer                   builds packets using dual-trigger batching
     │ list of (seq_num, packet_bytes, string_list)
     ▼
  Sliding Window Sender    select()-based loop
     │                     sends new packets while len(in_flight) < window
     │                     in_flight: dict[seq_num → (send_time_ns, strings)]
     ▼
  UDP socket               Linux AF_INET / SOCK_DGRAM
     │
     ▼  (response arrives)
  Receiver                 same select() call
     │                     parses SEQ_NUM, NUM_RESULTS, FPGA_CYCLE_STAMP
     │                     moves seq_num from in_flight → completed
     ▼
  Ordered Drain            sorts completed by SEQ_NUM
     │                     writes eth_matches.txt in same format as uart_matches.txt
     ▼
  [--verify]               diffs eth_matches.txt vs python_matches.txt
                           reports mismatches: string index, content, expected/got bitmask
```

#### Metrics Output

```
=== FPGA Ethernet Benchmark (Python) ===
Strings tested        : 10,000
Total wall time       : 0.41 s
Throughput            : 24,390 strings/sec
Data sent             : 6.8 MB  →  16.6 MB/s
Data received         : 2.1 MB  →   5.1 MB/s
Packet loss           : 0 / 612 packets (0.00%)
Retransmits           : 0

FPGA Compute Time (from cycle counter):
  Total               : 22.1 ms
  Per string avg      : 2.2 µs
  Per string min/max  : 0.4 µs / 8.3 µs

Network RTT (excluding FPGA compute):
  min    :  0.18 ms
  mean   :  0.51 ms
  p50    :  0.46 ms
  p95    :  0.89 ms
  p99    :  1.24 ms
  max    :  4.10 ms

Batch size distribution:
  mean strings/packet : 16.3
  min / max           : 1 / 24

vs. UART baseline (theoretical @ 115200 baud):
  Estimated UART time : 87.3 s
  Speedup             : 213×
```

---

### 6.2 C++ Peak-Performance Client: `bench_fpga_eth.cpp`

**Purpose:** measure the true hardware ceiling with minimal host-side overhead. Uses POSIX sockets on Linux.

#### Build

```bash
g++ -O3 -std=c++17 -o benchmarks/bench_fpga_eth benchmarks/bench_fpga_eth.cpp
```

No external dependencies — standard Linux headers only (`sys/socket.h`, `netinet/in.h`, `arpa/inet.h`, `pthread.h`).

#### Design Decisions

**Two threads with a lock-free ring buffer:**

```
Sender Thread                          Receiver Thread
─────────────────────────────────      ─────────────────────────────────
while packets_remain or in_flight > 0: while not done:
    while in_flight < W                    ret = recvfrom(sock, buf)
         and packets_remain:               if ret > 0:
        sendto(sock, pkt[i])                   recv_ns = clock_gettime()
        send_ns[seq] = clock_gettime()          parse SEQ_NUM, CYCLE_STAMP
        ring_push(seq)                          store result[seq]
        in_flight++ (atomic)                    in_flight-- (atomic)
        i++                                     advance base_seq if contiguous
    usleep(10)   # yield to receiver
```

The ring buffer is a fixed-size `std::array` with `std::atomic<uint32_t>` head and tail — no mutex, no allocation in the hot path.

**Pre-serialised packets:**  
All packets are packed into a contiguous `std::vector<uint8_t>` during startup. The hot send loop is just `sendto()` with a pointer + offset — zero string formatting, zero heap allocation during benchmarking.

**Socket tuning (Linux):**

```cpp
int rcvbuf = 4 * 1024 * 1024;  // 4 MB receive buffer
int sndbuf = 4 * 1024 * 1024;  // 4 MB send buffer
setsockopt(sock, SOL_SOCKET, SO_RCVBUF, &rcvbuf, sizeof(rcvbuf));
setsockopt(sock, SOL_SOCKET, SO_SNDBUF, &sndbuf, sizeof(sndbuf));
```

Without this, the Linux kernel drops packets at high packet rates silently. At 100 Mbps with ~100 byte average packets, you can hit >100k packets/sec — the default 212 KB socket buffer fills in ~2 ms.

**Timing:**  
`clock_gettime(CLOCK_MONOTONIC)` for nanosecond-accurate timestamps. No `gettimeofday` (microsecond only) and no `std::chrono` wrapping (adds ~10 ns overhead per call on some kernels).

#### Additional Metrics vs. Python Client

The C++ client reports everything the Python client does, plus:

- **CPU time breakdown**: `getrusage(RUSAGE_SELF)` — user time vs. system (kernel) time. High kernel time indicates you are socket-syscall bound, not compute bound.
- **Inter-send gap histogram**: time between consecutive `sendto()` calls. If the mean gap is much larger than `packet_size / link_rate`, the sender thread is stalling somewhere.
- **Achieved vs. theoretical packet rate**: `packets_sent / wall_time` vs. `link_rate / avg_packet_size`

---

### 6.3 Overhauled `run_all.sh`

```bash
#!/bin/bash
# XIIRegexBuilder — Full Benchmark Suite
# Usage: ./benchmarks/run_all.sh [--with-uart] [--with-cpp-eth] [--verify]

# ── Stage 1: Build ─────────────────────────────────────────────────────────
mkdir -p build benchmarks/results
g++ -Wall -O2 -std=c++17 -o benchmarks/bench_cpp benchmarks/bench_cpp.cpp
g++ -O3 -std=c++17 -o benchmarks/bench_fpga_eth benchmarks/bench_fpga_eth.cpp

# ── Stage 2: Software Baselines ────────────────────────────────────────────
python3 benchmarks/bench_python.py "$REGEX_FILE" "$LARGE_TEST"
./benchmarks/bench_cpp "$REGEX_FILE" "$LARGE_TEST"

# ── Stage 3: Hardware Theoretical Analysis (unchanged formula) ─────────────
# ... existing clock-cycle math from current run_all.sh ...

# ── Stage 4: UART Baseline (optional) ──────────────────────────────────────
if [[ "$WITH_UART" == "1" ]]; then
    python3 benchmarks/bench_fpga_uart.py
fi

# ── Stage 5: Ethernet Benchmarks ───────────────────────────────────────────
VERIFY_FLAG=""
[[ "$WITH_VERIFY" == "1" ]] && VERIFY_FLAG="--verify"

python3 benchmarks/bench_fpga_eth.py $VERIFY_FLAG \
    --input "$LARGE_TEST" --regex "$REGEX_FILE"

if [[ "$WITH_CPP_ETH" == "1" ]]; then
    ./benchmarks/bench_fpga_eth $VERIFY_FLAG
fi

# ── Stage 6: Correctness Check ─────────────────────────────────────────────
if [[ "$WITH_VERIFY" == "1" ]]; then
    diff benchmarks/cpp_matches.txt benchmarks/eth_matches.txt
    # reports mismatches with line numbers
fi

# ── Stage 7: Summary Table + Output Files ──────────────────────────────────
TIMESTAMP=$(date +%Y%m%d_%H%M%S)
python3 benchmarks/summarise.py \
    --output-json "benchmarks/results/run_${TIMESTAMP}.json" \
    --output-csv  "benchmarks/results/run_${TIMESTAMP}.csv"
```

#### Summary Table (printed to terminal)

```
╔══════════════════════════════════╦═══════════════╦═════════════╦══════════════╗
║ Method                           ║ Strings/sec   ║ Throughput  ║ Latency p99  ║
╠══════════════════════════════════╬═══════════════╬═════════════╬══════════════╣
║ Python re                        ║   210,000     ║      —      ║      —       ║
║ C++ std::regex                   ║   890,000     ║      —      ║      —       ║
║ FPGA via UART (measured)         ║      ~300     ║  ~11 KB/s   ║   ~40 ms     ║
║ FPGA via Ethernet (Python)       ║    24,000     ║   ~8 MB/s   ║    1.2 ms    ║
║ FPGA via Ethernet (C++)          ║   200,000     ║  ~11 MB/s   ║    0.3 ms    ║
║ FPGA Theoretical                 ║ 10,000,000    ║   1 B/clk   ║   10 ns/B    ║
╠══════════════════════════════════╬═══════════════╬═════════════╬══════════════╣
║ Speedup  ETH C++ / UART          ║      ~650×    ║   ~1,000×   ║    ~130×     ║
╚══════════════════════════════════╩═══════════════╩═════════════╩══════════════╝
```

*(Numbers shown are illustrative targets based on hardware specs. Actual numbers will appear after running on hardware.)*

#### Output Artifacts

| File | Contents |
|---|---|
| `benchmarks/results/run_TIMESTAMP.json` | All raw timing data, packet stats, histogram buckets |
| `benchmarks/results/run_TIMESTAMP.csv` | One row per method: strings/sec, throughput, latency percentiles |
| `benchmarks/eth_matches.txt` | FPGA match results in same format as `cpp_matches.txt` |

The JSON and CSV files accumulate across runs, enabling you to track performance over time as you iterate on the design.

---

## 7. Phase 6 — `synth.tcl` Updates

Add after the existing `read_verilog` calls:

```tcl
# verilog-ethernet library
foreach f [glob lib/verilog-ethernet/rtl/*.v] {
    read_verilog $f
}

# New Ethernet control modules (generated by emitter)
read_verilog output-eg/packet_parser_fsm.v
read_verilog output-eg/result_assembler.v
```

Also add the 50 MHz clock constraint for the MMCM output:

```tcl
create_generated_clock -name clk_50 \
    -source [get_ports clk] \
    -divide_by 2 \
    [get_pins mmcm_inst/CLKOUT1]
```

---

## 8. Phase 7 — Recommended Implementation Order

This sequence is designed to avoid being blocked by synthesis turnaround time (~10 min per Vivado run). Validate each stage in simulation before touching hardware.

### Step 1 — Simulate `packet_parser_fsm.v` in Isolation

Write `tb_packet_parser.v`: a testbench that feeds raw UDP payload bytes (matching the packet format in §3.3) directly into `packet_parser_fsm` and checks:
- Correct `nfa_start` / `nfa_end_of_str` timing for each string
- Correct `nfa_char_in` byte sequence
- Correct `result_valid` pulse and `result_match` value

Use the existing `tb_top.v` as a reference for how to drive the NFA engine. This is where the most complex logic lives — get it correct in simulation before adding the Ethernet stack.

### Step 2 — Integrate `verilog-ethernet` in Simulation

Add `eth_mac_mii_fifo` + UDP stack to a new `tb_eth_top.v` testbench using a **behavioural MII model** (a Verilog model that mimics the LAN8720A's RMII interface, generating valid MII timing without requiring real hardware). Verify that a crafted Ethernet frame containing a valid UDP request payload arrives correctly at `packet_parser_fsm`'s AXI-Stream input.

Forencich's repo includes example testbenches in `tb/` — use these as a starting point.

### Step 3 — Write `bench_fpga_eth.py`

Implement the Python client and test its packet packing and response parsing against a **mock FPGA server** (`socket.socket` UDP echo server on localhost that returns synthetic correctly-formatted responses). This validates your host-side protocol logic with zero FPGA involvement and with fast iteration cycles.

### Step 4 — First Hardware Synthesis + Smoke Test

Synthesise `top_fpga.v` with the new Ethernet modules. Program the FPGA. Use `bench_fpga_eth.py` to:
1. Send 10 known strings
2. Check that `match_bits` responses are correct (use `--verify` against `python_matches.txt`)
3. Confirm no packet loss

This is your go/no-go gate before investing in the C++ client.

### Step 5 — Write `bench_fpga_eth.cpp`

With a working Python client as a reference, the C++ client can be validated immediately by running both simultaneously on the same dataset and comparing their `eth_matches.txt` outputs.

### Step 6 — Overhaul `run_all.sh` and `summarise.py`

At this point all the pieces exist. `run_all.sh` is purely orchestration and `summarise.py` is purely output formatting — both are straightforward once the underlying tools work.

---

## 9. Open Questions

Confirm these three points before writing any code:

### 9.1 Maximum String Length

Does any string in `large_test_strings.txt` exceed **255 characters**? The packet format uses a 1-byte `STR_LEN` field (max 255). If any strings are longer, change `STR_LEN` to 2 bytes now — changing the packet format midway through implementation means updating the FPGA, the Python client, and the C++ client simultaneously.

```bash
# Check max string length in your test data:
awk '{ print length }' inputs/large_test_strings.txt | sort -n | tail -1
```

### 9.2 Pin Assignment Verification

Before your first synthesis run, open the [Digilent Nexys A7 master XDC](https://github.com/Digilent/digilent-xdc) and cross-check every `eth_*` pin listed in §4.5 against the official file. The names in this document match the Nexys A7 documentation, but always verify — one wrong pin burns a full synthesis run.

### 9.3 Number of Regexes Growing Beyond 8

The `MATCH_BITS` field in the response packet is currently 1 byte, supporting up to 8 regexes (you currently have 6). If you ever plan to exceed 8 regexes, bump this to 2 bytes **now**, before implementing both the FPGA and both host clients. Changing it later is a protocol-breaking change.

---

## Appendix A — Expected Performance vs. UART

| Metric | UART (115,200 baud) | Ethernet (100 Mbps UDP) | Improvement |
|---|---|---|---|
| Raw link bandwidth | ~11.5 KB/s | ~12 MB/s | ~1,000× |
| Strings/sec (measured) | ~300 | ~24,000 (Python) / ~200,000 (C++) | 80× – 650× |
| Per-string latency p99 | ~40 ms | ~1.2 ms (Python) / ~0.3 ms (C++) | 33× – 130× |
| FPGA compute time (theoretical) | same | same | — |
| FPGA compute as % of total latency | <1% (UART dominates) | ~40–70% (compute now visible) | — |

The most important insight is the last row: with UART, the NFA compute time is completely buried inside the communication overhead and invisible to benchmarking. With Ethernet, the `FPGA_CYCLE_STAMP` field lets you see compute time separately, and the NFA's actual performance becomes the measurable quantity rather than the bottleneck.

---

## Appendix B — File Tree After Implementation

```
lib/
  verilog-ethernet/          ← Forencich library (checked in)
    rtl/
      eth_mac_mii_fifo.v
      udp.v
      arp.v
      axis_fifo.v
      ... (all required modules)

output-eg/
  top_fpga.v                 ← regenerated with Ethernet ports
  packet_parser_fsm.v        ← new (generated by emitter)
  result_assembler.v         ← new (generated by emitter)
  constraints.xdc            ← updated with ETH pins
  top.v                      ← unchanged
  nfa_0.v ... nfa_5.v        ← unchanged
  uart_tx.v                  ← unchanged (kept for programming)

benchmarks/
  bench_python.py            ← unchanged
  bench_cpp.cpp              ← unchanged
  bench_fpga_uart.py         ← unchanged (demoted to baseline)
  bench_fpga_eth.py          ← new Python client
  bench_fpga_eth.cpp         ← new C++ client
  summarise.py               ← new summary table + JSON/CSV writer
  run_all.sh                 ← overhauled
  results/
    run_TIMESTAMP.json       ← accumulated benchmark history
    run_TIMESTAMP.csv

processor/
  uart.v                     ← unchanged
  src/prog_fpga.py           ← unchanged
```