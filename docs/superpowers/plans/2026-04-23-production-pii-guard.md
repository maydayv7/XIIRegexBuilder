# Production-Ready PII Guard Refactor Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Refactor the PII Guard into a deterministic, high-performance stream scrubber with exact boundary redaction and BRAM-backed buffering.

**Architecture:**
1. **NFA Active Tracking:** NFAs output an `active` signal when they are in any non-idle state.
2. **History-Commit Buffer:** A 1024-bit shift register tracks "potential match" history. On a `match` pulse, this history is committed to a redaction mask.
3. **BRAM Circular Buffer:** Incoming data is stored in a 1024-byte BRAM window, providing the necessary lookahead for the NFA to finish a match before the data is transmitted.
4. **Hardware Flow Control:** UART RTS/CTS ensures zero data loss even at high baud rates.

**Tech Stack:** C++, Verilog, Icarus Verilog (for simulation)

---

### Task 1: Simulation Infrastructure

**Files:**
- Modify: `Makefile`
- Modify: `src/emitter.cpp`

- [ ] **Step 1: Add `sim` target to Makefile**
```makefile
sim:
	iverilog -o sim_out output/*.v
	vvp sim_out
```

- [ ] **Step 2: Update `emitTestbench` to include success/fail messages**
In `src/emitter.cpp`, ensure `emitTestbench` prints a specific "PASSED" or "FAILED" string that `grep` can catch.

- [ ] **Step 3: Commit**
```bash
git add Makefile src/emitter.cpp
git commit -m "infra: add verilog simulation harness"
```

### Task 2: NFA "Active" Tracking

**Files:**
- Modify: `src/emitter.cpp`

- [ ] **Step 1: Add `active` port to `emitNFAModule`**
```verilog
output wire active
...
assign active = |state_reg[numStates-1:1];
```

- [ ] **Step 2: Update `emitTopModule` to route `active_bus`**

- [ ] **Step 3: Commit**
```bash
git add src/emitter.cpp
git commit -m "feat: implement NFA active state tracking"
```

### Task 3: BRAM and Shift-History Implementation

**Files:**
- Modify: `src/emitter.cpp` (emitTopFPGA)

- [ ] **Step 1: Implement 1024-byte BRAM Window**
Replace register-based delay line with BRAM.

- [ ] **Step 2: Implement Shift-History and Commit Logic**
```verilog
reg [1023:0] active_history;
reg [1023:0] commit_history;
always @(posedge clk) begin
    active_history <= {active_history[1022:0], any_active};
    if (any_match) commit_history <= commit_history | active_history;
    else commit_history <= {commit_history[1022:0], 1'b0};
end
```

- [ ] **Step 3: Commit**
```bash
git add src/emitter.cpp
git commit -m "feat: implement shift-history exact redaction logic"
```

### Task 4: Flow Control and High-Speed UART

**Files:**
- Modify: `src/emitter.cpp`

- [ ] **Step 1: Add RTS/CTS to UART modules**
- [ ] **Step 2: Update constraints for RTS/CTS pins**
- [ ] **Step 3: Commit**
```bash
git add src/emitter.cpp
git commit -m "feat: add hardware flow control (RTS/CTS) to UART"
```

### Task 5: Final Validation

- [ ] **Step 1: Run Full Pipeline**
`make && ./xiiregexbuilder inputs/regexes.txt inputs/test_strings.txt output && make sim`
- [ ] **Step 2: Commit**
```bash
git commit -m "docs: finalize production-ready PII guard implementation"
```
