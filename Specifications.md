# Regex → NFA → Verilog Hardware Matcher
## Project Specification

---

## 1. Project Overview

This project implements a hardware-accelerated regular expression matching pipeline. A set of regular expressions defined in plain text is automatically compiled into parallel hardware finite state machines (FSMs), expressed in Verilog, and synthesised onto an FPGA. At runtime, a stream of ASCII characters is fed into the hardware one character per clock cycle, and the system reports which regular expressions are matched by the full input string.

The pipeline consists of four major stages: regex parsing, NFA construction, Verilog code generation, and hardware simulation and synthesis.

---

## 2. Finalised Design Decisions

| Decision | Choice |
|---|---|
| Supported operators | Kleene star, plus, optional, union, dot, parentheses, literals |
| Architecture | One independent Verilog FSM module per NFA, all running in parallel |
| ε-transition handling | Retained in the NFA; encoded as combinational pass-through wires in Verilog |
| State encoding | One-hot (one flip-flop per NFA state) |
| Character stream interface | Synchronous; one 8-bit ASCII character per clock cycle |
| Match semantics | Full match only (the entire input string must satisfy the regex) |
| Scale | Fewer than 20 regular expressions |
| Simulation and synthesis tool | Xilinx Vivado |

---

## 3. Input Specification

### 3.1 Regex Input File

The input is a plain text file, referred to throughout as `regexes.txt`. It obeys the following rules:

- One regular expression per line.
- Lines beginning with the `#` character are treated as comments and ignored.
- Blank lines are ignored.
- Each non-comment, non-blank line is assigned a zero-based index that becomes the index of the corresponding NFA and Verilog module.

### 3.2 Supported Regex Syntax

The following constructs must be supported:

- **Literal characters** — any printable ASCII character (codes 32–126) that is not a metacharacter is matched literally.
- **Dot** (`.`) — matches any single character.
- **Kleene star** (`*`) — matches zero or more repetitions of the preceding atom.
- **Plus** (`+`) — matches one or more repetitions of the preceding atom.
- **Optional** (`?`) — matches zero or one occurrence of the preceding atom.
- **Union** (`|`) — matches either the expression on the left or the expression on the right. Union may appear both within a single regex (e.g. between subexpressions) and effectively between entire regex lines via the separate-line structure.
- **Parentheses** (`(` and `)`) — group subexpressions and control the scope of operators.

The operator precedence from highest to lowest is: `*`, `+`, `?` (postfix quantifiers) → concatenation (implicit) → `|` (union).

Anchors (`^` and `$`) are not supported. Full-match semantics are enforced by the hardware control signals rather than by the regex syntax.

### 3.3 Test String Input File

A second plain text input file provides strings to test against the regex set. Each line is one test string. The test strings are used to drive the hardware simulation testbench and to generate golden reference output.

---

## 4. Stage 1 — C++ Regex Parser

### 4.1 Lexer

The lexer reads a regex string character by character and produces a flat sequence of typed tokens. The token types are: literal character, dot, star, plus, question mark, pipe (union), left parenthesis, and right parenthesis.

### 4.2 Parser

The parser consumes the token stream produced by the lexer and constructs an Abstract Syntax Tree (AST). The parser uses recursive descent and enforces the correct operator precedence. The grammar it implements, informally stated, is:

- An expression is one or more concatenated terms separated by `|`.
- A term is one or more concatenated factors.
- A factor is an atom optionally followed by one quantifier (`*`, `+`, or `?`).
- An atom is either a literal, a dot, or a parenthesised expression.

Each AST node is one of the following types: Literal, Dot, Concatenation, Union, Star, Plus, Optional.

### 4.3 Error Handling

The parser must detect and report, with a meaningful message and line number:

- Unmatched parentheses.
- A quantifier applied to nothing (e.g. a leading `*`).
- An empty alternation branch (e.g. `a|`).
- Any character outside the printable ASCII range.

---

## 5. Stage 2 — C++ NFA Construction (Thompson's Construction)

### 5.1 Overview

Thompson's construction is applied to each AST, bottom-up, to produce an NFA. Each sub-NFA produced during the walk has exactly one distinguished start state and exactly one distinguished accept state. This structural invariant is maintained throughout the construction.

### 5.2 Construction Rules per AST Node

Each node type is handled as follows:

