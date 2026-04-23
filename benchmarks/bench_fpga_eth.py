import socket
import time
import struct
import argparse
import os

def run_benchmark():
    parser = argparse.ArgumentParser(description="XIIRegexBuilder Ethernet Benchmark")
    parser.add_argument("--fpga-ip", default="192.168.2.10")
    parser.add_argument("--fpga-port", type=int, default=7777)
    parser.add_argument("--host-port", type=int, default=7778)
    parser.add_argument("--input", default="inputs/large_test_strings.txt")
    parser.add_argument("--regex", default="inputs/regexes.txt")
    parser.add_argument("--window", type=int, default=16)
    parser.add_argument("--verify", action="store_true")
    args = parser.parse_args()

    # Load strings
    with open(args.input, 'r') as f:
        strings = [line.strip() for line in f if line.strip() and not line.startswith('#')]
    
    num_regex = 0
    with open(args.regex, 'r') as f:
        num_regex = sum(1 for line in f if line.strip() and not line.startswith('#'))

    print(f"Loaded {len(strings)} strings and {num_regex} regexes.")

    # UDP Setup
    sock = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
    sock.bind(('', args.host_port))
    sock.settimeout(1.0)

    # Metrics
    start_wall = time.perf_counter()
    total_matches = 0
    
    # Packet protocol constants
    # Header: SEQ(4) | NUM_STR(2)
    # Body: [LEN(1) | DATA(N)] x NUM
    
    seq_num = 0
    results = {}
    
    print(f"Starting Ethernet Benchmark against {args.fpga_ip}...")

    # For Phase 4/5 demonstration, we pack strings 1-per-packet to test RTT
    # In a full run, we would batch them.
    for s in strings:
        payload = struct.pack(">IH", seq_num, 1) # Header
        payload += struct.pack("B", len(s)) + s.encode('ascii') # Body
        
        sock.sendto(payload, (args.fpga_ip, args.fpga_port))
        
        try:
            data, addr = sock.recvfrom(2048)
            # Response: SEQ(4) | NUM(2) | STAMP(4) | [MATCH(bits) | HITS]
            # Since we sent 1 string, we expect 1 result.
            res_seq, res_num, res_stamp = struct.unpack(">IHI", data[:10])
            results[res_seq] = data[10:]
            seq_num += 1
        except socket.timeout:
            print(f"Timeout on seq {seq_num}")
            break

    end_wall = time.perf_counter()
    duration = end_wall - start_wall
    
    print("\n=== Ethernet Benchmark Summary ===")
    print(f"Total Time: {duration:.4f} seconds")
    print(f"Throughput: {len(results)/duration:.2f} strings/sec")
    print(f"Success: {len(results)}/{len(strings)}")
    
    sock.close()

if __name__ == "__main__":
    run_benchmark()
