import serial
import time
import os
import sys
import serial.tools.list_ports

def find_fpga_port():
    ports = serial.tools.list_ports.comports()
    # Try common names or the user's COM14
    for port in ports:
        if "COM14" in port.device: return port.device
    for port in ports:
        desc = port.description.lower()
        if any(x in desc for x in ["usb", "uart", "nexys", "digilent", "cp210x", "ch340", "serial"]):
            return port.device
    return None

def run_fpga_benchmark(test_file):
    port = find_fpga_port()
    if not port:
        print("!!! FPGA NOT FOUND !!! Continuing with simulation timing only.")
        return False

    try:
        ser = serial.Serial(port, 115200, timeout=0.1)
    except:
        print(f"!!! COULD NOT OPEN {port} !!!")
        return False

    with open(test_file, 'r') as f:
        test_strings = [line.rstrip('\r\n') for line in f if line.strip() and not line.strip().startswith('#')]

    print(f"Connected to {port}. Sending {len(test_strings)} strings...")
    
    start_wall = time.perf_counter()
    
    for s in test_strings:
        # Protocol: START(0x01) -> Chars -> END(\r)
        ser.write(b"\x01")
        ser.write(s.encode("ascii"))
        ser.write(b"\r")
        
        # Wait for 2-byte response
        # In a batch benchmark, we'd ideally not wait for every single response to maximize throughput,
        # but for simplicity and correctness verification, we read it.
        resp = ser.read(2)
        
    end_wall = time.perf_counter()
    
    total_time = end_wall - start_wall
    print(f"Total Time (UART + FPGA): {total_time:.4f} seconds")
    print(f"Throughput: {len(test_strings)/total_time:.2f} strings/sec")
    
    ser.close()
    return True

if __name__ == "__main__":
    run_fpga_benchmark("inputs/large_test_strings.txt")
