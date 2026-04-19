#!/usr/bin/env python3
"""
tui.py — Interactive TUI for XIIRegexBuilder
=====================================================================

Connects to the FPGA over a serial port (115200-8N1), lets the user type
input strings, and displays a live colour-coded table showing which of
the N regex patterns were matched by the most recent string.

Usage
─────
    python tui.py [--port PORT] [--baud BAUD] [--regexes FILE]

    --port    Serial port (default: auto-detect first USB-Serial)
    --baud    Baud rate   (default: 115200)
    --regexes Path to inputs/regexes.txt for label display (optional but
              strongly recommended — falls back to "Regex 0", "Regex 1", …)

Protocol
────────
  Send   : "<string>\n"
  Receive: "MATCH=<bits> BYTES=<8hexdigits> HITS=<4hex,…>\r\n"
  Send   : "?\n"  →  receive current counter snapshot (no NFA change)

Controls
────────
  Enter         — send current input to FPGA
  Ctrl-C / q    — quit
  Ctrl-P        — Program FPGA with output/regex.bin
  Ctrl-L        — clear input line
  ?             — query current counters without sending a string
"""

import argparse
import sys
import threading
import time
import re as _re
import os

try:
    import serial
    import serial.tools.list_ports
except ImportError:
    print("ERROR: pyserial not found. Run 'pip install pyserial'")
    sys.exit(1)

try:
    from rich import box
    from rich.console import Console
    from rich.console import Group
    from rich.live import Live
    from rich.panel import Panel
    from rich.table import Table
    from rich.text import Text
except ImportError:
    print("ERROR: rich not found. Run 'pip install rich'")
    sys.exit(1)

# OS-specific keyboard handling
if os.name == 'nt':
    import msvcrt
    def get_char():
        if msvcrt.kbhit():
            ch = msvcrt.getch()
            if ch in (b'\x00', b'\xe0'):
                ch += msvcrt.getch()
            return ch
        return None
else:
    import termios
    import tty
    import select
    def get_char():
        if select.select([sys.stdin], [], [], 0)[0]:
            return sys.stdin.read(1).encode('ascii')
        return None

# ─────────────────────────────────────────────────────────────────────────────
BANNER = r"""
██╗  ██╗██╗██╗██████╗ ███████╗ ██████╗ ███████╗██╗  ██╗██████╗ ██╗   ██╗██╗██╗     ██████╗ ███████╗██████╗ 
╚██╗██╔╝██║██║██╔══██╗██╔════╝██╔════╝ ██╔════╝╚██╗██╔╝██╔══██╗██║   ██║██║██║     ██╔══██╗██╔════╝██╔══██╗
 ╚███╔╝ ██║██║██████╔╝█████╗  ██║  ███╗█████╗   ╚███╔╝ ██████╔╝██║   ██║██║██║     ██║  ██║█████╗  ██████╔╝
 ██╔██╗ ██║██║██╔══██╗██╔══╝  ██║   ██║██╔══╝   ██╔██╗ ██╔══██╗██║   ██║██║██║     ██║  ██║██╔══╝  ██╔══██╗
██╔╝ ██╗██║██║██║  ██║███████╗╚██████╔╝███████╗██╔╝ ██╗██████╔╝╚██████╔╝██║███████╗██████╔╝███████╗██║  ██║
╚═╝  ╚═╝╚═╝╚═╝╚═╝  ╚═╝╚══════╝ ╚═════╝ ╚══════╝╚═╝  ╚═╝╚═════╝  ╚═════╝ ╚═╝╚══════╝╚═════╝ ╚══════╝╚═╝  ╚═╝
"""

RESPONSE_RE = _re.compile(
    r"MATCH=([01]+)\s+BYTES=([0-9A-Fa-f]+)\s+HITS=([0-9A-Fa-f,]+)"
)


# ─────────────────────────────────────────────────────────────────────────────
def load_regexes(path: str) -> list[str]:
    """Parse regexes.txt — skip blank lines and comments."""
    patterns = []
    try:
        with open(path) as f:
            for line in f:
                stripped = line.strip()
                if stripped and not stripped.startswith("#"):
                    patterns.append(stripped)
    except FileNotFoundError:
        pass
    return patterns


def auto_detect_port() -> str | None:
    """Return the first USB-Serial port found, or None."""
    for p in serial.tools.list_ports.comports():
        desc = p.description.lower()
        if "usb" in desc or "uart" in desc or "serial" in desc or "ftdi" in desc:
            return p.device
    ports = list(serial.tools.list_ports.comports())
    return ports[0].device if ports else None


