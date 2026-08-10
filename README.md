<div align="center">
  <p>
    <a href="README.md">English</a> •
    <a href="translations/README_DE.md">Deutsch</a> •
    <a href="translations/README_RU.md">Русский</a> •
    <a href="translations/README_ZH_CN.md">简体中文</a> •
    <a href="translations/README_ZH_TW.md">繁體中文</a> •
    <a href="translations/README_ES.md">Español</a> •
    <a href="translations/README_IT.md">Italiano</a> •
    <a href="translations/README_PT_BR.md">Português Brasileiro</a>
  </p>
</div>

<div align="center">
  <h1>Zen C</h1>
  <h3>Modern Ergonomics. Zero Overhead. Pure C.</h3>
  <br>
  <p>
    <a href="#"><img src="https://img.shields.io/badge/build-passing-brightgreen" alt="Build Status"></a>
    <a href="#"><img src="https://img.shields.io/badge/license-MIT-blue" alt="License"></a>
    <a href="#"><img src="https://img.shields.io/github/v/release/zenc-lang/zenc?label=version&color=orange" alt="Version"></a>
    <a href="#"><img src="https://img.shields.io/badge/platform-linux%20%7C%20windows%20%7C%20macos-lightgrey" alt="Platform"></a>
  </p>
  <p><em>Write like a high-level language, run like C.</em></p>
</div>

<hr>

<div align="center">
  <p>
    <b><a href="#overview">Overview</a></b> •
    <b><a href="#community">Community</a></b> •
    <b><a href="#quick-start">Quick Start</a></b> •
    <b><a href="#ecosystem">Ecosystem</a></b> •
    <b><a href="#language-reference">Language Reference</a></b> •
    <b><a href="#standard-library">Standard Library</a></b> •
    <b><a href="#tooling">Toolchain</a></b>
  </p>
</div>

---

## Overview

**Zen C** is a modern systems programming language that compiles to human-readable `GNU C`/`C11`. It provides a rich feature set including type inference, pattern matching, generics, traits, async/await, and manual memory management with RAII capabilities, all while maintaining 100% C ABI compatibility.

## Community

Join the discussion, share demos, ask questions, or report bugs in the official Zen C Discord server!

