import serial
import sys
import argparse
import asyncio
from textual.app import App, ComposeResult
from textual.widgets import Header, Footer, Input, RichLog
from textual.containers import Vertical
from textual import on, work

class PII_TUI(App):
    """A Textual TUI for interacting with the FPGA PII Guard."""
    
    CSS = """
    #output_log {
        background: #000000;
        color: #00FF00;
        border: solid #00AA00;
        height: 1fr;
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

    def compose(self) -> ComposeResult:
        yield Header(show_clock=True)
        with Vertical(id="output_container"):
            yield RichLog(id="output_log", markup=True, wrap=True)
        yield Input(placeholder="Type text to stream to FPGA...", id="input_field")
        yield Footer()

    def on_mount(self) -> None:
        """Called when the app is mounted."""
        try:
            self.ser = serial.Serial(self.port, self.baudrate, timeout=0.1)
            self.append_to_log(f"Connected to {self.port} at {self.baudrate} baud.", is_system=True)
            # Start the non-blocking reader worker
            self.read_from_serial()
        except Exception as e:
            self.append_to_log(f"Error connecting to {self.port}: {e}", is_system=True)
            self.append_to_log("Running in Mock Mode (No FPGA detected)", is_system=True)
            self.ser = None

    @work(exclusive=True, thread=True)
    def read_from_serial(self):
        """Continuously read from serial port in a background thread."""
        while True:
            if self.ser and self.ser.is_open:
                try:
                    if self.ser.in_waiting > 0:
                        char = self.ser.read(1).decode(errors='ignore')
                        if char:
                            # Schedule the UI update on the main thread
                            self.call_next(self.append_to_log, char)
                except Exception as e:
                    self.call_next(self.append_to_log, f"\nSerial Error: {e}\n", is_system=True)
                    break
            else:
                # Small sleep in thread to prevent CPU spinning
                import time
                time.sleep(0.01)

    def append_to_log(self, text: str, is_system: bool = False):
        log = self.query_one("#output_log", RichLog)
        if is_system:
            log.write(f"[bold yellow]{text}[/bold yellow]")
        else:
            # Optimized 'X' highlighting
            if text == 'X':
                log.write("[bold red]X[/bold red]", scroll_end=True)
            elif text == '\n':
                log.write("", scroll_end=True) # RichLog handles newlines via write calls
            else:
                # Escape markup characters
                escaped = text.replace("[", "[[").replace("]", "]]")
                log.write(escaped, scroll_end=False)
        
        # Always scroll to end for new system messages or if requested
        if is_system:
            log.scroll_end(animate=False)

    @on(Input.Submitted)
    def on_input_submitted(self, event: Input.Submitted) -> None:
        """Send the whole string to the FPGA when Enter is pressed."""
        text = event.value
        if text:
            # Reset input immediately for better UX
            self.query_one("#input_field").value = ""
            # Start worker to send data without blocking the UI
            self.send_to_fpga(text)

    @work(exclusive=True, thread=True)
    def send_to_fpga(self, text: str):
        """Worker to send characters to FPGA with small delays."""
        # Append a newline to trigger the FPGA's session reset logic
        full_text = text + "\r"
        self.call_next(self.append_to_log, f"Sending: {text}", is_system=True)
        
        import time
        if self.ser:
            for char in full_text:
                self.ser.write(char.encode())
                time.sleep(0.001) 
        else:
            # Mock behavior: echo back with delays
            for char in full_text:
                time.sleep(0.02)
                self.call_next(self.append_to_log, char)

    def action_clear(self) -> None:
        self.query_one("#output_log", RichLog).clear()

    def on_unmount(self) -> None:
        if self.ser:
            self.ser.close()

if __name__ == "__main__":
    parser = argparse.ArgumentParser(description="PII Guard TUI")
    parser.add_argument("--port", default="/dev/ttyUSB1", help="Serial port (e.g. /dev/ttyS3 for COM3)")
    args = parser.parse_args()
    
    app = PII_TUI(port=args.port)
    app.run()
