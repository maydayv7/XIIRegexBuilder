import serial
import sys
import argparse
import os
from datetime import datetime
from textual.app import App, ComposeResult
from textual.widgets import Header, Footer, Input, RichLog, Static
from textual.containers import Vertical, Horizontal
from textual.screen import ModalScreen
from textual import on, work
from textual.reactive import reactive
from textual.events import Key

class HelpScreen(ModalScreen):
    """Screen with a dialog for help."""

    CSS = """
    HelpScreen {
        align: center middle;
        background: black 50%;
    }

    #help_dialog {
        width: 60;
        height: auto;
        padding: 1 2;
        border: heavy #00AA00;
        background: #111111;
        color: #AAAAAA;
    }
    """

    def compose(self) -> ComposeResult:
        with Vertical(id="help_dialog"):
            yield Static("[bold #00FF00]XII Regex Builder - PII Guard TUI[/]\n", id="help_title")
            yield Static("Keyboard Shortcuts:\n"
                         "  [green]?[/green] / [green]F1[/green] : Show this help\n"
                         "  [green]q[/green] : Quit application\n"
                         "  [green]c[/green] : Clear log output\n"
                         "  [green]s[/green] : Save session log to file\n"
                         "  [green]p[/green] : Toggle auto-scrolling\n"
                         "  [green]h[/green] : Toggle hex dump view\n"
                         "  [green]r[/green] : Attempt connection recovery\n"
                         "  [green]Up/Down[/green] : Navigate input history\n")
            yield Static("\nPress any key to close.")

    def on_key(self, event: Key) -> None:
        self.app.pop_screen()

