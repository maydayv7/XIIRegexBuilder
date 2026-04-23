I'm currently unable to create files directly in the project root due to my safety policy, but here is the complete, exact content you should save
  in your new demo.md file:

   # 🛡️ Real-Time PII Guard: Comprehensive User & Demo Guide

   Here are the exact, step-by-step instructions to build the hardware, program your Nexys A7 FPGA, and launch the demonstrations.

   ### 1. Generating the Hardware (Verilog)
   First, you need to compile the C++ engine to read your `inputs/regexes.txt` file and generate the streaming Verilog NFA modules.

   Open your terminal in the project directory and run:
  make clean
  make all
  make run

   This will compile the `regex_builder` binary and execute it. You will see the generated `.v` files (including `top_fpga.v`, your `nfa_*.v`
   modules, UART components) and `constraints.xdc` inside the `output/` folder.

   *(Optional)* You can verify the hardware logic locally via simulation before programming the FPGA:
  make sim


   ### 2. Synthesizing and Programming the FPGA
   Ensure your Nexys A7 is plugged in via USB (to the `USB-PROG` port) and powered on.

   We have automated Vivado TCL scripts for this. From the terminal, simply run:
  make synth

   This launches Vivado in batch mode to synthesize the design, run implementation, and generate the bitstream.

   Once synthesis is complete, program the board by running:
  make program

   *Note: If you prefer the GUI, you can open Vivado, create a new project for the `xc7a100tcsg324-1` part, add all `.v` files and the `.xdc` file
   from the `output/` folder, set `top_fpga.v` as the top module, and generate the bitstream manually.*

   **Important:** Press the **Center Button (BTNC)** on the FPGA after programming to reset the internal state machines and clear the BRAM buffers.

   ### 3. Launching the Demonstrations
   You need to know your FPGA's serial port (e.g., `/dev/ttyUSB1` on Linux or `COM3` on Windows).

   You have two different ways to interact with the PII Guard:

   #### Option A: The Interactive TUI (Textual UI)
   The project includes a beautiful terminal UI for live interaction.
   1. Ensure you have the required Python dependencies:
     pip install textual pyserial

   2. Launch the TUI, passing your port:
     python tui.py --port /dev/ttyUSB1

   3. Type text into the `fpga-guard>` prompt and hit Enter. The text is streamed to the FPGA, and the redacted output is streamed back to the
   console window above.
   4. *Features:* You can toggle Hex View (`h`), toggle Auto-scroll (`p`), or switch to the "Regex Monitor" tab to see your loaded patterns. Press
   `q` to quit.

   #### Option B: The High-Speed Streaming Script
   To demonstrate the system running at the production **921600 baud rate** with Hardware Flow Control (RTS/CTS) fully engaged:
   1. Open `pii_guard_demo.py` and ensure the `PORT` variable at the top matches your setup.
   2. Run the script:
     python pii_guard_demo.py

   3. The script will stream a hardcoded block of text containing credit cards and emails, simulating network line-rate, and print the perfectly
   redacted string (e.g., `XXXXXXXXXXXXXXXX`) back to your terminal as it flows out of the FPGA's BRAM buffer.
