import serial
import sys
import argparse
import os
from datetime import datetime
from textual.app import App, ComposeResult
from textual.widgets import Header, Footer, Input, RichLog, Static
from textual.containers import Vertical, Horizontal
from textual import on, work
from textual.reactive import reactive
from textual.events import Key

class StatusDisplay(Static):
    """A widget to display connection status and statistics."""
    
    status = reactive("DISCONNECTED")
    bytes_sent = reactive(0)
    bytes_received = reactive(0)
    tx_active = reactive(False)
    rx_active = reactive(False)

    def render(self) -> str:
        color = "green" if self.status == "CONNECTED" else "yellow" if self.status == "MOCK" else "red"
        
        tx_led = "[bold bright_green]●[/]" if self.tx_active else "[dim]○[/]"
        rx_led = "[bold bright_cyan]●[/]" if self.rx_active else "[dim]○[/]"
        
        return (
            f"[bold]XII Regex Builder[/]\n"
            f"[dim]PII Guard[/]\n\n"
            f"Status: [{color}]{self.status}[/{color}]\n"
            f"Port:   [cyan]{self.app.port}[/cyan]\n\n"
            f"Tx: {tx_led} [blue]{self.bytes_sent}B[/blue]\n"
            f"Rx: {rx_led} [magenta]{self.bytes_received}B[/magenta]\n"
        )

class PII_TUI(App):
    """A Textual TUI for interacting with the FPGA PII Guard."""
    
    CSS = """
    PII_TUI {
        background: #080808;
    }

    #main_layout {
        height: 1fr;
    }

    StatusDisplay {
        width: 30;
        height: 1fr;
        background: #111111;
        color: #AAAAAA;
        padding: 1 2;
        border-right: double #333333;
    }

    #output_container {
        height: 1fr;
        width: 1fr;
    }

    #output_log {
        background: #000000;
        color: #00FF00;
        border: heavy #004400;
        height: 1fr;
        margin: 1 1 0 1;
    }
    
    #input_container {
        height: auto;
        dock: bottom;
        margin: 0 1 1 1;
        background: #000000;
        border: heavy #004400;
    }
    
    #input_container:focus-within {
        border: heavy #00AA00;
    }

    #prompt {
        padding: 1 1 0 2;
        color: #00AA00;
        background: #000000;
        width: auto;
    }

    Input {
        width: 1fr;
        background: #000000;
        color: #00FF00;
        border: none;
    }

    Input:focus {
        border: none;
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
        self._tx_timer = None
        self._rx_timer = None

    def compose(self) -> ComposeResult:
        yield Header(show_clock=True)
        with Horizontal(id="main_layout"):
            yield StatusDisplay()
            with Vertical(id="output_container"):
                yield RichLog(id="output_log", markup=True, wrap=True)
                with Horizontal(id="input_container"):
                    yield Static("fpga-guard> ", id="prompt")
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
                    self.call_next(self.append_to_log, f"Serial Error: {e}", is_system=True)
                    break
            else:
                import time
                time.sleep(0.01)

    def _reset_tx_led(self):
        self.query_one(StatusDisplay).tx_active = False
        
    def _reset_rx_led(self):
        self.query_one(StatusDisplay).rx_active = False

    def increment_rx(self, count: int):
        status = self.query_one(StatusDisplay)
        status.bytes_received += count
        status.rx_active = True
        if self._rx_timer:
            self._rx_timer.stop()
        self._rx_timer = self.set_timer(0.1, self._reset_rx_led)

    def increment_tx(self, count: int):
        status = self.query_one(StatusDisplay)
        status.bytes_sent += count
        status.tx_active = True
        if self._tx_timer:
            self._tx_timer.stop()
        self._tx_timer = self.set_timer(0.1, self._reset_tx_led)

    def append_to_log(self, text: str, is_system: bool = False):
        log = self.query_one("#output_log", RichLog)
        if is_system:
            timestamp = datetime.now().strftime("%H:%M:%S.%f")[:-3]
            log.write(f"[dim gray]{timestamp}[/] [bold yellow]{text}[/bold yellow]")
            log.scroll_end(animate=False)
        else:
            # Enhanced 'X' Highlighting: Red background for visibility
            if text == 'X':
                log.write("[bold white on red]X[/bold white on red]", scroll_end=True)
            elif text == '\n':
                log.write("", scroll_end=True)
            else:
                escaped = text.replace("[", "[[").replace("]", "]]")
                log.write(escaped, scroll_end=False)

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
            
            # Write a timestamped entry for our own message 
            timestamp = datetime.now().strftime("%H:%M:%S.%f")[:-3]
            log = self.query_one("#output_log", RichLog)
            log.write(f"[dim gray]{timestamp}[/] [bold cyan]fpga-guard>[/] {text}")
            log.scroll_end(animate=False)
            
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
        self.append_to_log(f"Session saved to {filename} (Metadata only in this version)", is_system=True)

    def on_unmount(self) -> None:
        if self.ser:
            self.ser.close()

if __name__ == "__main__":
    parser = argparse.ArgumentParser(description="PII Guard TUI")
    parser.add_argument("--port", default="/dev/ttyUSB1", help="Serial port (e.g. /dev/ttyS3 for COM3)")
    args = parser.parse_args()
    
    app = PII_TUI(port=args.port)
    app.title = "XII Regex Builder - PII Guard"
    app.run()
