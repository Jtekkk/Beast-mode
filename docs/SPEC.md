# Beast Language Specification (v0.1)

Beast is a small, dynamically-typed, garbage-collected scripting language.
This document defines its syntax and semantics precisely enough to write
programs and to write an alternative implementation. Where the reference
implementation (`src/`) and this document disagree, that is a bug in one of
them — please report it.

- File extension: `.bst`
- Interpreter: `beast`
- Comment character: `#`
- Encoding: UTF-8 source; strings are byte sequences.

---

## 1. Lexical structure

### 1.1 Comments

`#` begins a comment that runs to the end of the line. There are no block
comments. Because `#` is the comment character, a `#!` shebang line works:

```beast
#!/usr/bin/env beast
```

### 1.2 Statement termination (the TERM rule)

Beast has no mandatory semicolons. A statement ends at a newline, but *only*
when the newline follows a token that can legally end a statement. Concretely,
a newline is a statement terminator (`TERM`) when **both** hold:

1. It is not inside an open `(` or `[` grouping (bracket depth is zero), and
2. The previous significant token is one of:
   an identifier, number, or string literal; `)`, `]`, or `}`;
   or one of the keywords `true`, `false`, `nil`, `return`, `break`,
   `continue`.

Otherwise the newline is ordinary whitespace. Consequences:

- A line ending in a binary operator, comma, `.`, or an open bracket
  continues onto the next line:
  ```beast
  let x = 1 +
      2 +
      3
  ```
- A `}` also ends the current statement, so `if x { break }` works inline.
- `;` is an explicit terminator anywhere, allowing one-liners:
  ```beast
  let a = 1; let b = 2; print(a + b)
  ```
- A bare `return` on its own line returns `nil` (there is no expression after
  it on the line, and the newline terminates the statement).
- `else` must appear on the same line as the `}` that closes the `if` body
  (one-true-brace style); otherwise the newline terminates the `if` and the
  `else` is an error.

### 1.3 Keywords

Active (16): `and break continue else false fn for if in let nil not or
return true while`.

Reserved — using one is a compile error (`"reserved for a future version"`):
`class const import match try`.

### 1.4 Literals

- **Numbers**: decimal, optional fraction and exponent: `42`, `3.14`, `1e3`,
  `6.02e23`. All numbers are IEEE-754 doubles. `.` is only a decimal point
  when followed by a digit, so `0..5` lexes as `0 .. 5`, not `0. .5`.
- **Strings**: double-quoted. Escapes: `\n \t \r \" \\`. Any other escape is
  a compile error. A string may not span a raw newline.
- **Booleans**: `true`, `false`.
- **Nil**: `nil`.

### 1.5 Operators and punctuation

```
+  -  *  /  %        arithmetic
== != < <= > >=      comparison
=  += -= *= /=       assignment (statements only)
and or not           logical (words, not symbols)
.  ..  ,  :          member, range, separator, map colon
(  )  [  ]  {  }     grouping, indexing, blocks/maps
```

There is no `!`, `&&`, `||`, and no bitwise family. Negation is `not`.

---

## 2. Values and types

Seven first-class value types:

| Type    | Notes                                                        |
|---------|-------------------------------------------------------------|
| `nil`   | The absence of a value.                                      |
| `bool`  | `true` / `false`.                                            |
| `num`   | IEEE-754 double. Exact integers up to 2^53.                  |
| `str`   | Immutable, interned byte sequence.                           |
| `array` | Growable, 0-indexed sequence.                               |
| `map`   | Hash map with string or number keys.                         |
| `fn`    | Function, closure, or native — all callable, all first-class.|

`typeof(v)` returns one of `"nil" "bool" "num" "str" "array" "map" "fn"`.

### 2.1 Truthiness

Only `nil` and `false` are falsey. Everything else — including `0`, `""`,
`[]`, and `{}` — is truthy.

### 2.2 Equality

