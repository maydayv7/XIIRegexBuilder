# Real-Time PII Guard Implementation Specification

**Date:** 2026-04-13
**Topic:** Hardware-Accelerated PII Redaction Demo
**Status:** Approved

---

## 1. Project Overview
The objective is to transform the existing "Full-Match" hardware regex engine into a "Streaming Scrubber" (PII Guard). This system will detect sensitive information (Credit Cards, SSNs, Emails) in a real-time character stream and redact them with 'X' characters before they are sent back to the host.

## 2. Architecture: "Blind Window" Scrubber
The system uses a parallel architecture where incoming data feeds both the NFA Matchers and a **32-Byte Delay Line** simultaneously.

### 2.1 Hardware Components (FPGA)
*   **Streaming NFA Core:**
    *   **Always-On Start:** The NFA's start state (state 0) is hard-wired to `1'b1`. This enables substring matching by starting a new match attempt on every clock cycle.
    *   **Immediate Pulse:** The `match` signal pulses for one cycle immediately upon reaching an accept state, without waiting for an `end_of_str` signal.
*   **Redaction Buffer:**
    *   **32-Byte Shift Register:** Delays the incoming character stream by 32 cycles. This gives the NFA time to process the "tail" of a sensitive pattern before the "head" of that pattern leaves the buffer.
    *   **Multiplexer:** Swaps the outgoing character with ASCII 'X' (`8'h58`) when the `redact_counter` is active.
*   **Redaction Controller:**
    *   **Per-Regex Windows:** Each regex index is mapped to a heuristic redaction length (e.g., CC=16, SSN=11, Email=32).
    *   **Down-Counter:** Triggered by a `match` pulse from any NFA; it counts down for the duration of that regex's window to gate the 'X' replacement.
*   **Communication Interface:**
    *   **Full-Duplex UART:** Existing `uart_rx` for input; new `uart_tx` for streaming back redacted output at 115200 baud.

### 2.2 Software Components (Python)
*   **`pii_guard_demo.py`:**
    *   Streams "leaky" text to the FPGA over UART byte-by-byte.
    *   Reads the returned character stream and displays it in the terminal.
    *   Simulates "real-time" flow with a small delay between bytes for visual effect.

## 3. Implementation Details

### 3.1 C++ Emitter Changes
*   **`emitter.cpp`**:
    *   Modify `emitNFAModule` to force `next_state[0] = 1'b1`.
    *   Remove dependency on `end_of_str` for the `match` pulse.
    *   Add a `get_redaction_length(index)` helper to the generated Verilog to support the heuristic lengths.

### 3.2 Verilog Top Changes
*   **`top_fpga.v`**:
    *   Instantiate the 32-byte delay line.
    *   Instantiate a `uart_tx` module.
    *   Implement the `redact_counter` logic and the output Mux.

## 4. Success Criteria
1.  **Real-Time Redaction:** When text containing a credit card (e.g., `4111222233334444`) is streamed, the output shows `XXXXXXXXXXXXXXXX`.
2.  **Variable Length Support:** Emails of various lengths are correctly redacted (up to a 32-byte window).
3.  **Low Latency:** The system processes data at line-rate (modulo the 32-cycle buffer delay).
4.  **Hardware Efficiency:** One-hot state encoding is preserved; LUT usage remains low for < 20 regexes.

## 5. Patterns for Demo
*   **Credit Card:** `[0-9]{16}` (Length: 16)
*   **SSN:** `[0-9]{3}-[0-9]{2}-[0-9]{4}` (Length: 11)
*   **Email:** `[a-zA-Z0-9.]+@[a-zA-Z0-9.]+` (Length: 32 heuristic)