- Discord: [Join here](https://discord.com/invite/q6wEsCmkJP)
- RFCs: [Propose features](https://github.com/zenc-lang/rfcs)

## Ecosystem

The Zen C project consists of several repositories. Below you can find the primary ones:

| Repository | Description | Status |
| :--- | :--- | :--- |
| **[zenc](https://github.com/zenc-lang/zenc)** | The core Zen C compiler (`zc`), CLI, and Standard Library. | Active Development |
| **[docs](https://github.com/zenc-lang/docs)** | The official documentation and language specification. | Active |
| **[rfcs](https://github.com/zenc-lang/rfcs)** | The Request for Comments (RFC) repository. Shape the future of the language. | Active |
| **[vscode-zenc](https://github.com/zenc-lang/vscode-zenc)** | Official VS Code extension (Syntax Highlighting, Snippets). | Alpha |
| **[www](https://github.com/zenc-lang/www)** | Source code for `zenc-lang.org`. | Active |
| **[awesome-zenc](https://github.com/zenc-lang/awesome-zenc)** | A curated list of awesome Zen C examples | Growing |
| **[zenc.vim](https://github.com/zenc-lang/zenc.vim)** | Official Vim/Neovim plugin (Syntax, Indentation). | Active |

## Showcase

Check out these projects built with Zen C:

- **[ZC-pong-3ds](https://github.com/5quirre1/ZC-pong-3ds)**: A Pong clone for the Nintendo 3DS.
- **[zen-c-parin](https://github.com/Kapendev/zen-c-parin)**: A basic example using Zen C with Parin.
- **[almond](https://git.sr.ht/~leanghok/almond)**: A minimal web browser written in Zen C.

---

## Index

<table align="center">
  <tr>
    <th width="50%">General</th>
    <th width="50%">Language Reference</th>
  </tr>
  <tr>
    <td valign="top">
      <ul>
        <li><a href="#overview">Overview</a></li>
        <li><a href="#community">Community</a></li>
        <li><a href="https://github.com/zenc-lang/rfcs">RFCs</a></li>
        <li><a href="#quick-start">Quick Start</a></li>
        <li><a href="#ecosystem">Ecosystem</a></li>
        <li><a href="https://github.com/zenc-lang/docs">Documentation</a></li>
        <li><a href="#standard-library">Standard Library</a></li>
        <li><a href="#tooling">Tooling</a>
          <ul>
            <li><a href="#language-server-protocol-lsp">LSP</a></li>
            <li><a href="#debugging-zen-c">Debugging</a></li>
          </ul>
        </li>
        <li><a href="#compiler-support--compatibility">Compiler Support & Compatibility</a></li>
        <li><a href="#contributing">Contributing</a></li>
        <li><a href="#attributions">Attributions</a></li>
      </ul>
    </td>
    <td valign="top">
      <p><a href="https://docs.zenc-lang.org/tour/"><b>Browse the Language Reference</b></a></p>
    </td>
  </tr>
</table>

---

## Quick Start

### Installation

```bash
git clone https://github.com/zenc-lang/zenc.git
cd zenc
make clean # remove old build files
make
sudo make install
```

#### Development Targets

```bash
make format       # Auto-format all source files with clang-format
make format-check # Verify formatting without changing files
make lint         # Run format-check + shellcheck on test scripts
make bench        # Run performance benchmarks
make WERROR=1     # Build with -Werror (warnings as errors)
```

### Windows

Zen C has full native support for Windows (x86_64). You can build using the provided batch script with GCC (MinGW):

```cmd
build.bat
```

This will build the compiler (`zc.exe`). Networking, Filesystem, and Process operations are fully supported via the Platform Abstraction Layer (PAL).

Alternatively, you can use `make` if you have a Unix-like environment (MSYS2, Cygwin, git-bash).

### Portable Build (APE)

Zen C can be compiled as an **Actually Portable Executable (APE)** using [Cosmopolitan Libc](https://github.com/jart/cosmopolitan). This produces a single binary (`.com`) that runs natively on Linux, macOS, Windows, FreeBSD, OpenBSD, and NetBSD on both x86_64 and aarch64 architectures.

**Prerequisites:**
- `cosmocc` toolchain (must be in your PATH)

**Build & Install:**
```bash
make ape
sudo env "PATH=$PATH" make install-ape
```

**Artifacts:**
- `out/bin/zc.com`: The portable Zen-C compiler. Includes the standard library embedded within the executable.
- `out/bin/zc-boot.com`: A self-contained bootstrap installer for setting up new Zen-C projects.

**Usage:**
```bash
# Run on any supported OS
./out/bin/zc.com build hello.zc -o hello
```

### Usage

```bash
# Compile and run
zc run hello.zc

# Build executable
zc build hello.zc -o hello

# Interactive Shell
zc-repl

# Documentation (Recursive)
zc-doc main.zc

# Documentation (Single file, no check)
zc-doc --no-recursive-doc main.zc

```

### Environment Variables

You can set `ZC_ROOT` to specify the location of the Standard Library (standard imports like `import "std/vec.zc"`). This allows you to run `zc` from any directory.

```bash
export ZC_ROOT=/path/to/Zen-C
```

---

## Language Reference

See the official [Language Reference](https://docs.zenc-lang.org/tour/01-variables-constants/) for more details.

## Standard Library

Zen C includes a standard library (`std`) covering essential functionality.

[Browse the Standard Library Documentation](docs/std/README.md)

### Key Modules

<details>
<summary>Click to see all Standard Library modules</summary>

| Module | Description | Docs |
| :--- | :--- | :--- |
| **`std/bigfloat.zc`** | Arbitrary-precision floating-point arithmetic. | [Docs](docs/std/bigfloat.md) |
| **`std/bigint.zc`** | Arbitrary-precision integer `BigInt`. | [Docs](docs/std/bigint.md) |
| **`std/bits.zc`** | Low-level bitwise operations (`rotl`, `rotr`). | [Docs](docs/std/bits.md) |
| **`std/complex.zc`** | Complex Number Arithmetic `Complex`. | [Docs](docs/std/complex.md) |
| **`std/vec.zc`** | Growable dynamic array `Vec<T>`. | [Docs](docs/std/vec.md) |
| **`std/string.zc`** | Heap-allocated `String` type with UTF-8 support. | [Docs](docs/std/string.md) |
| **`std/queue.zc`** | FIFO queue (Ring Buffer). | [Docs](docs/std/queue.md) |
| **`std/map.zc`** | Generic Hash Map `Map<V>`. | [Docs](docs/std/map.md) |
| **`std/fs.zc`** | File system operations. | [Docs](docs/std/fs.md) |
| **`std/io.zc`** | Standard Input/Output (`print`/`println`). | [Docs](docs/std/io.md) |
| **`std/option.zc`** | Optional values (`Some`/`None`). | [Docs](docs/std/option.md) |
| **`std/result.zc`** | Error handling (`Ok`/`Err`). | [Docs](docs/std/result.md) |
| **`std/path.zc`** | Cross-platform path manipulation. | [Docs](docs/std/path.md) |
| **`std/env.zc`** | Process environment variables. | [Docs](docs/std/env.md) |
| **`std/net/`** | TCP, UDP, HTTP, DNS, URL. | [Docs](docs/std/net.md) |
| **`std/thread.zc`** | Threads and Synchronization. | [Docs](docs/std/thread.md) |
| **`std/time.zc`** | Time measurement and sleep. | [Docs](docs/std/time.md) |
| **`std/json.zc`** | JSON parsing and serialization. | [Docs](docs/std/json.md) |
| **`std/stack.zc`** | LIFO Stack `Stack<T>`. | [Docs](docs/std/stack.md) |
| **`std/set.zc`** | Generic Hash Set `Set<T>`. | [Docs](docs/std/set.md) |
| **`std/process.zc`** | Process execution and management. | [Docs](docs/std/process.md) |
| **`std/regex.zc`** | Regular Expressions (TRE based). | [Docs](docs/std/regex.md) |
| **`std/simd.zc`** | Native SIMD vector types. | [Docs](docs/std/simd.md) |

</details>

### 18. Unit Testing Framework

Zen C features a built-in testing framework with **per-test isolation**, **named output**, and **non-fatal assertions**.

#### Syntax
A `test` block contains a descriptive name and a body of code to execute. Tests do not require a `main` function to run.

```zc
test "descriptive name" {
    let a = 3;
    assert(a > 0, "a should be positive");
}
```

#### Running Tests
```bash
zc run my_file.zc
```

Output shows each test by name:
```
  TEST: descriptive name ... OK
  TEST: another test ... FAIL

1 test(s) failed
```

#### Assertions
| Function | Behavior |
|:---|:---|
| `assert(cond, msg)` | Records failure, continues to next test (no longer aborts) |
| `expect(cond, msg)` | Non-fatal — records failure but continues within the same test |

Use `assert` for critical checks that should stop the current test, and `expect` when you want to verify multiple conditions without short-circuiting:

```zc
test "example" {
    expect(result != null, "result should not be null");
    expect(result.code == 200, "status should be 200");
    // both run even if the first fails
}
```

#### Exit Code
The binary exits with the number of failed tests (0 = all passed).

---

## Tooling

Zen C provides a built-in Language Server and REPL to enhance the development experience. It is also debuggable with LLDB.

### Language Server (LSP)

The Zen C Language Server (LSP) supports standard LSP features for editor integration, providing:

*   **Go to Definition**
*   **Find References**
*   **Hover Information**
*   **Completion** (Function/Struct names, Dot-completion for methods/fields)
*   **Document Symbols** (Outline)
*   **Signature Help**
*   **Diagnostics** (Syntax/Semantic errors)

To start the language server (typically configured in your editor's LSP settings):

```bash
zc-lsp
```

It communicates via standard I/O (JSON-RPC 2.0).

### REPL

The Read-Eval-Print Loop allows you to experiment with Zen C code interactively using modern **In-Process JIT Compilation** (powered by LibTCC).

```bash
zc-repl
```

#### Features

*   **JIT Execution**: Code is compiled in-memory and executed directly within the REPL process for lightning-fast feedback.

*   **Interactive Coding**: Type expressions or statements for immediate evaluation.
*   **Persistent History**: Commands are saved to `~/.zenc_history`.
*   **Startup Script**: Auto-loads commands from `~/.zencrc`.

#### Commands

| Command | Description |
|:---|:---|
| `:help` | Show available commands. |
| `:reset` | Clear current session history (variables/functions). |
| `:vars` | Show active variables. |
| `:funcs` | Show user-defined functions. |
| `:structs` | Show user-defined structs. |
| `:imports` | Show active imports. |
| `:history` | Show session input history. |
| `:type <expr>` | Show the type of an expression. |
| `:c <stmt>` | Show the generated C code for a statement. |
| `:time <expr>` | Benchmark an expression (runs 1000 iterations). |
| `:edit [n]` | Edit command `n` (default: last) in `$EDITOR`. |
| `:save <file>` | Save the current session to a `.zc` file. |
| `:load <file>` | Load and execute a `.zc` file into the session. |
| `:watch <expr>` | Watch an expression (re-evaluated after every entry). |
| `:unwatch <n>` | Remove a watch. |
| `:undo` | Remove the last command from the session. |
| `:delete <n>` | Remove command at index `n`. |
| `:clear` | Clear the screen. |
| `:quit` | Exit the REPL. |
| `! <cmd>` | Run a shell command (e.g. `!ls`). |

---


### Language Server Protocol (LSP)

Zen C includes a built-in Language Server for editor integration.

- **[Installation & Setup Guide](docs/LSP.md)**
- **Supported Editors**: VS Code, Neovim, Vim ([zenc.vim](https://github.com/zenc-lang/zenc.vim)), Zed, and any LSP-capable editor.

Use `zc-lsp` to start the server.

### Debugging Zen C

Zen C programs can be debugged using standard C debuggers like **LLDB** or **GDB**.

#### Visual Studio Code

For the best experience in VS Code, install the official [Zen C extension](https://marketplace.visualstudio.com/items?itemName=Z-libs.zenc). For debugging, you can use the **C/C++** (by Microsoft) or **CodeLLDB** extension.

Add these configurations to your `.vscode` directory to enable one-click debugging:

**`tasks.json`** (Build Task):
```json
{
    "label": "Zen C: Build Debug",
    "type": "shell",
    "command": "zc",
    "args": [ "${file}", "-g", "-o", "${fileDirname}/app", "-O0" ],
    "group": { "kind": "build", "isDefault": true }
}
```

**`launch.json`** (Debugger):
```json
{
    "name": "Zen C: Debug (LLDB)",
    "type": "lldb",
    "request": "launch",
    "program": "${fileDirname}/app",
    "preLaunchTask": "Zen C: Build Debug"
}
```

## Compiler Support & Compatibility

Zen C is designed to work with most C11 compilers. Some features rely on GNU C extensions, but these often work in other compilers. Use the `--cc` flag to switch backends.

```bash
zc run app.zc --cc clang
zc run app.zc --cc zig
```

### Test Suite Status

<details>
<summary>Click to view Compiler Support details</summary>

| Compiler | Pass Rate | Supported Features | Known Limitations |
|:---|:---:|:---|:---|
| **GCC** | **100% (Full)** | All Features | None. |
| **Clang** | **100% (Full)** | All Features | None. |
| **Zig** | **100% (Full)** | All Features | None. Uses `zig cc` as a drop-in C compiler. |
| **TCC** | **98% (High)** | Structs, Generics, Traits, Pattern Matching | No Intel ASM, No `__attribute__((constructor))`. |

</details>

> [!WARNING]
> **COMPILER BUILD WARNING:** While **Zig CC** works excellently as a backend for your Zen C programs, building the *Zen C compiler itself* with it may verify but produce an unstable binary that fails tests. We recommend building the compiler with **GCC** or **Clang** and using Zig only as a backend for your operational code.

### MISRA C:2012 Compliance Testing

The Zen C test suite includes verification against MISRA C:2012 guidelines. 

> [!IMPORTANT]
> **MISRA Disclaimer**
> This project is completely independent and holds no affiliation, official endorsement, or corporate connection with MISRA (Motor Industry Software Reliability Association). 
> 
> Due to strict copyright restrictions, test cases only list directives by their numeric identifiers and avoid publishing internal specifications. Users needing primary documentation are encouraged to acquire authentic guideline materials from the [Official MISRA portal](https://www.misra.org.uk/).

### Building with Zig

Zig's `zig cc` command provides a drop-in replacement for GCC/Clang with excellent cross-compilation support. To use Zig:

```bash
# Compile and run a Zen C program with Zig
zc run app.zc --cc zig

# Build the Zen C compiler itself with Zig
make zig
```

### Output Backends

Zen C supports multiple output backends via the `--backend` flag. Each backend produces a different target format:

| Backend | Flag | Extension | Description |
|:---|:---|:---:|:---|
| **C** | `--backend c` | `.c` | Default — GNU C11 |
| **C++** | `--backend cpp` | `.cpp` | C++11 compatible (also available as `--cpp`) |
| **CUDA** | `--backend cuda` | `.cu` | NVIDIA CUDA C++ (also available as `--cuda`) |
| **Objective-C** | `--backend objc` | `.m` | Objective-C (also available as `--objc`) |
| **JSON** | `--backend json` | `.json` | Machine-readable AST for tooling |
| **AST dump** | `--backend ast-dump` | `.ast` | Human-readable AST tree (debugging) |
| **Lisp** | `--backend lisp` | `.lisp` | Transpile to Common Lisp (`sbcl --script`) |
| **Graphviz** | `--backend dot` | `.dot` | Visual AST graph (`dot -Tpng ast.dot -o ast.png`) |

Backend-specific options can be set with `--backend-opt`:

```bash
# Pretty-print JSON output
zc transpile file.zc --backend json --backend-opt pretty

# Show full raw content (no truncation)
zc transpile file.zc --backend lisp --backend-opt full-content

# OR use convenience aliases:
zc transpile file.zc --backend json --json-pretty
zc transpile file.zc --backend lisp --backend-full-content
```

All backend options are self-documented — unknown `--` flags are checked against registered backend aliases automatically.

### C++ Interop

Zen C can generate C++-compatible code with the `--backend cpp` flag (`--cpp` for short), allowing seamless integration with C++ libraries.

```bash
# Direct compilation with g++
zc app.zc --backend cpp

# Or transpile for manual build
zc transpile app.zc --backend cpp
g++ out.cpp my_cpp_lib.o -o app
```

#### Using C++ in Zen C

Include C++ headers and use raw blocks for C++ code:

```zc
include <vector>
include <iostream>

raw {
    std::vector<int> make_vec(int a, int b) {
        return {a, b};
    }
}

fn main() {
    let v = make_vec(1, 2);
    raw { std::cout << "Size: " << v.size() << std::endl; }
}
```

> [!NOTE]
> The `--cpp` flag switches the backend to `g++` and emits C++-compatible code (uses `auto` instead of `__auto_type`, function overloads instead of `_Generic`, and explicit casts for `void*`).

#### CUDA Interop

Zen C supports GPU programming by transpiling to **CUDA C++** via the `--backend cuda` flag (`--cuda` for short). This allows you to leverage powerful C++ features (templates, constexpr) within your kernels while maintaining Zen C's ergonomic syntax.

```bash
# Direct compilation with nvcc
zc run app.zc --backend cuda

# Or transpile for manual build
zc transpile app.zc --backend cuda -o app.cu
nvcc app.cu -o app
```

#### CUDA-Specific Attributes

| Attribute | CUDA Equivalent | Description |
|:---|:---|:---|
| `@global` | `__global__` | Kernel function (runs on GPU, called from host) |
| `@device` | `__device__` | Device function (runs on GPU, called from GPU) |
| `@host` | `__host__` | Host function (explicit CPU-only) |

#### Kernel Launch Syntax

Zen C provides a clean `launch` statement for invoking CUDA kernels:

```zc
launch kernel_name(args) with {
    grid: num_blocks,
    block: threads_per_block,
    shared_mem: 1024,  // Optional
    stream: my_stream   // Optional
};
```

This transpiles to: `kernel_name<<<grid, block, shared, stream>>>(args);`

#### Writing CUDA Kernels

Use Zen C function syntax with `@global` and the `launch` statement:

```zc
import "std/cuda.zc"

@global
fn add_kernel(a: float*, b: float*, c: float*, n: int) {
    let i = thread_id();
    if i < n {
        c[i] = a[i] + b[i];
    }
}

fn main() {
    def N = 1024;
    let d_a = cuda_alloc<float>(N);
    let d_b = cuda_alloc<float>(N); 
    let d_c = cuda_alloc<float>(N);
    defer cuda_free(d_a);
    defer cuda_free(d_b);
    defer cuda_free(d_c);

    // ... init data ...
    
    launch add_kernel(d_a, d_b, d_c, N) with {
        grid: (N + 255) / 256,
        block: 256
    };
    
    cuda_sync();
}
```

#### Standard Library (`std/cuda.zc`)
Zen C provides a standard library for common CUDA operations to reduce `raw` blocks:

```zc
import "std/cuda.zc"

// Memory management
let d_ptr = cuda_alloc<float>(1024);
cuda_copy_to_device(d_ptr, h_ptr, 1024 * sizeof(float));
defer cuda_free(d_ptr);

// Synchronization
cuda_sync();

// Thread Indexing (use inside kernels)
let i = thread_id(); // Global index
let bid = block_id();
let tid = local_id();
```


> [!NOTE]
> **Note:** The `--cuda` flag sets `nvcc` as the compiler and implies `--cpp` mode. Requires the NVIDIA CUDA Toolkit.

### C23 Support

Zen C supports modern C23 features when using a compatible backend compiler (GCC 14+, Clang 14+, TCC (partial)).

- **`auto`**: Zen C automatically maps type inference to standard C23 `auto` if `__STDC_VERSION__ >= 202300L`.
- **`_BitInt(N)`**: Use `iN` and `uN` types (e.g., `i256`, `u12`, `i24`) to access C23 arbitrary-width integers.

### Objective-C Interop

Zen C can compile to Objective-C (`.m`) using the `--backend objc` flag (`--objc` for short), allowing you to use Objective-C frameworks (like Cocoa/Foundation) and syntax.

```bash
# Compile with clang (or gcc/gnustep)
zc app.zc --backend objc --cc clang
```

#### Using Objective-C in Zen C

Use `include` for headers and `raw` blocks for Objective-C syntax (`@interface`, `[...]`, `@""`).

```zc
//> macos: framework: Foundation
//> linux: cflags: -fconstant-string-class=NSConstantString -D_NATIVE_OBJC_EXCEPTIONS
//> linux: link: -lgnustep-base -lobjc

include <Foundation/Foundation.h>

fn main() {
    raw {
        NSAutoreleasePool *pool = [[NSAutoreleasePool alloc] init];
        NSLog(@"Hello from Objective-C!");
        [pool drain];
    }
    println "Zen C works too!";
}
```

> [!NOTE]
> **Note:** Zen C string interpolation works with Objective-C objects (`id`) by calling `debugDescription` or `description`.

---

### Public API (Embedding)

Zen C can be used as a C library via the public headers in `src/public/*.h`. These headers compile without `-DZC_ALLOW_INTERNAL` and provide a stable API for embedding the compiler in your own tools:

```c
#include <zc_core.h>
#include <zc_driver.h>
#include <zc_diag.h>

int main(void) {
    ZenCompiler compiler = {0};
    compiler.config.input_file = "source.zc";
    return driver_run(&compiler);
}
```

**Compile with:**

```bash
cc -I src/public -I src -I src/utils my_tool.c -o my_tool
```

**After install (`make install`):**

```bash
cc -I /usr/local/include/zenc my_tool.c -o my_tool
```

The public API covers:
- **`zc_core.h`** — `CompilerConfig`, `ZenCompiler`, `ASTNode`, `Type` types, parser entry points, type introspection helpers
- **`zc_driver.h`** — `driver_run()`, `driver_compile()` (full pipeline orchestration)
- **`zc_codegen.h`** — `codegen_node()`, `emit_preamble()`, `format_expression_as_c()`
- **`zc_analysis.h`** — `check_program()`, `check_moves_only()`, `resolve_alias()`
- **`zc_diag.h`** — `zerror_at()`, `zwarn_at()`, `zpanic_at()`, diagnostic reporting
- **`zc_utils.h`** — `Emitter` (output buffer), `load_file()`, `z_resolve_path()`

Install with `sudo make install` to deploy headers, the binary, man pages, and standard library.

---

## Contributing
 
 We welcome contributions! Whether it's fixing bugs, adding documentation, or proposing new features.
 
 Please see [CONTRIBUTING.md](CONTRIBUTING.md) for detailed guidelines on how to contribute, run tests, and submit pull requests.

---
 
 ## Security
 
 For security reporting instructions, please see [SECURITY.md](SECURITY.md).
 
 ---
 
 ## Attributions

This project uses third-party libraries. Full license texts can be found in the `LICENSES/` directory.

*   **[cJSON](https://github.com/DaveGamble/cJSON)** (MIT License): Used for JSON parsing and generation in the Language Server.
*   **[zc-ape](https://github.com/OEvgeny/zc-ape)** (MIT License): The original Actually Portable Executable port of Zen-C by [Eugene Olonov](https://github.com/OEvgeny).
*   **[Cosmopolitan Libc](https://github.com/jart/cosmopolitan)** (ISC License): The foundational library that makes APE possible.
*   **[TRE](https://github.com/laurikari/tre)** (BSD License): Used for the regular expression engine in the standard library.
*   **[zenc.vim](https://github.com/zenc-lang/zenc.vim)** (MIT License): The official Vim/Neovim plugin, primarily authored by **[davidscholberg](https://github.com/davidscholberg)**.
*   **[TinyCC](https://github.com/TinyCC/tinycc)** (LGPL License): The foundational JIT engine used for the high-performance REPL evaluation.

---

<div align="center">
  <p>
    Copyright © 2026 Zen C Programming Language.<br>
    Start your journey today.
  </p>
  <p>
    <a href="https://discord.com/invite/q6wEsCmkJP">Discord</a> •
    <a href="https://github.com/zenc-lang/zenc">GitHub</a> •
    <a href="https://github.com/zenc-lang/docs">Documentation</a> •
    <a href="https://github.com/zenc-lang/awesome-zenc">Examples</a> •
    <a href="https://github.com/zenc-lang/rfcs">RFCs</a> •
    <a href="CONTRIBUTING.md">Contribute</a>
  </p>
</div>