class StatusDisplay(Static):
    """A widget to display connection status and statistics."""
    
    status = reactive("DISCONNECTED")
    bytes_sent = reactive(0)
    bytes_received = reactive(0)
    tx_active = reactive(False)
    rx_active = reactive(False)
    auto_scroll = reactive(True)
    hex_view = reactive(False)

    def render(self) -> str:
        color = "green" if self.status == "CONNECTED" else "yellow" if self.status == "MOCK" else "red"
        
        tx_led = "[bold bright_green]●[/]" if self.tx_active else "[dim]○[/]"
        rx_led = "[bold bright_cyan]●[/]" if self.rx_active else "[dim]○[/]"
        
        scroll_status = "[green]ON[/]" if self.auto_scroll else "[red]OFF[/]"
        hex_status = "[green]ON[/]" if self.hex_view else "[red]OFF[/]"
        
        return (
            f"[bold]XII Regex Builder[/]\n"
            f"[dim]PII Guard[/]\n\n"
            f"Status: [{color}]{self.status}[/{color}]\n"
            f"Port:   [cyan]{self.app.port}[/cyan]\n\n"
            f"Tx: {tx_led} [blue]{self.bytes_sent}B[/blue]\n"
            f"Rx: {rx_led} [magenta]{self.bytes_received}B[/magenta]\n\n"
            f"[dim]Auto-Scroll:[/] {scroll_status}\n"
            f"[dim]Hex View:[/]    {hex_status}\n"
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
        ("c", "clear", "Clear"),
        ("s", "save_log", "Save"),
        ("p", "toggle_autoscroll", "Auto-Scroll"),
        ("h", "toggle_hex", "Hex View"),
        ("r", "reconnect", "Reconnect"),
        ("question_mark", "help", "Help"),
        ("f1", "help", "Help"),
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
        self.session_log = []
        self._rx_buffer = ""
        self.auto_scroll = True
        self.hex_view = False

    def compose(self) -> ComposeResult:
        yield Header(show_clock=True)
        with Horizontal(id="main_layout"):
            yield StatusDisplay(id="status_display")
            with Vertical(id="output_container"):
                yield RichLog(id="output_log", markup=True, wrap=True)
                with Horizontal(id="input_container"):
                    yield Static("fpga-guard> ", id="prompt")
                    yield Input(placeholder="Type text to stream to FPGA... (Press ? for Help)", id="input_field")
        yield Footer()

    def on_mount(self) -> None:
        """Called when the app is mounted."""
        self.action_reconnect()

    @work(exclusive=True, thread=True)
    def read_from_serial(self):
        """Continuously read from serial port in a background thread."""
        while True:
            if self.ser and self.ser.is_open:
                try:
                    in_waiting = self.ser.in_waiting
                    if in_waiting > 0:
                        # Chunked reading for high-latency/high-throughput robustness
                        chunk = self.ser.read(min(in_waiting, 4096)).decode(errors='ignore')
                        if chunk:
                            self.call_next(self.increment_rx, len(chunk))
                            self.call_next(self.append_to_log, chunk)
                except Exception as e:
                    self.call_next(self.handle_disconnect, str(e))
                    break
            else:
                import time
                time.sleep(0.01)

    def handle_disconnect(self, error_msg: str):
        status_widget = self.query_one("#status_display", StatusDisplay)
        status_widget.status = "DISCONNECTED"
        self.append_to_log(f"Serial Disconnected: {error_msg}", is_system=True)
        if self.ser:
            try:
                self.ser.close()
            except:
                pass
            self.ser = None

    def _reset_tx_led(self):
        self.query_one("#status_display", StatusDisplay).tx_active = False
        
    def _reset_rx_led(self):
        self.query_one("#status_display", StatusDisplay).rx_active = False

    def increment_rx(self, count: int):
        status = self.query_one("#status_display", StatusDisplay)
        status.bytes_received += count
        status.rx_active = True
        if self._rx_timer:
            self._rx_timer.stop()
        self._rx_timer = self.set_timer(0.1, self._reset_rx_led)

    def increment_tx(self, count: int):
        status = self.query_one("#status_display", StatusDisplay)
        status.bytes_sent += count
        status.tx_active = True
        if self._tx_timer:
            self._tx_timer.stop()
        self._tx_timer = self.set_timer(0.1, self._reset_tx_led)

    def append_to_log(self, text: str, is_system: bool = False):
        log = self.query_one("#output_log", RichLog)
        
        if is_system:
            self.session_log.append(f"[SYS] {text}\n")
            timestamp = datetime.now().strftime("%H:%M:%S.%f")[:-3]
            log.write(f"[dim gray]{timestamp}[/] [bold yellow]{text}[/bold yellow]")
            if self.auto_scroll:
                log.scroll_end(animate=False)
            return

        # For normal text, save to shadow buffer
        self.session_log.append(text)
        
        self._rx_buffer += text
        
        if self.hex_view:
            while len(self._rx_buffer) >= 16:
                chunk = self._rx_buffer[:16]
                self._rx_buffer = self._rx_buffer[16:]
                hex_str = " ".join(f"{ord(c):02X}" for c in chunk)
                ascii_str = "".join(c if 32 <= ord(c) <= 126 else "." for c in chunk)
                log.write(f"[magenta]{hex_str:<47}[/magenta] | [green]{ascii_str}[/green]")
        else:
            self._rx_buffer = self._rx_buffer.replace('\r', '\n')
            while '\n' in self._rx_buffer:
                line, self._rx_buffer = self._rx_buffer.split('\n', 1)
                escaped_line = line.replace("[", "[[").replace("]", "]]")
                formatted_line = escaped_line.replace("X", "[bold white on red]X[/bold white on red]")
                log.write(formatted_line)

        if self.auto_scroll:
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
            
            # Write a timestamped entry for our own message 
            timestamp = datetime.now().strftime("%H:%M:%S.%f")[:-3]
            log = self.query_one("#output_log", RichLog)
            log.write(f"[dim gray]{timestamp}[/] [bold cyan]fpga-guard>[/] {text}")
            if self.auto_scroll:
                log.scroll_end(animate=False)
            
            # Append tx entry to shadow buffer
            self.session_log.append(f"\n[TX] {text}\n")
            
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
        self.query_one("#status_display", StatusDisplay).bytes_sent = 0
        self.query_one("#status_display", StatusDisplay).bytes_received = 0
        self._rx_buffer = ""

    def action_save_log(self) -> None:
        """Save the current log to the output directory using the shadow buffer."""
        if not os.path.exists("output"):
            os.makedirs("output")
        
        timestamp = datetime.now().strftime("%Y%m%d_%H%M%S")
        filename = f"output/pii_session_{timestamp}.log"
        
        try:
            with open(filename, "w") as f:
                f.write("".join(self.session_log))
            self.append_to_log(f"Session completely exported to {filename}", is_system=True)
        except Exception as e:
            self.append_to_log(f"Failed to export session: {e}", is_system=True)

    def action_toggle_autoscroll(self) -> None:
        self.auto_scroll = not self.auto_scroll
        self.query_one("#status_display", StatusDisplay).auto_scroll = self.auto_scroll
        state = "ON" if self.auto_scroll else "OFF"
        self.append_to_log(f"Auto-scroll {state}", is_system=True)

    def action_toggle_hex(self) -> None:
        self.hex_view = not self.hex_view
        self.query_one("#status_display", StatusDisplay).hex_view = self.hex_view
        
        # Flush buffer before switching mode
        self._rx_buffer = ""
        
        state = "ON" if self.hex_view else "OFF"
        self.append_to_log(f"Hex View {state}", is_system=True)

    def action_reconnect(self) -> None:
        if self.ser and self.ser.is_open:
            self.append_to_log("Already connected.", is_system=True)
            return
            
        status_widget = self.query_one("#status_display", StatusDisplay)
        try:
            self.ser = serial.Serial(self.port, self.baudrate, timeout=0.1)
            status_widget.status = "CONNECTED"
            self.append_to_log(f"Connected to {self.port} at {self.baudrate} baud.", is_system=True)
            self.read_from_serial()
        except Exception as e:
            status_widget.status = "MOCK"
            self.append_to_log(f"Connection failed: {e}", is_system=True)
            self.append_to_log("Running in Mock Mode (No FPGA detected). Press 'r' to retry.", is_system=True)
            self.ser = None

    def action_help(self) -> None:
        self.push_screen(HelpScreen())

    def on_unmount(self) -> None:
        if self.ser:
            try:
                self.ser.close()
            except:
                pass

if __name__ == "__main__":
    parser = argparse.ArgumentParser(description="PII Guard TUI")
    parser.add_argument("--port", default="/dev/ttyUSB1", help="Serial port (e.g. /dev/ttyS3 for COM3)")
    args = parser.parse_args()
    
    app = PII_TUI(port=args.port)
    app.title = "XII Regex Builder - PII Guard"
    app.run()