# ─────────────────────────────────────────────────────────────────────────────
class MatchState:
    """Shared state between the UART reader thread and the TUI renderer."""

    def __init__(self, num_regex: int):
        self.lock = threading.Lock()
        self.num_regex = 1 # Force 1 regex for dynamic mode
        self.match_bits: list[int] = [0] * self.num_regex
        self.byte_count: int = 0
        self.hit_counts: list[int] = [0] * self.num_regex
        self.last_string: str = ""
        self.last_response_raw: str = ""
        self.status_msg: str = "Waiting for first result…"
        self.connected: bool = False

    def update_from_response(self, raw: str, sent_string: str) -> bool:
        m = RESPONSE_RE.search(raw)
        if not m:
            return False
        bits_str, bytes_hex, hits_str = m.group(1), m.group(2), m.group(3)

        bits_str = bits_str.zfill(self.num_regex)
        match_bits = [int(b) for b in bits_str]
        match_bits_ordered = list(reversed(match_bits))

        hit_list = [int(h, 16) for h in hits_str.split(",")]
        while len(hit_list) < self.num_regex:
            hit_list.append(0)

        with self.lock:
            self.match_bits = match_bits_ordered[: self.num_regex]
            self.byte_count = int(bytes_hex, 16)
            self.hit_counts = hit_list[: self.num_regex]
            self.last_string = sent_string
            self.last_response_raw = raw.strip()
            self.status_msg = "OK"
        return True


# ─────────────────────────────────────────────────────────────────────────────
def reader_thread(
    ser: serial.Serial, state: MatchState, pending: list, pending_lock: threading.Lock
) -> None:
    """Background thread: read lines from FPGA and update state."""
    buf = ""
    while True:
        try:
            if ser.in_waiting:
                chunk = ser.read(ser.in_waiting).decode("ascii", errors="replace")
                buf += chunk
                while "\n" in buf:
                    line, buf = buf.split("\n", 1)
                    line = line.strip()
                    if not line:
                        continue
                    with pending_lock:
                        sent = pending.pop(0) if pending else ""
                    state.update_from_response(line, sent)
            else:
                time.sleep(0.01)
        except serial.SerialException:
            with state.lock:
                state.status_msg = "Serial error — disconnected"
                state.connected = False
            break
        except Exception:
            pass


# ─────────────────────────────────────────────────────────────────────────────
def build_match_table(state: MatchState, patterns: list[str]) -> Table:
    """Build the live Rich table showing match results (1 Regex row)."""
    table = Table(
        title="[bold cyan]Regex Match Result[/bold cyan]",
        box=box.ROUNDED,
        expand=True,
        show_lines=True,
    )

    with state.lock:
        bits = state.match_bits[:]
        hits = state.hit_counts[:]
        n = 1 # Only 1 regex

    table.add_column("#", style="dim", width=4, justify="right")
    table.add_column("Pattern", style="white", min_width=20)
    table.add_column("MATCH", justify="center", width=8)
    table.add_column("Hits", justify="right", width=8)

    label = patterns[0] if len(patterns) > 0 else "Dynamic Regex"
    if len(label) > 50:
        label = label[:47] + "..."
    matched = bool(bits[0]) if len(bits) > 0 else False
    hit_cnt = hits[0] if len(hits) > 0 else 0

    if matched:
        match_cell = Text("● MATCH", style="bold green")
    else:
        match_cell = Text("○ —", style="dim red")

    table.add_row("0", f"[italic]{label}[/italic]", match_cell, f"[yellow]{hit_cnt}[/yellow]")

    return table


def build_stats_panel(state: MatchState) -> Panel:
    with state.lock:
        bc = state.byte_count
        last_s = state.last_string or "(none)"
        raw = state.last_response_raw or "—"
        status = state.status_msg
        conn = state.connected

    conn_label = (
        "[bold green]● CONNECTED[/bold green]"
        if conn
        else "[bold red]● DISCONNECTED[/bold red]"
    )

    text = (
        f"{conn_label}\n"
        f"[bold]Last string :[/bold] [cyan]{last_s}[/cyan]\n"
        f"[bold]Total bytes :[/bold] {bc:,}\n"
        f"[bold]Status      :[/bold] {status}\n"
        f"[bold]Raw response:[/bold] [dim]{raw}[/dim]"
    )
    return Panel(text, title="[bold]Statistics[/bold]", border_style="blue")


