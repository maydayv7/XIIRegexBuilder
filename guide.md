# 🛡️ Real-Time PII Guard: Comprehensive User & Demo Guide

This guide provides the complete set of micro-instructions to build, program, and demonstrate the **Hardware-Accelerated PII Scrubber** on an FPGA.

---

## 📋 1. Prerequisites & Setup

### Hardware
*   **FPGA Board:** Xilinx Nexys A7 100T (Artix-7).
*   **Cable:** Micro-USB to USB-A (connected to the `USB-PROG` port).
*   **Switch:** Power switch in the `ON` position.

### Software
*   **Vivado Design Suite:** (2019.1 or newer recommended).
*   **Terminal/Shell:** Bash (Linux) or Git Bash (Windows).
*   **Python 3.x:** With `pyserial` (`pip install pyserial`).
*   **PuTTY:** Serial terminal client.

---

## 🛠️ 2. Build & Hardware Generation

Before opening Vivado, we must use the C++ compiler to generate the specific Verilog modules for our PII patterns.

1.  **Open your terminal** in the project directory.
2.  **Clean and Compile the Engine:**
    ```bash
    make clean
    make all
    ```
3.  **Generate the Streaming Verilog Modules:**
    This command reads `inputs/regexes.txt` and outputs the Verilog to the `output/` folder.
    ```bash
    make run
    ```
4.  **Verify Generated Artifacts:**
    Check the `output/` directory for these essential files:
    *   `top_fpga.v` (Top-level controller)
    *   `nfa_*.v` (The generated NFA matchers)
    *   `uart_rx.v` & `uart_tx.v` (The UART communication modules)
    *   `constraints.xdc` (The pin mappings for the Nexys A7)

---

## 💻 3. Vivado Project & Programming

1.  **Launch Vivado** and select **Create Project**.
2.  **Name:** `PII_Guard_Demo`. **Part:** `xc7a100tcsg324-1`.
3.  **Add Design Sources:** 
    *   Click **Add Sources** → **Add or Create Design Sources**.
    *   Add **ALL** `.v` files from the `output/` folder.
    *   **Right-click** `top_fpga.v` in the Sources pane and select **Set as Top**.
4.  **Add Constraints:**
    *   Click **Add Sources** → **Add or Create Constraints**.
    *   Add `output/constraints.xdc`.
5.  **Generate Bitstream:**
    *   Click **Run Synthesis**.
    *   Click **Run Implementation**.
    *   Click **Generate Bitstream**.
6.  **Program the Hardware:**
    *   Open **Hardware Manager** → **Open Target** → **Auto Connect**.
    *   Click **Program Device** and select the `.bit` file.

---

## 📺 4. The Live Demo (Using PuTTY)

This mode allows for a dramatic "live-typing" demonstration where characters are redacted as you type.

1.  **Open Device Manager** on your PC to find the **COM Port** (e.g., `COM3`).
2.  **Launch PuTTY** and configure exactly as follows:
    *   **Connection Type:** Serial
    *   **Serial Line:** `COMx` (your port)
    *   **Speed:** `115200`
3.  **Configure PuTTY Terminal Behavior:**
    *   Go to **Category: Terminal** in the left sidebar.
    *   Check: **Implicit LF on every CR**
    *   Check: **Implicit CR on every LF**
    *   **Local Echo:** Set to **Force Off** (Characters should only appear if the FPGA sends them back).
    *   **Local Line Editing:** Set to **Force Off**.
4.  **Open the Connection.**
5.  **Press the Reset Button (BTNC)** on the FPGA to ensure the engine is ready.

---

## 🎓 5. How to Present to Your Professor

To get the best grade, follow this narrative flow during your presentation:

### Phase 1: The "Why" (Context)
*   **Explain the Problem:** "In financial and medical data streams, PII (Personally Identifiable Information) must be redacted at line-rate. Software-based regex is often too slow for multi-gigabit streams."
*   **The Advantage:** "My system uses an FPGA to run multiple Non-deterministic Finite Automata (NFAs) in parallel. It checks every character against all patterns simultaneously in a single clock cycle."

### Phase 2: The Architecture (The "Cool" Part)
*   **The Streaming NFA:** Explain that you modified the NFA core to be "Always-On" for continuous substring matching.
*   **The Blind Window Buffer:** Explain the **32-byte delay line**. "The data flows into a buffer. If the NFA detects a match at the end of a string, the hardware 'reaches back' and redacts the window inside the buffer before it ever leaves the chip."

### Phase 3: The Live Demo (Action)
1.  **Show "Safe" Data:** Type `Hello, my name is Rahul.`
    *   *Observation:* Characters appear instantly, no redaction.
2.  **The "Leak" (Credit Card):** Type `My CC is 4111222233334444`.
    *   *Result:* As you type the final `4`, the entire sequence turns into `XXXXXXXXXXXXXXXX`.
3.  **The "Leak" (Email/SSN):** Type `email me at user@example.com`.
    *   *Result:* The email is redacted. Note the LEDs on the board—different LEDs light up for different pattern types.
4.  **Stress Test:** Paste a large block of text.
    *   *Result:* Show that redaction happens without any throughput drop.

### Phase 4: Conclusion (Technical Depth)
*   Mention that the system handles **overlapping matches** (e.g., an email inside a credit card string) by taking the maximum redaction length.
*   Highlight that the NFA is generated using **Glushkov's Construction**, ensuring a clean, ε-free state machine.

---

## 🛠️ Micro-Troubleshooting
*   **PuTTY is blank?** Press **BTNC** on the board.
*   **Redaction is off by one?** The 32-byte window is a heuristic; ensure your regexes in `regexes.txt` aren't longer than 31 characters.
*   **UART Busy?** Ensure no other program (like the Python script or Vivado) is holding the COM port open.