`==` never coerces. Values of different types are never equal (`1 == "1"` is
`false`). Numbers and strings compare by value; strings are interned so this
is a pointer comparison. Arrays, maps, and functions compare by identity.

### 2.3 Numbers

There is one numeric type. Integer-valued numbers print without a decimal
point (`4`, not `4.0`). Division is always floating-point (`10 / 4` is `2.5`);
use `floor()` for integer division. `%` is `fmod`. Exactness is guaranteed for
integers with magnitude up to 2^53.

---

## 3. Expressions

### 3.1 Precedence

From lowest to highest:

```
or
and
not            (prefix)
== !=
< <= > >=
+ -
* / %
- (unary)
. () []         (call, index, member)
literals, grouping
```

`and`/`or` short-circuit and evaluate to the *deciding operand*, not a
coerced bool: `nil or 5` is `5`, `1 and 2` is `2`. This gives a ternary
idiom: `cond and a or b`.

### 3.2 Arithmetic and concatenation

`+` adds two numbers or concatenates two strings. Mixing a string and a
non-string is a runtime error (use `str()`). `- * / %` require two numbers.

### 3.3 Indexing

- `array[i]`: `i` must be an integer in `0 <= i < len`. Out of bounds is a
  runtime error (never silent `nil`). Negative indices are rejected.
- `string[i]`: same rules; returns a 1-character string (byte-indexed).
- `map[k]`: `k` is a string or number. A missing key reads as `nil`.
- `map.field` is sugar for `map["field"]` (a constant string key).

### 3.4 Literals

- Array: `[e1, e2, ...]`, trailing comma allowed.
- Map: `{ key: value, ... }`, where a key is a bare identifier (string sugar),
  a string literal, or a number literal. Trailing comma and interior newlines
  allowed. A `{` in statement position is always a block; map literals are
  expression-only.

### 3.5 Calls

`f(a, b, c)`. Arity is checked exactly at the call site; too few or too many
arguments is a runtime error. Maximum 255 arguments. No varargs, defaults, or
keyword arguments.

---

## 4. Statements

### 4.1 Declarations

```beast
let name = expr          # required initializer
fn name(params) { ... }  # function declaration
```

`let` introduces a variable in the current scope; a global may not be
redeclared, and a local may not shadow another local in the same block (it may
shadow an outer one). At the top level, functions and globals bind *late*:
a function may refer to any global or function declared anywhere in the file,
enabling mutual recursion and define-in-any-order. A global that is referenced
but never declared is a **compile error** with a did-you-mean suggestion.

### 4.2 Assignment

Assignment is a statement, never an expression. `if x = y { }` is a compile
error (hint: did you mean `==`). Three target forms:

```beast
name = expr
container[index] = expr
container.field = expr
```

Compound forms `+= -= *= /=` work on all three targets and evaluate the target
expression once.

### 4.3 Control flow

```beast
if cond { ... } else if cond { ... } else { ... }
while cond { ... }
for name in start..limit { ... }
break
continue
return              # returns nil
return expr
```

Conditions take no parentheses; braces are mandatory. `for` iterates a
half-open integer range `[start, limit)` with step 1; there is no inclusive
or stepped form, and no iteration over arrays/maps (use
`for i in 0..len(a)` and `keys(m)`). The loop variable is a **fresh binding
each iteration**, so closures created in the body capture distinct values.
`break` and `continue` act on the innermost enclosing loop.

### 4.4 Blocks and scope

`{ ... }` in statement position is a block with its own scope. Locals are
block-scoped and may shadow outer bindings.

---

## 5. Functions and closures

Functions are values. A named declaration and an anonymous `fn(params){...}`
expression share the same semantics. Functions close over their lexical
environment by **reference**: a captured variable is shared, and mutations are
visible through the closure.

```beast
fn make_counter(start) {
    let n = start
    return fn(step) { n += step; return n }
}
```

Captured variables live as long as any closure referencing them. Note that a
variable declared *outside* a `while` loop and captured inside it is shared
across iterations (a common closure gotcha); a `for` loop variable is not.

