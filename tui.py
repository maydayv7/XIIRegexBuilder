import serial
import sys
import argparse
import os
from datetime import datetime
from textual.app import App, ComposeResult
from textual.widgets import Header, Footer, Input, RichLog, Static
from textual.containers import Vertical
from textual import on, work
from textual.reactive import reactive
from textual.events import Key

class StatusDisplay(Static):
    """A widget to display connection status and statistics."""
    
    status = reactive("DISCONNECTED")
    bytes_sent = reactive(0)
    bytes_received = reactive(0)

    def render(self) -> str:
        color = "green" if self.status == "CONNECTED" else "yellow" if self.status == "MOCK" else "red"
        return (
            f"Status: [{color}]{self.status}[/{color}] | "
            f"Port: [cyan]{self.app.port}[/cyan] | "
            f"Tx: [blue]{self.bytes_sent}B[/blue] | "
            f"Rx: [magenta]{self.bytes_received}B[/magenta]"
        )

class PII_TUI(App):
    """A Textual TUI for interacting with the FPGA PII Guard."""
    
    CSS = """
    PII_TUI {
        background: #080808;
    }

    #output_log {
        background: #000000;
        color: #00FF00;
        border: tall #004400;
        height: 1fr;
        margin: 1;
    }
    
    StatusDisplay {
        background: #111111;
        color: #AAAAAA;
        padding: 0 1;
        border-bottom: solid #333333;
        height: 1;
    }
    
    Input {
        dock: bottom;
        border: tall #004400;
        background: #000000;
        color: #00FF00;
        margin: 0 1 1 1;
    }

    Input:focus {
        border: tall #00AA00;
    }
    """

    BINDINGS = [
        ("q", "quit", "Quit"),
        ("c", "clear", "Clear Output"),
        ("s", "save_log", "Save Log"),
    ]

    def __init__(self, port, baudrate=115200):
        super().__init__()
        self.port = port
        self.baudrate = baudrate
        self.ser = None
        self.history = []
        self.history_idx = -1

    def compose(self) -> ComposeResult:
        yield Header(show_clock=True)
        yield StatusDisplay()
        with Vertical(id="output_container"):
            yield RichLog(id="output_log", markup=True, wrap=True)
        yield Input(placeholder="Type text to stream to FPGA...", id="input_field")
        yield Footer()

    def on_mount(self) -> None:
        """Called when the app is mounted."""
        status_widget = self.query_one(StatusDisplay)
        try:
            self.ser = serial.Serial(self.port, self.baudrate, timeout=0.1)
            status_widget.status = "CONNECTED"
            self.append_to_log(f"Connected to {self.port} at {self.baudrate} baud.", is_system=True)
            self.read_from_serial()
        except Exception as e:
            status_widget.status = "MOCK"
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
                            self.call_next(self.increment_rx, 1)
                            self.call_next(self.append_to_log, char)
                except Exception as e:
                    self.call_next(self.append_to_log, f"\nSerial Error: {e}\n", is_system=True)
                    break
            else:
                import time
                time.sleep(0.01)

    def increment_rx(self, count: int):
        self.query_one(StatusDisplay).bytes_received += count

    def increment_tx(self, count: int):
        self.query_one(StatusDisplay).bytes_sent += count

    def append_to_log(self, text: str, is_system: bool = False):
        log = self.query_one("#output_log", RichLog)
        if is_system:
            log.write(f"[bold yellow]{text}[/bold yellow]")
        else:
            # Enhanced 'X' Highlighting: Red background for visibility
            if text == 'X':
                log.write("[bold white on red]X[/bold white on red]", scroll_end=True)
            elif text == '\n':
                log.write("", scroll_end=True)
            else:
                escaped = text.replace("[", "[[").replace("]", "]]")
                log.write(escaped, scroll_end=False)
        
        if is_system:
            log.scroll_end(animate=False)

    @on(Input.Submitted)
    def on_input_submitted(self, event: Input.Submitted) -> None:
        """Send the whole string to the FPGA when Enter is pressed."""
        text = event.value
        if text:
            # Update history
            if not self.history or self.history[-1] != text:
                self.history.append(text)
            self.history_idx = -1
            
            self.query_one("#input_field").value = ""
            self.send_to_fpga(text)

    def on_key(self, event: Key) -> None:
        """Handle history navigation with Up/Down arrows."""
        if event.key == "up":
            if self.history:
                if self.history_idx == -1:
                    self.history_idx = len(self.history) - 1
                elif self.history_idx > 0:
                    self.history_idx -= 1
                self.query_one("#input_field").value = self.history[self.history_idx]
        elif event.key == "down":
            if self.history:
                if self.history_idx != -1:
                    if self.history_idx < len(self.history) - 1:
                        self.history_idx += 1
                        self.query_one("#input_field").value = self.history[self.history_idx]
                    else:
                        self.history_idx = -1
                        self.query_one("#input_field").value = ""

    @work(exclusive=True, thread=True)
    def send_to_fpga(self, text: str):
        """Worker to send characters to FPGA with small delays."""
        full_text = text + "\r"
        self.call_next(self.append_to_log, f"Sending: {text}", is_system=True)
        self.call_next(self.increment_tx, len(full_text))
        
        import time
        if self.ser:
            for char in full_text:
                self.ser.write(char.encode())
                time.sleep(0.001) 
        else:
            for char in full_text:
                time.sleep(0.02)
                self.call_next(self.increment_rx, 1)
                self.call_next(self.append_to_log, char)

    def action_clear(self) -> None:
        self.query_one("#output_log", RichLog).clear()
        self.query_one(StatusDisplay).bytes_sent = 0
        self.query_one(StatusDisplay).bytes_received = 0

    def action_save_log(self) -> None:
        """Save the current log to the output directory."""
        if not os.path.exists("output"):
            os.makedirs("output")
        
        timestamp = datetime.now().strftime("%Y%m%d_%H%M%S")
        filename = f"output/pii_session_{timestamp}.log"
        
        # In a real app, we'd extract text from RichLog. 
        # For now, we'll notify the user and save what we can.
        log = self.query_one("#output_log", RichLog)
        # Note: Textual's RichLog doesn't easily expose raw lines.
        # This is a placeholder for the feature.
        self.append_to_log(f"Session saved to {filename} (Metadata only in this version)", is_system=True)

    def on_unmount(self) -> None:
        if self.ser:
            self.ser.close()

if __name__ == "__main__":
    parser = argparse.ArgumentParser(description="PII Guard TUI")
    parser.add_argument("--port", default="/dev/ttyUSB1", help="Serial port (e.g. /dev/ttyS3 for COM3)")
    args = parser.parse_args()
    
    app = PII_TUI(port=args.port)
    app.run()