- **Literal and Dot** — produce a two-state fragment with a single labelled transition from start to accept.
- **Concatenation** — the accept state of the left fragment is merged with (or connected by ε to) the start state of the right fragment.
- **Union** — a new start state has ε-transitions to the start states of both branches. A new accept state receives ε-transitions from the accept states of both branches.
- **Star** — a new start state has ε-transitions to the inner fragment's start state and directly to a new accept state. The inner fragment's accept state has ε-transitions back to the inner fragment's start state and to the new accept state.
- **Plus** — same as Star except the new start state does not have a direct ε-transition to the new accept state, forcing at least one traversal of the inner fragment.
- **Optional** — a new start state has ε-transitions to both the inner fragment's start state and the new accept state.

### 5.3 NFA Representation

Each NFA is described by a set of numbered states. Each state carries: a unique integer identifier, a Boolean flag indicating whether it is an accept state, a set of labelled transitions (character → destination state), and a set of ε-transitions (unlabelled edges to other states).

### 5.4 Global State Numbering

All NFA states across all N NFAs share a single global numbering space. This avoids identifier collisions when multiple NFAs are emitted into a single project.

---

## 6. Stage 3 — C++ Verilog Emitter

### 6.1 Overview

The emitter walks each NFA and produces a self-contained Verilog module. It also produces a top-level wrapper module and a simulation testbench. All output files are written to the `output/` directory.

### 6.2 ε-Transition Handling in Verilog

ε-transitions are not eliminated before emission. Instead, they are represented as combinational pass-through wires inside each Verilog module. Concretely:

- Each state has a registered bit in the one-hot state register (the "raw" active signal).
- A combinational "effective active" wire for each state is the OR of the state's own registered bit and the effective active signals of all its ε-predecessors.
- This combinational layer computes the ε-closure within a single clock cycle, before the next clock edge.

Because Thompson's construction guarantees that ε-cycles only arise in the context of Kleene star and plus loops — which are resolved by the registered state update — the combinational ε-closure layer is acyclic and safe for synthesis.

### 6.3 Per-NFA Module Interface

Each NFA produces one Verilog module. The module's ports are:

- `clk` — clock input.
- `rst` — synchronous active-high reset.
- `start` — a one-cycle pulse that initialises the FSM to its start state, beginning a new match attempt.
- `end_of_str` — a one-cycle pulse asserted on the same cycle as the final character of the input string.
- `char_in` — an 8-bit input carrying the current ASCII character.
- `match` — a registered output that is asserted for one cycle when the FSM is in an accept state at the moment `end_of_str` is asserted.

### 6.4 One-Hot State Encoding

The state register inside each module is one-hot: there is one flip-flop per NFA state. A bit being set means that NFA state is currently active. Because the NFA is non-deterministic, multiple bits may be set simultaneously, faithfully representing the set of NFA states that are active in parallel.

On reset or on the `start` pulse, the state register is cleared and only the bit corresponding to the NFA's start state is set.

### 6.5 Next-State Logic

The next-state logic is purely combinational. For each state, its next-state bit is the OR over all states that have a transition on the current character to that state, gated by those states being effectively active (including ε-closure). The computed next-state vector is loaded into the state register on the rising clock edge.

### 6.6 Top-Level Wrapper Module

A single top-level Verilog module instantiates all N NFA modules. All instances share the same `clk`, `rst`, `start`, `end_of_str`, and `char_in` signals. Each instance drives one bit of an N-bit `match` output bus, where bit k corresponds to NFA k.

### 6.7 Emitter Output Files

| File | Contents |
|---|---|
| `nfa_0.v` … `nfa_N.v` | One self-contained FSM module per regex |
| `top.v` | Top-level wrapper instantiating all NFA modules |
| `tb_top.v` | Simulation testbench |
| `expected_matches.txt` | Golden reference output for validation |

---

## 7. Stage 4 — Simulation Testbench

### 7.1 Structure

The testbench instantiates `top` and drives all its inputs. It operates as follows for each test string:

1. Assert `rst` for a fixed number of cycles to initialise all FSMs.
2. Assert `start` for one cycle.
3. Drive `char_in` with successive characters of the test string, one per clock cycle.
4. On the last character cycle, also assert `end_of_str`.
5. Sample `match` on the following cycle and compare it against the expected bitmask from the golden reference.
6. Print a PASS or FAIL message for each test case, including the test string and the observed versus expected match bitmasks.

### 7.2 Waveform Dump

The testbench dumps all signals to a VCD file for inspection in Vivado's waveform viewer.