---

## 6. Built-in functions

17 natives, all ordinary global functions (no methods):

| Function              | Description                                              |
|-----------------------|----------------------------------------------------------|
| `print(v)`            | Print `v` and a newline. Returns `nil`.                  |
| `clock()`             | Monotonic seconds as a number (for timing).              |
| `len(x)`              | Length of a string, array, or map.                       |
| `push(a, v)`          | Append `v` to array `a`. Returns `a` (chainable).        |
| `pop(a)`              | Remove and return the last element of array `a`.         |
| `get(c, k [, d])`     | `c[k]` for a map/array, or default `d` (else `nil`).     |
| `del(m, k)`           | Delete key `k` from map `m`. Returns whether it existed. |
| `keys(m)`             | Array of `m`'s keys (order unspecified).                 |
| `str(v)`              | Convert any value to its display string.                 |
| `num(s)`              | Parse a number from string `s`; `nil` if not numeric.    |
| `chr(n)`              | 1-character string from byte value `n` (0–255).          |
| `ord(s)`              | Byte value of a 1-character string.                      |
| `floor(n)`            | Largest integer `<= n`.                                  |
| `sqrt(n)`             | Square root.                                             |
| `typeof(v)`           | Type name string.                                        |
| `assert(c [, msg])`   | Runtime error if `c` is falsey.                          |
| `readline()`          | Read one line from stdin (no newline); `nil` at EOF.     |

---

## 7. Errors and exit codes

- **Compile errors** report `file:line:column`, the source line, a caret, and
  often a fix hint. The interpreter exits with status **65** and runs nothing.
- **Runtime errors** print a message and a stack trace (function names and
  line numbers), then exit with status **70**.

There is no in-language exception handling in v0.1. Recoverable failure uses
return-value conventions (`num("x")` returns `nil`) together with `assert`.

---

## 8. Grammar (EBNF sketch)

`TERM` is a statement terminator per §1.2. Newlines that are not `TERM` are
insignificant.

```ebnf
program    = { TERM } { declaration } ;
declaration= funcDecl | letDecl | statement ;
funcDecl   = "fn" IDENT function ;
function   = "(" [ params ] ")" block ;
params     = IDENT { "," IDENT } [ "," ] ;
letDecl    = "let" IDENT "=" expression TERM ;

statement  = ifStmt | whileStmt | forStmt | returnStmt
           | "break" TERM | "continue" TERM | block | exprStmt ;
ifStmt     = "if" expression block [ "else" ( ifStmt | block ) ] ;
whileStmt  = "while" expression block ;
forStmt    = "for" IDENT "in" expression ".." expression block ;
returnStmt = "return" [ expression ] TERM ;
block      = "{" { TERM } { declaration } "}" ;
exprStmt   = assignment TERM ;

assignment = ( call "." IDENT | call "[" expression "]" | IDENT )
             ( "=" | "+=" | "-=" | "*=" | "/=" ) expression
           | logic_or ;
logic_or   = logic_and { "or" logic_and } ;
logic_and  = equality { "and" equality } ;
equality   = comparison { ( "==" | "!=" ) comparison } ;
comparison = term { ( "<" | "<=" | ">" | ">=" ) term } ;
term       = factor { ( "+" | "-" ) factor } ;
factor     = unary { ( "*" | "/" | "%" ) unary } ;
unary      = ( "-" | "not" ) unary | call ;
call       = primary { "(" [ args ] ")" | "[" expression "]" | "." IDENT } ;
primary    = NUMBER | STRING | "true" | "false" | "nil" | IDENT
           | "(" expression ")" | arrayLit | mapLit | "fn" function ;
arrayLit   = "[" [ expression { "," expression } [ "," ] ] "]" ;
mapLit     = "{" [ entry { "," entry } [ "," ] ] "}" ;
entry      = ( IDENT | STRING | NUMBER ) ":" expression ;
```
