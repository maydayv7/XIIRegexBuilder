import sys
import re

# Enhanced Assembler for Regex CPU
# Layout: [31:24] char | [23:16] next1 | [15:8] next2 | [7:4] mid | [3] term | [0] any


def pack(char_val, next1, next2, mid, term, any_bit):
    return (
        (char_val << 24)
        | (next1 << 16)
        | (next2 << 8)
        | (mid << 4)
        | (term << 3)
        | any_bit
    )


def main():
    input_file = sys.argv[1] if len(sys.argv) > 1 else "regexes.rasm"
    output_file = sys.argv[2] if len(sys.argv) > 2 else "imem.hex"

    mem = [0] * 256
    with open(input_file, "r") as f:
        for line in f:
            line = line.strip()
            if not line or line.startswith("#"):
                continue

            m = re.match(r"(\d+):\s+(\w+)(?:\((.*)\))?", line)
            if not m:
                continue

            pc, instr, args_str = int(m.group(1)), m.group(2), m.group(3)
            args = []
            if args_str:
                for arg in re.split(r",\s*", args_str):
                    arg = arg.strip()
                    if arg.startswith("'") and arg.endswith("'"):
                        args.append(ord(arg[1:-1]))
                    else:
                        args.append(int(arg))

            if instr == "CHAR":
                # Support: CHAR('a') OR CHAR('a', n1, n2, mid, term, any)
                c = args[0]
                n1 = args[1] if len(args) > 1 else pc + 1
                n2 = args[2] if len(args) > 2 else 0
                mid = args[3] if len(args) > 3 else 0
                term = args[4] if len(args) > 4 else 0
                any_b = args[5] if len(args) > 5 else 0
                mem[pc] = pack(c, n1, n2, mid, term, any_b)
            elif instr == "SPLIT":
                mem[pc] = pack(0, args[0], args[1], 0, 0, 0)
            elif instr == "JMP":
                mem[pc] = pack(0, args[0], 0, 0, 0, 0)
            elif instr == "MATCH":
                mem[pc] = pack(0, 0, 0, args[0], 1, 0)
            elif instr == "ANY":
                n1 = args[0] if len(args) > 0 else pc + 1
                mem[pc] = pack(0, n1, 0, 0, 0, 1)

    with open(output_file, "w") as f:
        for val in mem:
            f.write(f"{val:08x}\n")
    print(f"Assembled {input_file} to {output_file}")


if __name__ == "__main__":
    main()
