# Regex → NFA → Verilog Hardware Matcher

## Project Specification

---

## 1. Project Overview

This project implements a hardware-accelerated regular expression matching pipeline. A set of regular expressions defined in plain text is automatically compiled into parallel hardware finite state machines (FSMs), expressed in Verilog, and synthesised onto an FPGA. At runtime, a stream of ASCII characters is fed into the hardware one character per clock cycle, and the system reports which regular expressions are matched by the full input string.

The pipeline consists of four major stages: regex parsing, NFA construction, Verilog code generation, and hardware simulation and synthesis.

---

## 2. Finalised Design Decisions

| Decision                      | Choice                                                              |
| ----------------------------- | ------------------------------------------------------------------- |
| Supported operators           | Kleene star, plus, optional, union, dot, parentheses, literals      |
| NFA construction algorithm    | Glushkov's construction                                             |
| Architecture                  | One independent Verilog FSM module per NFA, all running in parallel |
| ε-transition handling         | None — Glushkov's construction produces ε-free NFAs directly        |
| State encoding                | One-hot (one flip-flop per NFA state)                               |
| Character stream interface    | Synchronous; one 8-bit ASCII character per clock cycle              |
| Match semantics               | Full match only (the entire input string must satisfy the regex)    |
| Scale                         | Fewer than 20 regular expressions                                   |
| Simulation and synthesis tool | Xilinx Vivado                                                       |

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

## 5. Stage 2 — C++ NFA Construction (Glushkov's Construction)

### 5.1 Overview

Glushkov's construction is applied to each AST to produce an ε-free NFA. Unlike Thompson's construction, Glushkov's algorithm never introduces ε-transitions. Instead, it works by analysing the positions of symbol occurrences within the regex and computing which positions can follow which others during a match. The resulting NFA has exactly one state per symbol occurrence in the regex, plus one distinguished initial state, and all transitions are labelled with concrete characters.

This ε-free property means the Verilog emitter requires no combinational ε-closure layer — the next-state logic is entirely registered, which simplifies synthesis and removes combinational depth as a timing concern.

### 5.2 Step 1 — Linearisation

Each symbol occurrence in the regex (every literal character and every dot) is assigned a unique integer position label, numbered from 1 upwards, in left-to-right order of appearance. Position 0 is reserved for the special initial state and does not correspond to any symbol. Two occurrences of the same character are treated as distinct positions.

For example, the regex `ab*a` is linearised as `a₁ b₂* a₃`, yielding positions 1, 2, and 3.

### 5.3 Step 2 — Computing Nullable

For each node in the AST, a Boolean property `nullable` is computed, indicating whether the sub-expression rooted at that node can match the empty string. The rules are:

- A Literal or Dot node is never nullable.
- A Star or Optional node is always nullable.
- A Plus node is nullable if and only if its inner sub-expression is nullable.
- A Union node is nullable if either branch is nullable.
- A Concatenation node is nullable if and only if both sub-expressions are nullable.

### 5.4 Step 3 — Computing Firstpos

For each AST node, `firstpos` is the set of positions that can match the first character of any string accepted by that sub-expression. The rules are:

- A Literal or Dot node at position p has `firstpos = {p}`.
- A Star, Plus, or Optional node has the same `firstpos` as its inner sub-expression.
- A Union node has `firstpos` equal to the union of the `firstpos` sets of both branches.
- A Concatenation node of left and right sub-expressions has `firstpos` equal to the `firstpos` of the left sub-expression, plus the `firstpos` of the right sub-expression if the left sub-expression is nullable.

### 5.5 Step 4 — Computing Lastpos

For each AST node, `lastpos` is the set of positions that can match the last character of any string accepted by that sub-expression. The rules are:

- A Literal or Dot node at position p has `lastpos = {p}`.
- A Star, Plus, or Optional node has the same `lastpos` as its inner sub-expression.
- A Union node has `lastpos` equal to the union of the `lastpos` sets of both branches.
- A Concatenation node of left and right sub-expressions has `lastpos` equal to the `lastpos` of the right sub-expression, plus the `lastpos` of the left sub-expression if the right sub-expression is nullable.

### 5.6 Step 5 — Computing Followpos

For each position p, `followpos(p)` is the set of positions that can immediately follow position p in any match. This set is built by traversing the AST and applying two rules:

- **Concatenation rule:** for every position p in `lastpos` of the left sub-expression of a Concatenation node, add all positions in `firstpos` of the right sub-expression to `followpos(p)`.
- **Star and Plus rule:** for every position p in `lastpos` of the inner sub-expression of a Star or Plus node, add all positions in `firstpos` of that same inner sub-expression to `followpos(p)`.

All other node types (Union, Optional, Literal, Dot) do not contribute to `followpos` directly.

### 5.7 Step 6 — Building the NFA

With the position functions computed, the NFA is assembled as follows:

- **States:** one state for each position 1 through n (where n is the total number of symbol occurrences), plus state 0 as the initial state.
- **Start state:** state 0.
- **Accept states:** all positions in `lastpos` of the root AST node. Additionally, state 0 is an accept state if the root node is nullable (i.e. the regex matches the empty string).
- **Transitions from state 0:** for each position p in `firstpos` of the root node, add a transition from state 0 on the symbol at position p to state p.
- **Transitions from state p (p > 0):** for each position q in `followpos(p)`, add a transition from state p on the symbol at position q to state q.
