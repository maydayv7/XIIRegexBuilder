import serial
import serial.tools.list_ports
import sys
import argparse
import os
from datetime import datetime
from textual.app import App, ComposeResult
from textual.widgets import Header, Footer, Input, RichLog, Static, TabbedContent, TabPane, DataTable, Label, Button, Select, Switch
from textual.containers import Vertical, Horizontal, Container, VerticalScroll
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
        border: heavy #444444;
        background: #1A1B26;
        color: #A9B1D6;
    }
    """

    def compose(self) -> ComposeResult:
        with Vertical(id="help_dialog"):
            yield Static("[bold #7AA2F7]XII Regex Builder - PII Guard TUI[/]\n", id="help_title")
            yield Static("Keyboard Shortcuts:\n"
                         "  [#7AA2F7]?[/#7AA2F7] / [#7AA2F7]F1[/#7AA2F7] : Show this help\n"
                         "  [#7AA2F7]q[/#7AA2F7] : Quit application\n"
                         "  [#7AA2F7]c[/#7AA2F7] : Clear log output\n"
                         "  [#7AA2F7]s[/#7AA2F7] : Save session log to file\n"
                         "  [#7AA2F7]p[/#7AA2F7] : Toggle auto-scrolling\n"
                         "  [#7AA2F7]h[/#7AA2F7] : Toggle hex dump view\n"
                         "  [#7AA2F7]r[/#7AA2F7] : Reconnect to UART\n"
                         "  [#7AA2F7]Up/Down[/#7AA2F7] : Input history\n")
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
        
        scroll_status = "[#7AA2F7]ON[/]" if self.auto_scroll else "[#F7768E]OFF[/]"
        hex_status = "[#7AA2F7]ON[/]" if self.hex_view else "[#F7768E]OFF[/]"
        
        return (
            f"[bold #7AA2F7]XII REGEX BUILDER[/]\n"
            f"[dim]Hardware PII Guard[/]\n\n"
            f"Status: [{color}]{self.status}[/{color}]\n"
            f"Port:   [#BB9AF7]{self.app.port}[/#BB9AF7]\n\n"
            f"Tx: {tx_led} [cyan]{self.bytes_sent}B[/cyan]\n"
            f"Rx: {rx_led} [magenta]{self.bytes_received}B[/magenta]\n\n"
            f"[dim]Auto-Scroll:[/] {scroll_status}\n"
            f"[dim]Hex View:[/]    {hex_status}\n"
        )

class PII_TUI(App):
    """A Textual TUI for interacting with the FPGA PII Guard."""
    
    CSS = """
    PII_TUI {
        background: #1A1B26;
    }

    #main_layout {
        height: 1fr;
    }

    StatusDisplay {
        width: 30;
        height: 1fr;
        background: #16161E;
        color: #A9B1D6;
        padding: 1 2;
        border-right: double #444444;
    }

    TabbedContent {
        height: 1fr;
    }

    TabPane {
        padding: 0;
    }

    #output_container {
        height: 1fr;
    }

    #output_log {
        background: #16161E;
        color: #C0CAF5;
        border: heavy #444444;
        height: 1fr;
        margin: 1 1 0 1;
    }
    
    #input_container {
        height: auto;
        dock: bottom;
        margin: 0 1 1 1;
        background: #16161E;
        border: heavy #444444;
    }
    
    #input_container:focus-within {
        border: heavy #7AA2F7;
    }

    #prompt {
        padding: 1 1 0 2;
        color: #7AA2F7;
        background: #16161E;
        width: auto;
    }

    Input {
        width: 1fr;
        background: #16161E;
        color: #C0CAF5;
        border: none;
    }

    Input:focus {
        border: none;
    }

    /* Settings Tab Styles */
    #settings_container {
        padding: 2;
    }

    .setting_row {
        height: auto;
        margin-bottom: 1;
        padding: 1;
    }

    .setting_label {
        width: 20;
        color: #7AA2F7;
    }

    /* Regex Monitor Styles */
    #regex_table {
        height: 1fr;
        border: heavy #444444;
        margin: 1;
    }
    """

    BINDINGS = [
        ("q", "quit", "Quit"),
        ("c", "clear", "Clear"),
        ("s", "save_log", "Save"),
        ("p", "toggle_autoscroll", "Scroll"),
        ("h", "toggle_hex", "Hex"),
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
        self.regexes = []

    def compose(self) -> ComposeResult:
        yield Header(show_clock=True)
        with Horizontal(id="main_layout"):
            yield StatusDisplay(id="status_display")
            with TabbedContent(id="tabs"):
                with TabPane("Console", id="console_tab"):
                    with Vertical(id="output_container"):
                        yield RichLog(id="output_log", markup=True, wrap=True)
                        with Horizontal(id="input_container"):
                            yield Static("fpga-guard> ", id="prompt")
                            yield Input(placeholder="Type text to stream to FPGA...", id="input_field")
                with TabPane("Regex Monitor", id="regex_tab"):
                    yield DataTable(id="regex_table")
                with TabPane("Settings", id="settings_tab"):
                    with VerticalScroll(id="settings_container"):
                        with Horizontal(classes="setting_row"):
                            yield Label("Serial Port:", classes="setting_label")
                            yield Select(id="port_select", options=[(self.port, self.port)])
                            yield Button("Refresh", id="refresh_ports", variant="primary")
                        with Horizontal(classes="setting_row"):
                            yield Label("Baudrate:", classes="setting_label")
                            yield Select(id="baud_select", options=[(str(b), str(b)) for b in [9600, 38400, 57600, 115200, 230400, 460800]], value=str(self.baudrate))
                        with Horizontal(classes="setting_row"):
                            yield Label("Auto-Scroll:", classes="setting_label")
                            yield Switch(id="scroll_switch", value=self.auto_scroll)
                        with Horizontal(classes="setting_row"):
                            yield Label("Hex View:", classes="setting_label")
                            yield Switch(id="hex_switch", value=self.hex_view)
                        yield Button("Apply & Reconnect", id="apply_settings", variant="success")
        yield Footer()

    def on_mount(self) -> None:
        """Called when the app is mounted."""
        # Load regexes for the monitor
        self.load_regexes()
        
        # Initialize regex table
        table = self.query_one("#regex_table", DataTable)
        table.add_columns("ID", "Pattern", "Match Count")
        for i, regex in enumerate(self.regexes):
            table.add_row(str(i), regex, "0")
            
        # Initial port discovery
        self.refresh_ports()
        
        # Connect
        self.action_reconnect()

    def load_regexes(self):
        try:
            if os.path.exists("inputs/regexes.txt"):
                with open("inputs/regexes.txt", "r") as f:
                    for line in f:
                        line = line.strip()
                        if line and not line.startswith("#"):
                            self.regexes.append(line)
        except Exception as e:
            self.notify(f"Failed to load regexes: {e}", severity="error")

    def refresh_ports(self):
        ports = [p.device for p in serial.tools.list_ports.comports()]
        if not ports:
            ports = [self.port]
        
        select = self.query_one("#port_select", Select)
        select.set_options([(p, p) for p in ports])
        if self.port in ports:
            select.value = self.port
        else:
            select.value = ports[0]
        self.notify("Serial ports refreshed.")

    @on(Button.Pressed, "#refresh_ports")
    def on_refresh_ports(self):
        self.refresh_ports()

    @on(Button.Pressed, "#apply_settings")
    def on_apply_settings(self):
        port_select = self.query_one("#port_select", Select)
        baud_select = self.query_one("#baud_select", Select)
        scroll_switch = self.query_one("#scroll_switch", Switch)
        hex_switch = self.query_one("#hex_switch", Switch)
        
        self.port = str(port_select.value)
        self.baudrate = int(str(baud_select.value))
        self.auto_scroll = scroll_switch.value
        self.hex_view = hex_switch.value
        
        # Update reactive states
        status = self.query_one("#status_display", StatusDisplay)
        status.auto_scroll = self.auto_scroll
        status.hex_view = self.hex_view
        
        self.notify(f"Settings applied. Reconnecting to {self.port}...")
        self.action_reconnect()

    @work(exclusive=True, thread=True)
    def read_from_serial(self):
        """Continuously read from serial port in a background thread."""
        while True:
            if self.ser and self.ser.is_open:
                try:
                    in_waiting = self.ser.in_waiting
                    if in_waiting > 0:
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
        self.notify(f"Serial disconnected: {error_msg}", severity="error")
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
            log.write(f"[dim #A9B1D6]{timestamp}[/] [bold yellow]{text}[/bold yellow]")
            if self.auto_scroll:
                log.scroll_end(animate=False)
            return

        self.session_log.append(text)
        self._rx_buffer += text
        
        if self.hex_view:
            while len(self._rx_buffer) >= 16:
                chunk = self._rx_buffer[:16]
                self._rx_buffer = self._rx_buffer[16:]
                hex_str = " ".join(f"{ord(c):02X}" for c in chunk)
                ascii_str = "".join(c if 32 <= ord(c) <= 126 else "." for c in chunk)
                log.write(f"[magenta]{hex_str:<47}[/magenta] | [#9ECE6A]{ascii_str}[/#9ECE6A]")
        else:
            self._rx_buffer = self._rx_buffer.replace('\r', '\n')
            while '\n' in self._rx_buffer:
                line, self._rx_buffer = self._rx_buffer.split('\n', 1)
                escaped_line = line.replace("[", "[[").replace("]", "]]")
                formatted_line = escaped_line.replace("X", "[bold white on #F7768E]X[/bold white on #F7768E]")
                log.write(formatted_line)

        if self.auto_scroll:
            log.scroll_end(animate=False)

    @on(Input.Submitted)
    def on_input_submitted(self, event: Input.Submitted) -> None:
        """Send the whole string to the FPGA when Enter is pressed."""
        text = event.value
        if text:
            if not self.history or self.history[-1] != text:
                self.history.append(text)
            self.history_idx = -1
            self.query_one("#input_field").value = ""
            
            timestamp = datetime.now().strftime("%H:%M:%S.%f")[:-3]
            log = self.query_one("#output_log", RichLog)
            log.write(f"[dim #A9B1D6]{timestamp}[/] [bold #7AA2F7]fpga-guard>[/] {text}")
            if self.auto_scroll:
                log.scroll_end(animate=False)
            
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
        self.notify("Log and statistics cleared.")

    def action_save_log(self) -> None:
        """Save the current log to the output directory using the shadow buffer."""
        if not os.path.exists("output"):
            os.makedirs("output")
        
        timestamp = datetime.now().strftime("%Y%m%d_%H%M%S")
        filename = f"output/pii_session_{timestamp}.log"
        
        try:
            with open(filename, "w") as f:
                f.write("".join(self.session_log))
            self.notify(f"Log exported to {filename}", title="Success")
        except Exception as e:
            self.notify(f"Export failed: {e}", severity="error")

    def action_toggle_autoscroll(self) -> None:
        self.auto_scroll = not self.auto_scroll
        self.query_one("#status_display", StatusDisplay).auto_scroll = self.auto_scroll
        self.query_one("#scroll_switch", Switch).value = self.auto_scroll
        state = "enabled" if self.auto_scroll else "disabled"
        self.notify(f"Auto-scroll {state}.")

    def action_toggle_hex(self) -> None:
        self.hex_view = not self.hex_view
        self.query_one("#status_display", StatusDisplay).hex_view = self.hex_view
        self.query_one("#hex_switch", Switch).value = self.hex_view
        self._rx_buffer = ""
        state = "enabled" if self.hex_view else "disabled"
        self.notify(f"Hex view {state}.")

    def action_reconnect(self) -> None:
        if self.ser and self.ser.is_open:
            try:
                self.ser.close()
            except:
                pass
            self.ser = None
            
        status_widget = self.query_one("#status_display", StatusDisplay)
        try:
            self.ser = serial.Serial(self.port, self.baudrate, timeout=0.1)
            status_widget.status = "CONNECTED"
            self.append_to_log(f"Connected to {self.port} at {self.baudrate} baud.", is_system=True)
            self.read_from_serial()
        except Exception as e:
            status_widget.status = "MOCK"
            self.append_to_log(f"Hardware connection failed: {e}", is_system=True)
            self.notify(f"Connection failed: {e}. Falling back to Mock Mode.", severity="warning")
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
    parser.add_argument("--port", default="/dev/ttyUSB1", help="Serial port")
    args = parser.parse_args()
    
    app = PII_TUI(port=args.port)
    app.title = "XII Regex Builder - PII Guard"
    app.run()
