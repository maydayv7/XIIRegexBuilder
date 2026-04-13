import threading
import serial
import sys
import argparse
import time
from textual.app import App, ComposeResult
from textual.widgets import Header, Footer, Input, Static
from textual.containers import Vertical
from textual import on

class PII_TUI(App):
    """A Textual TUI for interacting with the FPGA PII Guard."""
    
    CSS = """
    #output_container {
        background: #000000;
        color: #00FF00;
        border: solid #00AA00;
        height: 1fr;
        overflow-y: scroll;
    }
    
    #output_text {
        width: 100%;
        padding: 1;
    }
    
    Input {
        dock: bottom;
        border: solid #00AA00;
    }
    """

    BINDINGS = [
        ("q", "quit", "Quit"),
        ("c", "clear", "Clear Output"),
    ]

    def __init__(self, port, baudrate=115200):
        super().__init__()
        self.port = port
        self.baudrate = baudrate
        self.ser = None
        self.reader_thread = None
        self.running = True
        self.log_content = ""

    def compose(self) -> ComposeResult:
        yield Header(show_clock=True)
        with Vertical(id="output_container"):
            yield Static("", id="output_text", markup=True)
        yield Input(placeholder="Type text to stream to FPGA...", id="input_field")
        yield Footer()

    def on_mount(self) -> None:
        """Called when the app is mounted."""
        try:
            self.ser = serial.Serial(self.port, self.baudrate, timeout=0.1)
            self.append_to_log(f"Connected to {self.port} at {self.baudrate} baud.\n", is_system=True)
            
            # Start reader thread
            self.reader_thread = threading.Thread(target=self.read_from_serial, daemon=True)
            self.reader_thread.start()
        except Exception as e:
            self.append_to_log(f"Error connecting to {self.port}: {e}\n", is_system=True)
            self.append_to_log("Running in Mock Mode (No FPGA detected)\n", is_system=True)
            self.ser = None

    def read_from_serial(self):
        """Continuously read from serial port."""
        while self.running:
            if self.ser and self.ser.is_open:
                try:
                    if self.ser.in_waiting > 0:
                        char = self.ser.read(1).decode(errors='ignore')
                        if char:
                            # Update UI from thread
                            self.call_from_thread(self.append_to_log, char)
                except Exception as e:
                    self.call_from_thread(self.append_to_log, f"\nSerial Error: {e}\n", is_system=True)
                    break
            else:
                time.sleep(0.1)

    def append_to_log(self, text: str, is_system: bool = False):
        if is_system:
            self.log_content += f"[bold yellow]{text}[/bold yellow]"
        else:
            for char in text:
                if char == 'X':
                    self.log_content += "[bold red]X[/bold red]"
                elif char == '\n':
                    self.log_content += "\n"
                else:
                    # Escape markup characters to avoid unintended formatting
                    escaped = char.replace("[", "[[").replace("]", "]]")
                    self.log_content += escaped
        
        # Cap log size to avoid performance issues (last 15000 chars)
        if len(self.log_content) > 15000:
            self.log_content = self.log_content[-10000:]
            
        static = self.query_one("#output_text")
        static.update(self.log_content)
        self.query_one("#output_container").scroll_end(animate=False)

    @on(Input.Submitted)
    def on_input_submitted(self, event: Input.Submitted) -> None:
        """Send the whole string to the FPGA when Enter is pressed."""
        text = event.value
        if text:
            # Append a newline to trigger the FPGA's session reset logic
            full_text = text + "\r"
            self.append_to_log(f"\nSending: {text}\n", is_system=True)
            if self.ser:
                for char in full_text:
                    self.ser.write(char.encode())
                    # Small sleep to ensure we don't overflow the FPGA's FIFO
                    time.sleep(0.001) 
            else:
                # Mock echo for demonstration
                for c in full_text:
                    self.append_to_log(c)
            self.query_one("#input_field").value = ""

    def action_clear(self) -> None:
        self.log_content = ""
        self.query_one("#output_text").update("")

    def on_unmount(self) -> None:
        self.running = False
        if self.ser:
            self.ser.close()

if __name__ == "__main__":
    parser = argparse.ArgumentParser(description="PII Guard TUI")
    parser.add_argument("--port", default="/dev/ttyUSB1", help="Serial port (e.g. /dev/ttyS3 for COM3)")
    args = parser.parse_args()
    
    app = PII_TUI(port=args.port)
    app.run()
