# Stage 1 — C++ Regex Parser

This stage is responsible for transforming a raw regular expression string into an Abstract Syntax Tree (AST) that can be processed by the NFA construction stage.

---

## 1. Lexer Details

### Overview
The lexer is the first component. Its primary responsibility is to read a raw regular expression string character-by-character and transform it into a sequence of typed tokens. This abstraction simplifies the downstream parsing process by handling character-level concerns like literal character validation and metacharacter identification.

### Token Types
The lexer identifies the following token types:

| Token Type | Representation | Description |
|---|---|---|
| `LITERAL` | Any printable ASCII (32–126) | Represents a literal character match. |
| `DOT` | `.` | Matches any single character. |
| `STAR` | `*` | Kleene star: zero or more repetitions. |
| `PLUS` | `+` | One or more repetitions. |
| `QUESTION` | `?` | Optional: zero or one occurrence. |
| `PIPE` | `\|` | Union / Alternation operator. |
| `LPAREN` | `(` | Start of a subexpression group. |
| `RPAREN` | `)` | End of a subexpression group. |
| `END_OF_INPUT`| `\0` | Sentinel token marking the end of the regex string. |

### Input Processing Rules
1. **Character Filtering:** Only printable ASCII characters (codes 32–126) are allowed. Non-printable characters are reported by their numeric code in error messages.
2. **Coordinate Tracking:** Each token records its `line` and `column` number for precise error reporting.
3. **Implicit Concatenation:** The lexer treats characters as discrete units. The parser handles the logic of implicit concatenation.
4. **Metacharacter Recognition:** Characters like `*`, `+`, `?`, `|`, `(`, `)`, and `.` are immediately recognized as their respective operator tokens.

---

## 2. Parser Details

### Overview
The parser consumes the token stream produced by the lexer and constructs an Abstract Syntax Tree (AST). It uses a **recursive descent** approach and strictly enforces operator precedence.

### Grammar and Precedence
The parser implements the following grammar (highest to lowest precedence):

1. **Atom:** The basic building blocks (Literal, Dot, or a Parenthesized Expression).
2. **Factor:** An Atom optionally followed by a quantifier (`*`, `+`, or `?`).
3. **Term:** One or more concatenated Factors.
4. **Expression:** One or more Terms separated by the union operator (`|`).

### AST Node Structure
The parser produces a tree composed of various node types (Literal, Dot, Concatenation, Union, Star, Plus, Optional). 

**Important Design Note:** While the base `ASTNode` class contains fields for `nullable`, `firstpos`, and `lastpos`, these are **not** populated by the parser. They are reserved for and populated by the **Stage 2 — NFA Builder**.

### Error Handling
The parser detects and reports the following errors with meaningful messages including line and column numbers:

- **Unmatched Parentheses:** e.g., `(a|b`.
- **Quantifier applied to nothing:** e.g., `*abc` or `(+a)`.
- **Empty alternation branch:** Both left-side (e.g., `|abc`) and right-side (e.g., `abc|`) are explicitly detected.
- **Unexpected Tokens:** Tokens that don't fit the expected grammar at the current position.

---

## Stage 2 — NFA Construction

This stage converts the Abstract Syntax Tree (AST) into an ε-free Non-deterministic Finite Automaton (NFA) using Glushkov's algorithm.

---

### 1. Glushkov's Algorithm
The algorithm produces an NFA with exactly $n+1$ states, where $n$ is the number of symbol occurrences (literals or dots) in the regex.

#### Construction Steps:
1. **Linearization:** Each symbol is assigned a unique integer position (1 to $n$).
2. **Nullable:** Computes if a sub-expression can match the empty string.
3. **Firstpos:** The set of positions that can match the first character of a string.
4. **Lastpos:** The set of positions that can match the last character of a string.
5. **Followpos:** A map where for each position $p$, it stores the set of positions that can immediately follow it.
6. **Transitions:** 
   - State 0 is the start state.
   - Transitions from state 0 go to all positions in `firstpos` of the root.
   - Transitions from state $p$ go to all positions in `followpos(p)`.

### 2. Global State Numbering
To facilitate Stage 3 (Verilog Emitter), every NFA state across all input regular expressions is assigned a **globally unique ID**. This ensures that multiple FSM modules can be instantiated in a single Verilog project without identifier collisions.

### 3. Dot Operator Handling
The dot (`.`) matches any character. In the NFA, a dot position generates 256 individual transition arcs for every possible byte value (0–255). This ensures the hardware matcher correctly identifies any character in that position.

---

## Validation and Simulation

To ensure the integrity of the regex-to-NFA conversion before hardware generation, the builder includes an optional simulation engine.

### 1. NFA Simulation
The `NFA::simulate` method implements a software-based FSM runner:
- It tracks the set of active states simultaneously (handling non-determinism).
- It consumes the input string character-by-character.
- It returns `true` if any final active state is an acceptance state.


## Stage 3 — Verilog Emitter

This stage transforms the internal NFA structures into synthesizable Verilog HDL code.

### 1. Per-NFA Modules
For each regular expression, the emitter generates a self-contained Verilog module (`nfa_N.v`):
- **File I/O:** All file and directory operations use the C++17 `<filesystem>` library for cross-platform compatibility and improved error handling. File streams are configured to throw exceptions on failure, providing detailed system-level error messages (e.g., "Permission denied").
- **Robustness:** The emitter validates its inputs before generating code. It will skip emission if no valid NFAs are provided and will throw an error if the golden reference data does not match the number of NFAs, preventing the generation of invalid Verilog.
- **One-Hot Encoding:** The state register uses one-hot encoding (one flip-flop per NFA state). This is ideal for FPGA implementation as it results in high-speed, shallow combinational logic.
- **Optimized Next-State Logic:** Transitions are implemented as pure combinational logic. To improve generation speed, the logic is built by iterating over destination states rather than source states.
- **Deterministic Output:** Global state IDs are sorted during emission to ensure consistent and deterministic Verilog code generation.
- **Match Logic:** The `match` output is registered. The logic uses a Verilog OR-reduction (`|{...}`) for a concise and efficient way to check if any of the final accept states are active.

### 2. Top-Level Wrapper
A `top.v` module is generated to instantiate all NFA modules in parallel.
- All modules share the same clock, reset, and input character stream.
- The results are aggregated into a `match_bus` where each bit corresponds to one regular expression (Regex $k$ maps to `match_bus[k]`).

### 3. Simulation Testbench
A fully functional testbench (`tb_top.v`) is generated:
- It automatically loads test cases from `test_strings.txt`.
- It integrates **Golden Reference** matches generated via `std::regex`.
- For each test string, it drives the `start`, `char_in`, and `end_of_str` signals with correct, deterministic timing. The testbench now samples the registered `match` output on the exact clock cycle it becomes valid (the cycle immediately following the assertion of `end_of_str`), explicitly avoiding race conditions and double-sampling bugs.
- It reports `PASS` or `FAIL` for each test case directly in the simulation console and generates a `dump.vcd` for waveform analysis.
