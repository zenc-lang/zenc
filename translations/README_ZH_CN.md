<div align="center">
  <p>
    <a href="../README.md">English</a> •
    <a href="README_DE.md">Deutsch</a> •
    <a href="README_RU.md">Русский</a> •
    <a href="README_ZH_CN.md">简体中文</a> •
    <a href="README_ZH_TW.md">繁體中文</a> •
    <a href="README_ES.md">Español</a> •
    <a href="README_IT.md">Italiano</a> •
    <a href="README_PT_BR.md">Português Brasileiro</a>
  </p>
</div>

<div align="center">
  <h1>Zen C</h1>
  <h3>现代开发体验。零开销。纯净 C。</h3>
  <br>
  <p>
    <a href="#"><img src="https://img.shields.io/badge/build-passing-brightgreen" alt="构建状态"></a>
    <a href="#"><img src="https://img.shields.io/badge/license-MIT-blue" alt="许可证"></a>
    <a href="#"><img src="https://img.shields.io/github/v/release/zenc-lang/zenc?label=version&color=orange" alt="版本"></a>
    <a href="#"><img src="https://img.shields.io/badge/platform-linux%20%7C%20windows%20%7C%20macos-lightgrey" alt="平台"></a>
  </p>
  <p><em>像高级语言一样编写，像 C 一样运行。</em></p>
</div>

<hr>

<div align="center">
  <p>
    <b><a href="#概述">概述</a></b> •
    <b><a href="#社区">社区</a></b> •
    <b><a href="#快速入门">快速入门</a></b> •
    <b><a href="#生态系统">生态系统</a></b> •
    <b><a href="#语言参考">语言参考</a></b> •
    <b><a href="#标准库">标准库</a></b> •
    <b><a href="#工具链">工具链</a></b>
  </p>
</div>

---

## 概述

**Zen C** 是一种现代系统编程语言，可编译为人类可读的 `GNU C`/`C11`。它提供了一套丰富的特性，包括类型推断、模式匹配、泛型、Trait、async/await 以及具有 RAII 能力的手动内存管理，同时保持 100% 的 C ABI 兼容性。

## 社区

加入官方 Zen C Discord 服务器，参与讨论、展示 Demo、提问或报告 Bug！