# ─────────────────────────────────────────────────────────────────────────────
def run_tui(port: str, baud: int, patterns: list[str]) -> None:
    console = Console()
    num_regex = 1
    state = MatchState(num_regex)

    try:
        ser = serial.Serial(port, baud, timeout=0.05)
        state.connected = True
    except serial.SerialException as e:
        console.print(f"[red]Cannot open {port}: {e}[/red]")
        sys.exit(1)

    pending: list[str] = []
    pending_lock = threading.Lock()

    t = threading.Thread(
        target=reader_thread, args=(ser, state, pending, pending_lock), daemon=True
    )
    t.start()

    input_buffer = ""

    def program_fpga():
        try:
            with state.lock:
                state.status_msg = "Programming..."
            
            if not os.path.exists("output/regex.bin"):
                with state.lock:
                    state.status_msg = "Error: output/regex.bin not found"
                return

            with open("output/regex.bin", "rb") as f:
                payload = f.read()

            if len(payload) != 4098:
                with state.lock:
                    state.status_msg = f"Error: regex.bin is {len(payload)} bytes (expected 4098)"
                return

            # Send Header
            ser.write(b'\xFE\xFE')
            ser.flush()
            time.sleep(0.05) # 50ms wait

            # Send Payload in chunks
            chunk_size = 512
            for i in range(0, len(payload), chunk_size):
                chunk = payload[i:i + chunk_size]
                ser.write(chunk)
                ser.flush()
                time.sleep(0.002) # 2ms sleep between chunks

            with state.lock:
                state.status_msg = "Programmed successfully"
        except Exception as e:
            with state.lock:
                state.status_msg = f"Prog Error: {e}"

    def render_tui():
        table = build_match_table(state, patterns)
        stats = build_stats_panel(state)

        input_panel = Panel(
            f"[bold yellow]Enter string:[/bold yellow] [cyan]{input_buffer}[/cyan][blink]█[/blink]\n"
            f"[dim]Enter:send, Esc/F10:quit, Ctrl-P:Program, Ctrl-R:query, Ctrl-U:clear[/dim]",
            title="[bold]Interaction[/bold]",
            border_style="yellow",
        )

        return Group(table, stats, input_panel)

    console.print(BANNER, style="bold cyan")
    console.print(f"[green]Connected to [bold]{port}[/bold] @ {baud} baud[/green]\n")

    # Use non-canonical mode for Unix
    old_settings = None
    if os.name != 'nt':
        old_settings = termios.tcgetattr(sys.stdin)
        tty.setcbreak(sys.stdin.fileno())

    try:
        with Live(render_tui(), console=console, refresh_per_second=10) as live:
            while True:
                live.update(render_tui())
                char = get_char()
                if char is None:
                    time.sleep(0.01)
                    continue

                # 1. Quit Commands
                if char in (b"\x1b", b"\x11", b"\x18", b"\x00D"): # Esc, Ctrl-Q, Ctrl-X, F10
                    break
                elif char == b"\x03": # Ctrl-C
                    raise KeyboardInterrupt

                # 2. Programming Command (Ctrl-P)
                elif char == b"\x10": # Ctrl-P
                    program_fpga()
                    continue

                # 3. Query Commands
                elif char in (b"\x12", b"\x00;"): # Ctrl-R, F1
                    try:
                        ser.write(b"?")
                        ser.flush()
                        with state.lock:
                            state.status_msg = "Sent Query (?)"
                    except serial.SerialException as exc:
                        with state.lock:
                            state.status_msg = f"Write error: {exc}"
                    continue

                # 4. Input Commands
                elif char == b"\r" or char == b"\n": # Enter
                    if not input_buffer:
                        continue

                    if input_buffer == "?":
                        try:
                            ser.write(b"?")
                            ser.flush()
                            with state.lock:
                                state.status_msg = "Sent Query (?)"
                        except serial.SerialException as exc:
                            with state.lock:
                                state.status_msg = f"Write error: {exc}"
                        input_buffer = ""
                        continue

                    payload = input_buffer + "\n"
                    try:
                        with pending_lock:
                            pending.append(input_buffer)
                        ser.write(payload.encode("ascii"))
                        ser.flush()
                    except serial.SerialException as exc:
                        with state.lock:
                            state.status_msg = f"Write error: {exc}"

                    input_buffer = ""

                elif char in (b"\x08", b"\x7f"): # Backspace
                    input_buffer = input_buffer[:-1]
                elif char in (b"\x0c", b"\x15"): # Ctrl-L, Ctrl-U
                    input_buffer = ""

                # 5. Printable Input
                elif len(char) == 1:
                    try:
                        decoded = char.decode("ascii")
                        if decoded.isprintable():
                            input_buffer += decoded
                    except UnicodeDecodeError:
                        pass

                time.sleep(0.01)
    finally:
        if old_settings:
            termios.tcsetattr(sys.stdin, termios.TCSADRAIN, old_settings)

    ser.close()
    console.print("\n[bold cyan]Bye![/bold cyan]")


# ─────────────────────────────────────────────────────────────────────────────
def main() -> None:
    parser = argparse.ArgumentParser(description="Interactive TUI for XIIRegexBuilder")
    parser.add_argument(
        "--port", default=None, help="Serial port (default: auto-detect)"
    )
    parser.add_argument(
        "--baud", type=int, default=115200, help="Baud rate (default: 115200)"
    )
    parser.add_argument(
        "--regexes",
        default="inputs/regexes.txt",
        help="Path to regexes.txt for pattern labels",
    )
    args = parser.parse_args()

    patterns = load_regexes(args.regexes)

    port = args.port
    if port is None:
        port = auto_detect_port()
        if port is None:
            print("ERROR: No serial port found. Pass --port explicitly.")
            sys.exit(1)
        print(f"Auto-detected port: {port}")

    run_tui(port, args.baud, patterns)


if __name__ == "__main__":
    main()