### 7.3 Timing Assumptions

- The clock period is fixed at 10 ns.
- `char_in` is stable before the rising clock edge.
- All control signals (`start`, `end_of_str`) are synchronous and held for exactly one cycle.

---

## 8. Stage 5 — Golden Reference

A separate C++ program reads `regexes.txt` and `test_strings.txt`, runs each test string against each regex using the C++ standard library's full-match mode, and writes `expected_matches.txt`. Each line of the output corresponds to one test string and contains a bitmask (or equivalent structured text) indicating which regexes matched.

This output is consumed by the testbench generator and used during simulation to validate the Verilog results.

---

## 9. Build System and Project Structure

### 9.1 Directory Layout

```
project/
├── regexes.txt
├── test_strings.txt
│
├── src/
│   ├── main.cpp
│   ├── lexer.h / lexer.cpp
│   ├── parser.h / parser.cpp
│   ├── nfa.h / nfa.cpp
│   └── emitter.h / emitter.cpp
│
├── golden/
│   └── golden.cpp
│
└── output/
    ├── nfa_0.v … nfa_N.v
    ├── top.v
    ├── tb_top.v
    └── expected_matches.txt
```

### 9.2 Build and Run Order

The pipeline is executed in the following sequence:

1. Compile and run the C++ frontend on `regexes.txt` to produce all Verilog files and `expected_matches.txt`.
2. Compile and run the golden reference binary on `regexes.txt` and `test_strings.txt` to produce or verify `expected_matches.txt`.
3. Import all Verilog files into a Xilinx Vivado project.
4. Run behavioural simulation; confirm all test cases report PASS.
5. Run synthesis and implementation targeting the chosen FPGA part.
6. Review timing and resource utilisation reports.

---

## 10. Validation Strategy

### 10.1 Unit Testing the C++ Frontend

Each C++ component is tested independently before integration:

- The lexer is verified against a set of regex strings by inspecting the token stream it produces.
- The parser is verified by comparing its AST output against hand-drawn trees for representative regexes.
- The NFA builder is verified by drawing the NFA graphs for simple cases and confirming state counts and transition structures match the expected Thompson's construction output.
- The emitter is verified by compiling its output in Vivado and confirming the modules are syntax-valid.

### 10.2 Simulation Validation

Every test case in the testbench must produce a match bitmask identical to the golden reference output. Any discrepancy is a bug in the emitter or the NFA construction logic.

### 10.3 Synthesis Validation

After synthesis and implementation, the following are checked:

- No combinational loops reported by Vivado.
- Timing closure achieved at the target clock frequency (10 ns period).
- One-hot encoding inferred correctly for all state registers (confirmed via synthesis log).
- Resource utilisation (LUT and FF count) scales reasonably with the number of NFA states.

---

## 11. Known Design Constraints and Risks

### 11.1 Combinational ε-Closure Depth

Retaining ε-transitions as combinational logic introduces a chain of OR gates whose depth equals the maximum ε-path length in an NFA. For complex regexes with deeply nested alternations or quantifiers, this chain may become a timing critical path. This should be monitored during synthesis and, if necessary, addressed by inserting pipeline registers or by performing partial ε-closure before emission.

### 11.2 One-Hot State Register Size

One-hot encoding allocates one flip-flop per NFA state. Thompson's construction can produce a large number of states for complex regexes (on the order of two states per AST node). With fewer than 20 regexes and relatively simple patterns this is not expected to be problematic, but unusually complex regexes should be monitored for excessive state counts.

### 11.3 Dot Operator Fan-Out

The dot operator (`.`) matches any of the 256 possible byte values. Each dot transition in the NFA produces 256 parallel transition arcs in the Verilog next-state logic. Heavy use of dot in patterns will increase LUT usage accordingly.

### 11.4 Full Match Only

The system performs full-match checking only. Substring search is not supported. Any future extension to substring matching would require changes to how the `start` signal is managed (specifically, recycling the FSM from its initial state on every input character).

---

## 12. Out of Scope

The following are explicitly not part of this project:

- Character classes (`[a-z]`, `[^abc]`, etc.)
- Anchors (`^` and `$`)
- Backreferences or capture groups
- Unicode support (ASCII only)
- Intersection of regular languages
- Substring or multi-match modes
- AXI-Stream or other bus-protocol interfaces
- Formal verification
- A graphical front-end or interactive tooling
