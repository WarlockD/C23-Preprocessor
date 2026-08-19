# C23-Preprocessor

A fully-featured **C23 compatible preprocessor** written in **C++23**, built on top of the [cpp-peglib](https://github.com/yhirose/cpp-peglib) PEG (Parsing Expression Grammar) library.

## Features

| Feature | Details |
|---|---|
| **PEG-based lexer** | Phase 1–3 tokenisation (trigraph removal, line-splicing, PEG grammar) via cpp-peglib |
| **String pool** | All token spellings and filenames are interned in a thread-safe pool backed by a monotonic arena |
| **Full C23 macro system** | Object-like, function-like, variadic (`__VA_ARGS__`, `__VA_OPT__`), `#` stringify, `##` token-paste |
| **C23 conditionals** | `#if`, `#ifdef`, `#ifndef`, `#elif`, `#elifdef`, `#elifndef`, `#else`, `#endif` |
| **C23 builtins** | `__has_include`, `__has_c_attribute`, `__has_extension`, `__has_embed` |
| **Predefined macros** | `__STDC__`, `__STDC_VERSION__` (202311L), `__STDC_HOSTED__`, `__DATE__`, `__TIME__` |
| **Directives** | `#include` / `#include_next`, `#define`, `#undef`, `#line`, `#error`, `#warning`, `#pragma` |
| **Expansion log** | Optionally write all macro expansions to a separate file (`--expand-log`) |
| **Memory modes** | Eager (all files into RAM) or lazy (on-demand) via `--lazy` |
| **Parallel processing** | Multiple top-level files processed concurrently with `std::async` via `--parallel` |
| **stdin / stdout** | Pass `-` or no file to read from stdin; `-o file` to write to a file |

## Building

```sh
mkdir build && cd build
cmake .. -DCMAKE_BUILD_TYPE=Release
make -j$(nproc)
```

Requires a **C++23-capable compiler** (GCC 13+ or Clang 17+) and CMake 3.20+.

## Usage

```
c23pp [options] [file...]

Options:
  -I<dir>              Add user include path
  -isystem <dir>       Add system include path
  -D<name>[=<value>]   Define macro
  -U<name>             Undefine macro
  -o <file>            Output file (default: stdout)
  --expand-log <file>  Macro-expansion log output file
  --no-line-marks      Suppress #line markers
  --keep-comments      Keep comment tokens
  --lazy               Lazy file loading
  --parallel           Parallel processing of multiple inputs
  --std=<ver>          Target standard (default: c23)
  -                    Read from stdin
```

### Examples

```sh
# Preprocess a file
c23pp -I/usr/include -DDEBUG input.c -o output.i

# Pipe from stdin
echo '#define SQ(x) ((x)*(x))
int r = SQ(7);' | c23pp --no-line-marks -

# Log all macro expansions to a file
c23pp --expand-log macros.log input.c -o output.i

# Process multiple files in parallel
c23pp --parallel file1.c file2.c file3.c -o combined.i
```

## Architecture

```
Source File
    │
    ▼  Phase 1 — trigraph removal
    │  Phase 2 — line-splicing (backslash-newline)
    │
    ▼  [Lexer]  cpp-peglib PEG grammar → preprocessing tokens
    │           TokenKind: Identifier, PPNumber, StringLiteral,
    │           CharConst, Punctuator, Whitespace, Newline, …
    │
    ▼  [Preprocessor]
       ├─ Directive processing (#include, #define, #if, …)
       ├─ Macro expansion (object-like, function-like, variadic)
       ├─ #if expression evaluator (recursive-descent)
       └─ SourceManager (file registry, search-path resolution)
```

## Tests

```sh
cd build
./c23pp_tests
# or via CTest:
ctest
```

## Third-party

| Library | Licence | Purpose |
|---|---|---|
| [cpp-peglib](https://github.com/yhirose/cpp-peglib) v1.17 | MIT | PEG parser used as the C23 tokenisation engine |
