import os
import sys
import time
import serial
import serial.tools.list_ports
from rich.console import Console
from rich.table import Table
from rich.panel import Panel
from rich.layout import Layout
from rich.syntax import Syntax
from rich.markup import escape

console = Console()

# Global State
last_match_binary = "0" * 16
raw_hex = "0000"
ser = None
status_msg = "Ready"


def get_regex_list():
    input_file = sys.argv[1] if len(sys.argv) > 1 else "processor/regex.txt"
    if os.path.exists(input_file):
        with open(input_file, "r") as f:
            return [
                line.strip() for line in f if line.strip() and not line.startswith("#")
            ]
    return [
        "ab*c",
        "ab*",
        "a(b|c)",
        "b*a",
        "c+",
        "(a|b|c)a",
        "ab?c",
        "(abc)*",
        "a.c",
        "b(ab)c",
        "(a|c)*",
        "cc*b",
    ]


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


def get_rasm():
    rasm_file = sys.argv[2] if len(sys.argv) > 2 else "processor/build/regexes.rasm"
    if os.path.exists(rasm_file):
        with open(rasm_file, "r") as f:
            return f.read()
    return ""


def get_regex_text():
    input_file = sys.argv[1] if len(sys.argv) > 1 else "processor/regex.txt"
    if os.path.exists(input_file):
        with open(input_file, "r") as f:
            return f.read()
    return ""


def update_display(layout):
    regex_list = get_regex_list()
    port_info = (
        f"[bold green]CONNECTED: {ser.port}[/bold green]"
        if ser
        else "[bold red]DISCONNECTED[/bold red]"
    )
    layout["header"].update(
        Panel(
            f"[bold cyan]XIIRegex Dashboard[/bold cyan] | {port_info} | {status_msg}",
            border_style="cyan",
        )
    )

    match_view = Table(
        expand=True, title=f"Hardware Match Results (Bits: {last_match_binary})"
    )
    match_view.add_column("ID", width=4, style="dim")
    match_view.add_column("Pattern", style="cyan")
    match_view.add_column("Status", justify="right")

    bits = last_match_binary[::-1]
    for i, name in enumerate(regex_list):
        is_match = bits[i] == "1" if i < len(bits) else False
        status = "[bold green]MATCH[/bold green]" if is_match else "[dim]no match[/dim]"
        match_view.add_row(str(i), escape(name), status)

    layout["matches"].update(Panel(match_view, border_style="blue"))
    layout["code"].update(
        Panel(
            Syntax(get_regex_text(), "python", theme="monokai", line_numbers=True),
            title="regex.txt (Source Patterns)",
            border_style="green",
        )
    )
    layout["footer"].update(
        Panel(
            "Type a string and press Enter. 'exit' to quit. Use 'make update_regex' to flash changes.",
            border_style="dim",
        )
    )


def main():
    global ser, status_msg, last_match_binary, raw_hex
    port = find_fpga_port()
    if port:
        try:
            # Added dsrdtr and rtscts settings
            ser = serial.Serial(port, 115200, timeout=0.1, dsrdtr=False, rtscts=False)
            ser.dtr = True  # Force DTR High
            ser.rts = True  # Force RTS High
            status_msg = f"Connected to {port}"
        except Exception as e:
            status_msg = f"Connection Failed: {e}"
            ser = None
    else:
        status_msg = "FPGA Not Found. Please check COM14."

    layout = Layout()
    layout.split_column(
        Layout(name="header", size=3),
        Layout(name="main", ratio=1),
        Layout(name="footer", size=3),
    )
    layout["main"].split_row(
        Layout(name="matches", ratio=1), Layout(name="code", ratio=1)
    )

    while True:
        os.system("cls" if os.name == "nt" else "clear")
        update_display(layout)
        console.print(layout)

        try:
            test_str = console.input("\n[bold yellow]Enter test string: [/bold yellow]")
            if test_str.lower() == "exit":
                break

            if ser and ser.is_open:
                ser.reset_input_buffer()

                # Protocol: START, CHARS, END
                ser.write(b"\x01")
                time.sleep(0.01)

                for char in test_str:
                    ser.write(char.encode("ascii"))
                    time.sleep(0.005)

                ser.write(b"\r")
                ser.flush()  # Ensure everything is sent

                # Wait for response
                start_time = time.time()
                response = b""
                while len(response) < 2 and (time.time() - start_time) < 1.0:
                    if ser.in_waiting > 0:
                        response += ser.read(ser.in_waiting)
                    time.sleep(0.01)

                if len(response) >= 2:
                    res_bytes = response[-2:]
                    val = (res_bytes[0] << 8) | res_bytes[1]
                    raw_hex = f"{res_bytes[0]:02X}{res_bytes[1]:02X}"
                    last_match_binary = bin(val)[2:].zfill(16)
                    status_msg = (
                        f"[bold green]Match Received! (Raw: {raw_hex})[/bold green]"
                    )
                else:
                    status_msg = "[bold red]Error: No response (Check if LED13 flickered)[/bold red]"

            else:
                status_msg = "Error: Serial port not available"
        except KeyboardInterrupt:
            break


if __name__ == "__main__":
    main()