- Discord: [点击加入](https://discord.com/invite/q6wEsCmkJP)
- RFC: [功能提案](https://github.com/zenc-lang/rfcs)

## 生态系统

Zen C 项目包含多个仓库。下面是主要的仓库列表：

| 仓库 | 描述 | 状态 |
| :--- | :--- | :--- |
| **[zenc](https://github.com/zenc-lang/zenc)** | Zen C 核心编译器 (`zc`)、CLI 和标准库。 | 活跃开发 |
| **[docs](https://github.com/zenc-lang/docs)** | 官方技术文档与语言规范。 | 活跃 |
| **[rfcs](https://github.com/zenc-lang/rfcs)** | 征求意见稿 (RFC) 仓库。塑造语言的未来。 | 活跃 |
| **[vscode-zenc](https://github.com/zenc-lang/vscode-zenc)** | 官方 VS Code 扩展（语法高亮、代码片段）。 | Alpha |
| **[www](https://github.com/zenc-lang/www)** | `zenc-lang.org` 的源代码。 | 活跃 |
| **[awesome-zenc](https://github.com/zenc-lang/awesome-zenc)** | 精选的 Zen C 示例列表。 | 不断增加 |
| **[zenc.vim](https://github.com/zenc-lang/zenc.vim)** | 官方 Vim/Neovim 插件（语法高亮、智能缩进）。 | 活跃 |

## 展示

查看这些使用 Zen C 构建的项目：

- **[ZC-pong-3ds](https://github.com/5quirre1/ZC-pong-3ds)**: Nintendo 3DS 上的 Pong 克隆版。
- **[zen-c-parin](https://github.com/Kapendev/zen-c-parin)**: 使用 Parin 的 Zen C 基础示例。
- **[almond](https://git.sr.ht/~leanghok/almond)**: 用 Zen C 编写的极简网页浏览器。

---

## 目录

<table align="center">
  <tr>
    <th width="50%">通用</th>
    <th width="50%">语言参考</th>
  </tr>
  <tr>
    <td valign="top">
      <ul>
        <li><a href="#概述">概述</a></li>
        <li><a href="#社区">社区</a></li>
        <li><a href="#生态系统">生态系统</a></li>
        <li><a href="https://github.com/zenc-lang/rfcs">RFC</a></li>
        <li><a href="#快速入门">快速入门</a></li>
        <li><a href="https://github.com/zenc-lang/docs">文档</a></li>
        <li><a href="#标准库">标准库</a></li>
        <li><a href="#工具链">工具链</a>
          <ul>
            <li><a href="#语言服务器协议-lsp">LSP</a></li>
            <li><a href="#zen-c-调试">调试</a></li>
          </ul>
        </li>
        <li><a href="#编译器支持与兼容性">编译器支持与兼容性</a></li>
        <li><a href="#贡献">贡献</a></li>
        <li><a href="#致谢与归属">致谢与归属</a></li>
      </ul>
    </td>
    <td valign="top">
      <p><a href="https://docs.zenc-lang.org/tour/"><b>Browse the Language Reference</b></a></p>
    </td>
  </tr>
</table>

---

## 快速入门

### 安装

```bash
git clone https://github.com/zenc-lang/zenc.git
cd zenc
make clean # 移除旧的构建文件
make
sudo make install

#### Development Targets

```bash
make format       # Auto-format all source files with clang-format
make format-check # Verify formatting without changing files
make lint         # Run format-check + shellcheck on test scripts
make bench        # Run performance benchmarks
make WERROR=1     # Build with -Werror (warnings as errors)
```

```

### Unit Testing Framework

Zen C features a built-in testing framework with **per-test isolation**, **named output**, and **non-fatal assertions**.

For full details, see the [English README](../README.md#unit-testing-framework).


### Windows

Zen C 对 Windows (x86_64) 提供完备的原生支持。你可以使用提供的批处理脚本配合 GCC (MinGW) 进行构建：

```cmd
build.bat
```

这将构建编译器 (`zc.exe`)。网络、文件系统和进程操作通过平台抽象层 (PAL) 得到完全支持。

或者，如果你有类 Unix 环境（MSYS2、Cygwin、git-bash），也可以使用 `make`。

### 便携式构建 (APE)

Zen C 可以通过 [Cosmopolitan Libc](https://github.com/jart/cosmopolitan) 编译为 **Actually Portable Executable (APE)**。这将生成一个单个的可执行文件 (`.com`)，能够原生运行在 Linux, macOS, Windows, FreeBSD, OpenBSD, 和 NetBSD 上的 x86_64 和 aarch64 架构上。

**前提条件：**
- `cosmocc` 工具链（必须在 PATH 中）

**构建与安装：**
```bash
make ape
sudo env "PATH=$PATH" make install-ape
```

**产物：**
- `out/bin/zc.com`: 便携式 Zen-C 编译器。已将标准库嵌入到可执行文件中。
- `out/bin/zc-boot.com`: 一个自包含的引导安装程序，用于设置新的 Zen-C 项目。

**用法：**
```bash
# 在任何支持的操作系统上运行
./out/bin/zc.com build hello.zc -o hello
```

### 模块化构建

Zen C 分为可选模块。使用 `ZC_*` 标志在构建时选择功能：

| 标志 | 默认值 | 排除项 |
|---|---|---|
| `ZC_LSP=0` | 1 | LSP 服务器 (~8 个文件) |
| `ZC_REPL=0` | 1 | 交互式 REPL (~6 个文件) |
| `ZC_PLUGINS=0` | 1 | 插件系统 |
| `ZC_ZEN=0` | 1 | `--doc` / `--facts` 模式 |
| `ZC_BACKENDS=0` | 1 | 非 C 后端 (JSON, Lisp 等) |
| `ZC_TRE=0` | 1 | TRE 正则表达式库 |

```bash
make                     # 所有功能 (3.3 MB)
make lite                # 无 LSP、REPL、Zen (2.9 MB)
make core                # 仅编译器 (2.7 MB)
make minimal             # 最小构建 (2.6 MB)

# 自定义选择：
make ZC_LSP=0 ZC_REPL=0  # 排除 LSP 和 REPL
```

禁用的命令会显示清晰的提示信息而不会崩溃：`zc-lsp` → "LSP support not included"。

### 用法

```bash
# 编译并运行
zc run hello.zc

# 构建可执行文件
zc build hello.zc -o hello

# 交互式 Shell
zc-repl

# 文档 (递归)
zc-doc main.zc

# 文档 (单文件, 无检查)
zc-doc --no-recursive-doc main.zc

```

### 环境变量

你可以设置 `ZC_ROOT` 来指定标准库的位置（标准导入如 `import "std/vector.zc"`）。这允许你从任何目录运行 `zc`。

```bash
export ZC_ROOT=/path/to/zenc
```

---

## 语言参考

有关更多详细信息，请参阅官方[语言参考](https://docs.zenc-lang.org/tour/01-variables-constants/)。

## 标准库

Zen C 包含一个涵盖基本功能的标准库 (`std`)。

[浏览标准库文档](../docs/std/README.md)

### 核心模块

<details>
<summary>点击查看所有标准库模块</summary>

| 模块 | 描述 | 文档 |
| :--- | :--- | :--- |
| **`std/bigfloat.zc`** | 任意精度浮点运算。 | [文档](../docs/std/bigfloat.md) |
| **`std/bigint.zc`** | 任意精度整数 `BigInt`。 | [文档](../docs/std/bigint.md) |
| **`std/bits.zc`** | 底层位运算操作 (`rotl`, `rotr` 等)。 | [文档](../docs/std/bits.md) |
| **`std/complex.zc`** | 复数算术 `Complex`。 | [文档](../docs/std/complex.md) |
| **`std/vec.zc`** | 可增长动态数组 `Vec<T>`。 | [文档](../docs/std/vec.md) |
| **`std/string.zc`** | 堆分配的 `String` 类型，支持 UTF-8。 | [文档](../docs/std/string.md) |
| **`std/queue.zc`** | 先进先出队列 (环形缓冲区)。 | [文档](../docs/std/queue.md) |
| **`std/map.zc`** | 泛型哈希表 `Map<V>`。 | [文档](../docs/std/map.md) |
| **`std/fs.zc`** | 文件系统操作。 | [文档](../docs/std/fs.md) |
| **`std/io.zc`** | 标准输入/输出 (`print`/`println`)。 | [文档](../docs/std/io.md) |
| **`std/option.zc`** | 可选值 (`Some`/`None`)。 | [文档](../docs/std/option.md) |
| **`std/result.zc`** | 错误处理 (`Ok`/`Err`)。 | [文档](../docs/std/result.md) |
| **`std/path.zc`** | 跨平台路径操作。 | [文档](../docs/std/path.md) |
| **`std/env.zc`** | 进程环境变量。 | [文档](../docs/std/env.md) |
| **`std/net/`** | TCP, UDP, HTTP, DNS, URL. | [文档](../docs/std/net.md) |
| **`std/thread.zc`** | 线程与同步。 | [文档](../docs/std/thread.md) |
| **`std/time.zc`** | 时间测量与睡眠。 | [文档](../docs/std/time.md) |
| **`std/json.zc`** | JSON 解析与序列化。 | [文档](../docs/std/json.md) |
| **`std/stack.zc`** | 后进先出栈 `Stack<T>`。 | [文档](../docs/std/stack.md) |
| **`std/set.zc`** | 泛型哈希集合 `Set<T>`。 | [文档](../docs/std/set.md) |
| **`std/process.zc`** | 进程执行与管理。 | [文档](../docs/std/process.md) |
| **`std/regex.zc`** | 正则表达式 (基于 TRE)。 | [文档](../docs/std/regex.md) |
| **`std/simd.zc`** | 原生 SIMD 向量类型。 | [文档](../docs/std/simd.md) |

</details>

---

## 工具链

Zen C 提供内置的语言服务器 (LSP) 和 REPL 以增强开发体验。

### 语言服务器 (LSP)

Zen C 语言服务器 (LSP) 支持标准的 LSP 特性，用于编辑器集成：

*   **转到定义**
*   **查找引用**
*   **悬停信息**
*   **补全** (函数/结构体名，方法/字段的点补全)
*   **文档符号** (大纲)
*   **签名帮助**
*   **诊断** (语法/语义错误)

启动语言服务器（通常在编辑器的 LSP 设置中配置）：

```bash
zc-lsp
```

它通过标准 I/O (JSON-RPC 2.0) 进行通信。

### REPL

Read-Eval-Print Loop (REPL) 允许您使用现代的**进程内 JIT 编译**（由 LibTCC 提供支持）交互式地尝试 Zen C 代码。

```bash
zc-repl
```

#### 特性

*   **JIT 执行**：代码在内存中编译并直接在 REPL 进程中执行，以实现极速的反馈。

*   **交互式编码**：输入表达式或语句以立即求值。
*   **持久历史**：命令保存在 `~/.zprep_history` 中。
*   **启动脚本**：自动加载 `~/.zprep_init.zc` 中的命令。

#### 命令

| 命令 | 描述 |
|:---|:---|
| `:help` | 显示可用命令。 |
| `:reset` | 清除当前会话历史 (变量/函数)。 |
| `:vars` | 显示活跃变量。 |
| `:funcs` | 显示用户定义的函数。 |
| `:structs` | 显示用户定义的结构体。 |
| `:imports` | 显示活跃导入。 |
| `:history` | 显示会话输入历史。 |
| `:type <expr>` | 显示表达式的类型。 |
| `:c <stmt>` | 显示语句生成的 C 代码。 |
| `:time <expr>` | 基准测试表达式 (运行 1000 次迭代)。 |
| `:edit [n]` | 在 `$EDITOR` 中编辑命令 `n` (默认：最后一条)。 |
| `:save <file>` | 将当前会话保存到 `.zc` 文件。 |
| `:load <file>` | 将 `.zc` 文件加载并执行到会话中。 |
| `:watch <expr>` | 监视表达式 (每次输入后重新求值)。 |
| `:unwatch <n>` | 移除监视。 |
| `:undo` | 从会话中移除最后一条命令。 |
| `:delete <n>` | 移除索引为 `n` 的命令。 |
| `:clear` | 清屏。 |
| `:quit` | 退出 REPL。 |
| `! <cmd>` | 运行 shell 命令 (如 `!ls`)。 |

---


### 语言服务器协议 (LSP)

Zen C 包含一个内置的语言服务器，用于编辑器集成。

- **[安装与设置指南](translations/LSP_ZH_CN.md)**
- **支持的编辑器**: VS Code, Neovim, Vim, Zed, 以及任何支持 LSP 的编辑器。

使用 `zc-lsp` 启动服务器。

### Zen C 调试

Zen C 程序可以使用标准的 C 调试器（如 **LLDB** 或 **GDB**）进行调试。

#### Visual Studio Code

为了在 VS Code 中获得最佳体验，请安装官方的 [Zen C 扩展](https://marketplace.visualstudio.com/items?itemName=Z-libs.zenc)。对于调试，您可以使用 **C/C++**（由 Microsoft 提供）或 **CodeLLDB** 扩展。

将这些配置添加到您的 `.vscode` 目录中，以启用一键调试：

**`tasks.json`** (构建任务):
```json
{
    "label": "Zen C: Build Debug",
    "type": "shell",
    "command": "zc",
    "args": [ "${file}", "-g", "-o", "${fileDirname}/app", "-O0" ],
    "group": { "kind": "build", "isDefault": true }
}
```

**`launch.json`** (调试器):
```json
{
    "name": "Zen C: Debug (LLDB)",
    "type": "lldb",
    "request": "launch",
    "program": "${fileDirname}/app",
    "preLaunchTask": "Zen C: Build Debug"
}
```

## 编译器支持与兼容性

Zen C 旨在与大多数 C11 编译器配合使用。某些特性依赖于 GNU C 扩展，但这些扩展通常在其他编译器中也能工作。使用 `--cc` 标志切换后端。

```bash
zc run app.zc --cc clang
zc run app.zc --cc zig
```

### 测试套件状态

<details>
<summary>点击查看编译器支持详情</summary>

| 编译器 | 通过率 | 受支持特性 | 已知局限性 |
|:---|:---:|:---|:---|
| **GCC** | **100% (全面)** | 所有特性 | 无. |
| **Clang** | **100% (全面)** | 所有特性 | 无. |
| **Zig** | **100% (全面)** | 所有特性 | 无. 使用 `zig cc` 作为替代 C 编译器. |
| **TCC** | **98% (高)** | 结构体, 泛型, Trait, 模式匹配 | 不支持 Intel ASM, 不支持 `__attribute__((constructor))`. |

</details>

> [!WARNING]
> **编译器构建警告：** 虽然 **Zig CC** 作为 Zen C 程序的后端非常出色，但使用它构建 *Zen C 编译器本身*可能会通过验证，但会生成无法通过测试的不稳定二进制文件。我们建议使用 **GCC** 或 **Clang** 构建编译器，并仅将 Zig 用作操作代码的后端。

> [!TIP]
> 
### MISRA C:2012 合规性测试

Zen C 测试套件包含针对 MISRA C:2012 指南的验证。

> [!IMPORTANT]
> **MISRA 免责声明**
> 本项目完全独立，与 MISRA (Motor Industry Software Reliability Association) 没有任何关联、官方认可或商业合作关系。 
> 
> 由于严格的版权限制，测试用例仅通过数字标识符列出指南，避免发布内部具体规范。需要原始文档的用户，请前往 [MISRA 官方门户网站](https://www.misra.org.uk/) 获取真实可靠的指南材料。

### 使用 Zig 构建


Zig 的 `zig cc` 命令提供了 GCC/Clang 的替代方案，具有出色的跨平台编译支持。使用 Zig：

```bash
# 使用 Zig 编译并运行 Zen C 程序
zc run app.zc --cc zig

# 使用 Zig 构建 Zen C 编译器本身
make zig
```

### 输出后端

Zen C 通过 `--backend` 标志支持多种输出后端。每个后端生成不同的目标格式：

| 后端 | 标志 | 扩展名 | 描述 |
|:---|:---|:---:|:---|
| **C** | `--backend c` | `.c` | 默认 — GNU C11 |
| **C++** | `--backend cpp` | `.cpp` | 兼容 C++11（也可用 `--cpp`） |
| **CUDA** | `--backend cuda` | `.cu` | NVIDIA CUDA C++（也可用 `--cuda`） |
| **Objective-C** | `--backend objc` | `.m` | Objective-C（也可用 `--objc`） |
| **JSON** | `--backend json` | `.json` | 机器可读的 AST，用于工具 |
| **AST 转储** | `--backend ast-dump` | `.ast` | 人类可读的 AST 树（调试） |
| **Lisp** | `--backend lisp` | `.lisp` | 转译为 Common Lisp（`sbcl --script`） |
| **Graphviz** | `--backend dot` | `.dot` | 可视化 AST 图（`dot -Tpng ast.dot -o ast.png`） |

后端特定选项可以通过 `--backend-opt` 设置：

```bash
# 美化输出 JSON
zc transpile file.zc --backend json --backend-opt pretty

# 显示完整原始内容（不截断）
zc transpile file.zc --backend lisp --backend-opt full-content

# 或使用便捷别名：
zc transpile file.zc --backend json --json-pretty
zc transpile file.zc --backend lisp --backend-full-content
```

所有后端选项都是自文档的 — 未知的 `--` 标志会自动对照已注册的后端别名进行检查。

### C++ 互操作

Zen C 可以通过 `--backend cpp` 标志（`--cpp` 简称）生成 C++ 兼容的代码，从而实现与 C++ 库的无缝集成。

```bash
# 直接使用 g++ 编译
zc app.zc --backend cpp

# 或者转译用于手动构建
zc transpile app.zc --backend cpp
g++ out.cpp my_cpp_lib.o -o app
```

#### 在 Zen C 中使用 C++

包含 C++ 头文件并在 `raw` 块中使用 C++ 代码：

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
> --cpp 标志会将后端切换为 `g++` 并发出 C++ 兼容的代码（使用 `auto` 代替 `__auto_type`，使用函数重载代替 `_Generic`，以及对 `void*` 进行显式转换）。

#### CUDA 互操作

Zen C 通过转译为 **CUDA C++** 来支持 GPU 编程，使用 `--backend cuda` 标志（简称 `--cuda`）。这使你在维持 Zen C 人体工程学语法的同时，能够利用内核中的强大 C++ 特性（模板、constexpr）。

```bash
# 直接使用 nvcc 编译
zc run app.zc --backend cuda

# 或者转译用于手动构建
zc transpile app.zc --backend cuda -o app.cu
nvcc app.cu -o app
```

#### CUDA 特定属性

| 属性 | CUDA 等效项 | 描述 |
|:---|:---|:---|
| `@global` | `__global__` | 内核函数 (运行在 GPU，从主机调用) |
| `@device` | `__device__` | 设备函数 (运行在 GPU，从 GPU 调用) |
| `@host` | `__host__` | 主机函数 (明确仅 CPU 运行) |

#### 内核启动语法

Zen C 提供了一个简洁的 `launch` 语句用于调用 CUDA 内核：

```zc
launch kernel_name(args) with {
    grid: num_blocks,
    block: threads_per_block,
    shared_mem: 1024,  // 可选
    stream: my_stream   // 可选
};
```

这转译为：`kernel_name<<<grid, block, shared, stream>>>(args);`

#### 编写 CUDA 内核

使用带有 `@global` 的 Zen C 函数语法和 `launch` 语句：

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

    // ... 初始化数据 ...
    
    launch add_kernel(d_a, d_b, d_c, N) with {
        grid: (N + 255) / 256,
        block: 256
    };
    
    cuda_sync();
}
```

#### 标准库 (`std/cuda.zc`)
Zen C 为常见的 CUDA 操作提供了一个标准库，以减少 `raw` 块的使用：

```zc
import "std/cuda.zc"

// 内存管理
let d_ptr = cuda_alloc<float>(1024);
cuda_copy_to_device(d_ptr, h_ptr, 1024 * sizeof(float));
defer cuda_free(d_ptr);

// 同步
cuda_sync();

// 线程索引 (在内核内部使用)
let i = thread_id(); // 全局索引
let bid = block_id();
let tid = local_id();
```


> [!NOTE]
> **注意：** `--cuda` 标志设置 `nvcc` 为编译器并隐含 `--cpp` 模式。需要安装 NVIDIA CUDA Toolkit。

### C23 支持

当使用兼容的后端编译器（GCC 14+, Clang 14+）时，Zen C 支持现代 C23特性。

- **`auto`**: 如果 `__STDC_VERSION__ >= 202300L`，Zen C 会自动将类型推导映射到标准 C23 `auto`。
- **`_BitInt(N)`**: 使用 `iN` 和 `uN` 类型（例如 `i256`, `u12`, `i24`）访问 C23 任意位宽整数。

### Objective-C 互操作

Zen C 可以通过 `--backend objc` 标志（简称 `--objc`）编译为 Objective-C (`.m`)，允许你使用 Objective-C 框架（如 Cocoa/Foundation）和语法。

```bash
# 使用 clang 编译（或 gcc/gnustep）
zc app.zc --backend objc --cc clang
```

#### 在 Zen C 中使用 Objective-C

使用 `include` 包含头文件，并在 `raw` 块中使用 Objective-C 语法 (`@interface`, `[...]`, `@""`)。

```zc
//> macos: framework: Foundation
//> linux: cflags: -fconstant-string-class=NSConstantString -D_NATIVE_OBJC_EXCEPTIONS
//> linux: link: -lgnustep-base -lobjc

include <Foundation/Foundation.h>

fn main() {
    raw {
        NSAutoreleasePool *pool = [[NSAutoreleasePool alloc] init];
        NSLog(@"来自 Objective-C 的问候！");
        [pool drain];
    }
    println "Zen C 也能正常工作！";
}
```

> [!NOTE]
> **注意：** Zen C 字符串插值通过调用 `debugDescription` 或 `description` 同样适用于 Objective-C 对象 (`id`)。

### 18. 单元测试框架

Zen C 内置了测试框架，允许你使用 `test` 关键字直接在源文件中编写单元测试。

#### 语法
`test` 块包含一个描述性名称和要执行的代码体。测试不需要 `main` 函数即可运行。

```zc
test "unittest1" {
    "这是一个单元测试";

    let a = 3;
    assert(a > 0, "a 应该是一个正整数");

    "unittest1 通过。";
}
```

#### 运行测试
要运行文件中的所有测试，请使用 `run` 命令。编译器将自动检测并执行所有顶层 `test` 块。

```bash
zc run my_file.zc
```

#### 断言
使用内置的 `assert(condition, message)` 函数来验证预期。如果条件为假，测试将失败并打印提供的消息。

---

### 公共 API（嵌入）

Zen C 可以通过 `src/public/*.h` 中的公共头文件作为 C 库使用。这些头文件无需 `-DZC_ALLOW_INTERNAL` 即可编译，并提供了将编译器嵌入到您自己的工具中的稳定 API：

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

**编译方式：**

```bash
cc -I src/public -I src -I src/utils my_tool.c -o my_tool
```

**安装后（`make install`）：**

```bash
cc -I /usr/local/include/zenc my_tool.c -o my_tool
```

公共 API 涵盖：
- **`zc_core.h`** — `CompilerConfig`、`ZenCompiler`、`ASTNode`、`Type` 类型，解析器入口点，类型内省辅助函数
- **`zc_driver.h`** — `driver_run()`、`driver_compile()`（完整流水线编排）
- **`zc_codegen.h`** — `codegen_node()`、`emit_preamble()`、`format_expression_as_c()`
- **`zc_analysis.h`** — `check_program()`、`check_moves_only()`、`resolve_alias()`
- **`zc_diag.h`** — `zerror_at()`、`zwarn_at()`、`zpanic_at()`，诊断报告
- **`zc_utils.h`** — `Emitter`（输出缓冲区）、`load_file()`、`z_resolve_path()`

使用 `sudo make install` 安装以部署头文件、二进制文件、手册页和标准库。

---

## 贡献

我们欢迎各类贡献！无论是修复 Bug、完善文档，还是提出新功能建议。

请参阅 [CONTRIBUTING_ZH_CN.md](CONTRIBUTING_ZH_CN.md) 了解有关如何贡献、运行测试和提交拉取请求的详细指南。

---

## 安全

关于安全漏洞报告的说明，请参阅 [SECURITY_ZH_CN.md](SECURITY_ZH_CN.md)。

---

## 致谢与归属

本项目使用了第三方库。完整许可证文本可在 `LICENSES/` 目录中找到。

*   **[cJSON](https://github.com/DaveGamble/cJSON)** (MIT 许可证)：用于语言服务器中的 JSON 解析和生成。
*   **[zc-ape](https://github.com/OEvgeny/zc-ape)** (MIT 许可证)：由 [Eugene Olonov](https://github.com/OEvgeny) 开发的原版 Zen-C 实际上便携的可执行文件 (APE) 端口。
*   **[Cosmopolitan Libc](https://github.com/jart/cosmopolitan)** (ISC 许可证)：使 APE 成为可能的基础库。
*   **[TRE](https://github.com/laurikari/tre)** (BSD 许可证): 用于标准库中的正则表达式引擎。
*   **[zenc.vim](https://github.com/zenc-lang/zenc.vim)** (MIT 许可证)：官方 Vim/Neovim 插件，主要由 **[davidscholberg](https://github.com/davidscholberg)** 编写。
*   **[TinyCC](https://github.com/TinyCC/tinycc)** (LGPL 许可证)：用于高性能 REPL 评估的基础 JIT 引擎。

---

<div align="center">
  <p>
    Copyright © 2026 Zen C 编程语言。<br>
    今天就开始你的旅程。
  </p>
  <p>
    <a href="https://discord.com/invite/q6wEsCmkJP">Discord</a> •
    <a href="https://github.com/zenc-lang/zenc">GitHub</a> •
    <a href="https://github.com/zenc-lang/docs">文档</a> •
    <a href="https://github.com/zenc-lang/awesome-zenc">示例</a> •
    <a href="https://github.com/zenc-lang/rfcs">RFC</a> •
    <a href="CONTRIBUTING_ZH_CN.md">贡献</a>
  </p>
</div>
