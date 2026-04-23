import serial
import time
import sys

# Update the port depending on your OS/Setup (e.g., COM3 on Windows)
PORT = '/dev/ttyUSB1' 
BAUD = 921600 # High-speed production baud rate

try:
    ser = serial.Serial(PORT, 115200, timeout=1, rtscts=False)
except Exception as e:
    print(f"ERROR: Could not open {PORT}: {e}")
    if "Permission denied" in str(e):
        print(f"HINT: Try running with sudo: 'sudo ./venv/bin/python pii_guard_demo.py'")
    print("\n--- FALLING BACK TO MOCK MODE (Simulation) ---")
    ser = None

def mock_redact(text):
    import re
    # Simplified simulation of the FPGA patterns
    patterns = [
        r'[a-zA-Z0-9.]+@[a-zA-Z0-9.]+\.[a-zA-Z]+', # Email
        r'\d{10}',                                 # Phone
        r'\d{3}-\d{2}-\d{4}',                      # SSN
        r'\d{16}'                                  # CC
    ]
    redacted = text
    for p in patterns:
        for match in re.finditer(p, redacted):
            start, end = match.span()
            redacted = redacted[:start] + 'X' * (end-start) + redacted[end:]
    return redacted

def stream_text(text):
    print(f"Original Stream: {text}\n")
    
    if ser:
        print("Redacted Stream: ", end="", flush=True)
        ser.reset_input_buffer()
        
        has_started = False
        for char in text:
            ser.write(char.encode())
            scrubbed = ser.read(1).decode('utf-8', errors='replace')
            
            # Skip leading spaces from the delay buffer initialization
            if not has_started and scrubbed != ' ':
                has_started = True
                
            if has_started:
                print(scrubbed, end="", flush=True)
                
            time.sleep(0.002) # Faster streaming for large demo files    else:
        print("(MOCK) Redacted Stream: ", end="", flush=True)
        simulated = mock_redact(text)
        for char in simulated:
            print(char, end="", flush=True)
            time.sleep(0.01)
    
    print("\n\nDone.")

if __name__ == "__main__":
    with open("demo_input.txt", "r") as f:
        demo_text = f.read()
    
    # Add 128 spaces to flush the FPGA's 128-char delay buffer
    stream_text(demo_text + " " * 128)

