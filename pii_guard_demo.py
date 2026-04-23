import serial
import time
import sys

# Update the port depending on your OS/Setup (e.g., COM3 on Windows)
PORT = '/dev/ttyUSB1' 
BAUD = 921600 # High-speed production baud rate

try:
    ser = serial.Serial(PORT, 115200, timeout=1, rtscts=False)
except Exception as e:
    print(f"Failed to open {PORT}: {e}")
    print("Mocking stream output for demonstration purposes.")
    ser = None

def stream_text(text):
    print(f"Original Stream: {text}\n")
    print("Redacted Stream: ", end="", flush=True)

    if ser:
        ser.reset_input_buffer()

    for char in text:
        if ser:
            ser.write(char.encode())
            # Use errors='replace' to handle potential noise/uninitialized bytes
            scrubbed = ser.read(1).decode('utf-8', errors='replace')
            print(scrubbed, end="", flush=True)
            time.sleep(0.01) 
        else:
            # Mock behavior if FPGA is unplugged
            print(char, end="", flush=True)
            time.sleep(0.01)

    print("\n\nDone.")

if __name__ == "__main__":
    demo_text = "Hello! My CC is 4111222233334444 and my SSN is 123-45-6789. Contact user@example.com."
    # Add 128 spaces to flush the FPGA's 128-char delay buffer
    stream_text(demo_text + " " * 128)

