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

## Running the Builder
The tool accepts a regex input file as a command-line argument. It can be compiled and run using the provided `Makefile`:

```bash
# Compile and run with the default 'regexes.txt'
make run

# Or manually
g++ -o regex_builder src/main.cpp src/lexer.cpp src/parser.cpp -Isrc
./regex_builder your_input_file.txt
```

This will output a visual representation of the AST for each valid regular expression in the input file.
