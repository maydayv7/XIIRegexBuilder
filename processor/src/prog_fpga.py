import serial
import time
import sys
import serial.tools.list_ports


def find_fpga_port():
    ports = serial.tools.list_ports.comports()
    for port in ports:
        if "COM14" in port.device:
            return port.device
    ports.sort(
        key=lambda x: int(x.device.replace("COM", "") if "COM" in x.device else 0),
        reverse=True,
    )
    for port in ports:
        desc = port.description.lower()
        if any(
            x in desc
            for x in ["usb", "uart", "nexys", "digilent", "cp210x", "ch340", "serial"]
        ):
            return port.device
    return None


def main():
    hex_file = "imem.hex"
    if len(sys.argv) > 1:
        hex_file = sys.argv[1]

    port = find_fpga_port()
    if not port:
        print("Error: FPGA Port not found.")
        sys.exit(1)

    print(f"Connecting to {port}...")
    try:
        ser = serial.Serial(port, 115200, timeout=1)
    except Exception as e:
        print(f"Error: Could not open serial port {port}: {e}")
        sys.exit(1)

    print(f"Programming {hex_file} to FPGA imem...")

    with open(hex_file, "r") as f:
        lines = f.readlines()

    for addr, line in enumerate(lines):
        line = line.strip()
        if not line:
            continue

        data = int(line, 16)

        # Protocol: 0x02 | Addr | Data[31:24] | Data[23:16] | Data[15:8] | Data[7:0]
        packet = bytearray(
            [
                0x02,
                addr & 0xFF,
                (data >> 24) & 0xFF,
                (data >> 16) & 0xFF,
                (data >> 8) & 0xFF,
                data & 0xFF,
            ]
        )

        ser.write(packet)
        time.sleep(0.001)  # Small delay for FPGA to process

    ser.close()
    print("Programming Complete!")


if __name__ == "__main__":
    main()
