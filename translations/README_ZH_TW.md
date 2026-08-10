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
  <h3>現代開發體驗。零開銷。純淨 C。</h3>
  <br>
  <p>
    <a href="#"><img src="https://img.shields.io/badge/build-passing-brightgreen" alt="構建狀態"></a>
    <a href="#"><img src="https://img.shields.io/badge/license-MIT-blue" alt="許可證"></a>
    <a href="#"><img src="https://img.shields.io/github/v/release/zenc-lang/zenc?label=version&color=orange" alt="版本"></a>
    <a href="#"><img src="https://img.shields.io/badge/platform-linux%20%7C%20windows%20%7C%20macos-lightgrey" alt="平台"></a>
  </p>
  <p><em>像高級語言一樣編寫，像 C 一樣運行。</em></p>
</div>

<hr>

<div align="center">
  <p>
    <b><a href="#概述">概述</a></b> •
    <b><a href="#社區">社區</a></b> •
    <b><a href="#快速入門">快速入門</a></b> •
    <b><a href="#生態系統">生態系統</a></b> •
    <b><a href="#語言參考">語言參考</a></b> •
    <b><a href="#標準庫">標準庫</a></b> •
    <b><a href="#工具鏈">工具鏈</a></b>
  </p>
</div>

---

## 概述

**Zen C** 是一種現代系統編程語言，可編譯為人類可讀的 `GNU C`/`C11`。它提供了一套豐富的特性，包括類型推斷、模式匹配、泛型、Trait、async/await 以及具有 RAII 能力的手動內存管理，同時保持 100% 的 C ABI 兼容性。

## 社區

加入官方 Zen C Discord 服務器，參與討論、展示 Demo、提問或報告 Bug！

- Discord: [點擊加入](https://discord.com/invite/q6wEsCmkJP)
- RFC: [功能提案](https://github.com/zenc-lang/rfcs)

## 生態系統

Zen C 項目包含多個倉庫。下面是主要的倉庫列表：

| 倉庫 | 描述 | 狀態 |
| :--- | :--- | :--- |
| **[zenc](https://github.com/zenc-lang/zenc)** | Zen C 核心編譯器 (`zc`)、CLI 和標準庫。 | 活躍開發 |
| **[docs](https://github.com/zenc-lang/docs)** | 官方技術文檔與語言規範。 | 活躍 |
| **[rfcs](https://github.com/zenc-lang/rfcs)** | 徵求意見稿 (RFC) 倉庫。塑造語言的未來。 | 活躍 |
| **[vscode-zenc](https://github.com/zenc-lang/vscode-zenc)** | 官方 VS Code 擴充功能（語法高亮、程式碼片段）。 | Alpha |
| **[www](https://github.com/zenc-lang/www)** | `zenc-lang.org` 的源代碼。 | 活躍 |
| **[awesome-zenc](https://github.com/zenc-lang/awesome-zenc)** | 精選的 Zen C 範例列表。 | 不斷增加 |

## 展示

查看這些使用 Zen C 構建的項目：

- **[ZC-pong-3ds](https://github.com/5quirre1/ZC-pong-3ds)**: Nintendo 3DS 上的 Pong 克隆版。
- **[zen-c-parin](https://github.com/Kapendev/zen-c-parin)**: 使用 Parin 的 Zen C 基礎範例。
- **[almond](https://git.sr.ht/~leanghok/almond)**: 用 Zen C 編寫的極簡網頁瀏覽器。

---

## 目錄

<table align="center">
  <tr>
    <th width="50%">通用</th>
    <th width="50%">語言參考</th>
  </tr>
  <tr>
    <td valign="top">
      <ul>
        <li><a href="#概述">概述</a></li>
        <li><a href="#社區">社區</a></li>
        <li><a href="#生態系統">生態系統</a></li>
        <li><a href="https://github.com/zenc-lang/rfcs">RFC</a></li>
        <li><a href="#快速入門">快速入門</a></li>
        <li><a href="https://github.com/zenc-lang/docs">文檔</a></li>
        <li><a href="#標準庫">標準庫</a></li>
        <li><a href="#工具鏈">工具鏈</a>
          <ul>
            <li><a href="#語言伺服器協定-lsp">LSP</a></li>
            <li><a href="#zen-c-調試">調試</a></li>
          </ul>
        </li>
        <li><a href="#編譯器支持與兼容性">編譯器支持與兼容性</a></li>
        <li><a href="#貢獻">貢獻</a></li>
        <li><a href="#致謝與歸屬">致謝與歸屬</a></li>
      </ul>
    </td>
    <td valign="top">
      <p><a href="https://docs.zenc-lang.org/tour/"><b>Browse the Language Reference</b></a></p>
    </td>
  </tr>
</table>

---

## 快速入門

### 安裝

```bash
git clone https://github.com/zenc-lang/zenc.git
cd zenc
make clean # 移除舊的構建文件
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

Zen C 對 Windows (x86_64) 提供完備的原生支援。你可以使用提供的批处理脚本配合 GCC (MinGW) 進行構建：

```cmd
build.bat
```

這將構建編譯器 (`zc.exe`)。網路、檔案系統和程序操作通過平台抽象層 (PAL) 得到完全支援。

或者，如果你有類 Unix 環境（MSYS2、Cygwin、git-bash），也可以使用 `make`。

### 便攜式構建 (APE)

Zen C 可以通過 [Cosmopolitan Libc](https://github.com/jart/cosmopolitan) 編譯為 **Actually Portable Executable (APE)**。這將生成一個單個的可執行文件 (`.com`)，能夠原生運行在 Linux, macOS, Windows, FreeBSD, OpenBSD, 和 NetBSD 上的 x86_64 和 aarch64 架構上。

**前提條件：**
- `cosmocc` 工具鏈（必須在 PATH 中）

**構建與安裝：**
```bash
make ape
sudo env "PATH=$PATH" make install-ape
```

**產物：**
- `out/bin/zc.com`: 便攜式 Zen-C 編譯器。已將標準庫嵌入到可執行文件中。
- `out/bin/zc-boot.com`: 一個自包含的引導安裝程序，用於設置新的 Zen-C 項目。

**用法：**
```bash
# 在任何支持的操作系統上運行
./out/bin/zc.com build hello.zc -o hello
```

### 模組化構建

Zen C 分為可選模組。使用 `ZC_*` 標誌在構建時選擇功能：

| 標誌 | 預設值 | 排除項 |
|---|---|---|
| `ZC_LSP=0` | 1 | LSP 伺服器 (~8 個檔案) |
| `ZC_REPL=0` | 1 | 互動式 REPL (~6 個檔案) |
| `ZC_PLUGINS=0` | 1 | 外掛系統 |
| `ZC_ZEN=0` | 1 | `--doc` / `--facts` 模式 |
| `ZC_BACKENDS=0` | 1 | 非 C 後端 (JSON, Lisp 等) |
| `ZC_TRE=0` | 1 | TRE 正則表達式庫 |

```bash
make                     # 所有功能 (3.3 MB)
make lite                # 無 LSP、REPL、Zen (2.9 MB)
make core                # 僅編譯器 (2.7 MB)
make minimal             # 最小構建 (2.6 MB)

# 自定義選擇：
make ZC_LSP=0 ZC_REPL=0  # 排除 LSP 和 REPL
```

禁用的命令會顯示清晰的提示訊息而不會崩潰：`zc-lsp` → "LSP support not included"。

### 用法

```bash
# 編譯並運行
zc run hello.zc

# 構建可執行文件
zc build hello.zc -o hello

# 交互式 Shell
zc-repl

# 文檔 (遞迴)
zc-doc main.zc

# 文檔 (單文件, 無檢查)
zc-doc --no-recursive-doc main.zc

```

### 環境變量

你可以設置 `ZC_ROOT` 來指定標準庫的位置（標準導入如 `import "std/vector.zc"`）。這允許你從任何目錄運行 `zc`。

```bash
export ZC_ROOT=/path/to/zenc
```

---

## 語言參考

有關更多詳細資訊，請參閲官方[語言參考](https://docs.zenc-lang.org/tour/01-variables-constants/)。

## 標準庫

Zen C 包含一個涵蓋基本功能的標準庫 (`std`)。

[瀏覽標準庫文檔](../docs/std/README.md)

### 核心模塊

<details>
<summary>點擊查看所有標準庫模塊</summary>

| 模塊 | 描述 | 文檔 |
| :--- | :--- | :--- |
| **`std/bigfloat.zc`** | 任意精度浮點運算。 | [文檔](../docs/std/bigfloat.md) |
| **`std/bigint.zc`** | 任意精度整數 `BigInt`。 | [文檔](../docs/std/bigint.md) |
| **`std/bits.zc`** | 底層位運算操作 (`rotl`, `rotr` 等)。 | [文檔](../docs/std/bits.md) |
| **`std/complex.zc`** | 複數算術 `Complex`。 | [文檔](../docs/std/complex.md) |
| **`std/vec.zc`** | 可增長動態數組 `Vec<T>`。 | [文檔](../docs/std/vec.md) |
| **`std/string.zc`** | 堆分配的 `String` 類型，支持 UTF-8。 | [文檔](../docs/std/string.md) |
| **`std/queue.zc`** | 先進先出隊列 (環形緩衝區)。 | [文檔](../docs/std/queue.md) |
| **`std/map.zc`** | 泛型哈希表 `Map<V>`。 | [文檔](../docs/std/map.md) |
| **`std/fs.zc`** | 文件系統操作。 | [文檔](../docs/std/fs.md) |
| **`std/io.zc`** | 標準輸入/輸出 (`print`/`println`)。 | [文檔](../docs/std/io.md) |
| **`std/option.zc`** | 可選值 (`Some`/`None`)。 | [文檔](../docs/std/option.md) |
| **`std/result.zc`** | 錯誤處理 (`Ok`/`Err`)。 | [文檔](../docs/std/result.md) |
| **`std/path.zc`** | 跨平台路徑操作。 | [文檔](../docs/std/path.md) |
| **`std/env.zc`** | 進程環境變量。 | [文檔](../docs/std/env.md) |
| **`std/net/`** | TCP, UDP, HTTP, DNS, URL. | [文檔](../docs/std/net.md) |
| **`std/thread.zc`** | 線程與同步。 | [文檔](../docs/std/thread.md) |
| **`std/time.zc`** | 時間測量與睡眠。 | [文檔](../docs/std/time.md) |
| **`std/json.zc`** | JSON 解析與序列化。 | [文檔](../docs/std/json.md) |
| **`std/stack.zc`** | 後進先出棧 `Stack<T>`。 | [文檔](../docs/std/stack.md) |
| **`std/set.zc`** | 泛型哈希集合 `Set<T>`。 | [文檔](../docs/std/set.md) |
| **`std/process.zc`** | 進程執行與管理。 | [文檔](../docs/std/process.md) |
| **`std/regex.zc`** | 正則表達式 (基於 TRE)。 | [文檔](../docs/std/regex.md) |
| **`std/simd.zc`** | 原生 SIMD 向量類型。 | [文檔](../docs/std/simd.md) |

</details>

---

## 工具鏈

Zen C 提供內置的語言服務器 (LSP) 和 REPL 以增強開發體驗。

### 語言服務器 (LSP)

Zen C 語言服務器 (LSP) 支持標準的 LSP 特性，用於編輯器集成：

*   **轉到定義**
*   **查找引用**
*   **懸停信息**
*   **補全** (函數/結構體名，方法/字段的點補全)
*   **文檔符號** (大綱)
*   **簽名幫助**
*   **診斷** (語法/語義錯誤)

啟動語言服務器（通常在編輯器的 LSP 設置中配置）：

```bash
zc-lsp
```

它通過標準 I/O (JSON-RPC 2.0) 進行通信。

### REPL

Read-Eval-Print Loop (REPL) 允許您使用現代的**程序內 JIT 編譯**（由 LibTCC 提供支持）互動式地嘗試 Zen C 程式碼。

```bash
zc-repl
```

#### 特性

*   **JIT 執行**：程式碼在記憶體中編譯並直接在 REPL 程序中執行，以實現極速的快反饋。

*   **交互式編碼**：輸入表達式或語句以立即求值。
*   **持久歷史**：命令保存在 `~/.zprep_history` 中。
*   **啟動腳本**：自動加載 `~/.zprep_init.zc` 中的命令。

#### 命令

| 命令 | 描述 |
|:---|:---|
| `:help` | 顯示可用命令。 |
| `:reset` | 清除當前會話歷史 (變量/函數)。 |
| `:vars` | 顯示活躍變量。 |
| `:funcs` | 顯示用戶定義的函數。 |
| `:structs` | 顯示用戶定義的結構體。 |
| `:imports` | 顯示活躍導入。 |
| `:history` | 顯示會話輸入歷史。 |
| `:type <expr>` | 顯示表達式的類型。 |
| `:c <stmt>` | 顯示語句生成的 C 代碼。 |
| `:time <expr>` | 基准測試表達式 (運行 1000 次迭代)。 |
| `:edit [n]` | 在 `$EDITOR` 中編輯命令 `n` (默認：最後一條)。 |
| `:save <file>` | 將當前會話保存到 `.zc` 文件。 |
| `:load <file>` | 將 `.zc` 文件加載並執行到會話中。 |
| `:watch <expr>` | 監視表達式 (每次輸入後重新求值)。 |
| `:unwatch <n>` | 移除監視。 |
| `:undo` | 從會話中移除最後一條命令。 |
| `:delete <n>` | 移除索引為 `n` 的命令。 |
| `:clear` | 清屏。 |
| `:quit` | 退出 REPL。 |
| `! <cmd>` | 運行 shell 命令 (如 `!ls`)。 |

---


### 語言伺服器協定 (LSP)

Zen C 包含一個內建的語言伺服器，用於編輯器整合。

- **[安裝與設定指南](translations/LSP_ZH_TW.md)**
- **支援的編輯器**: VS Code, Neovim, Vim, Zed, 以及任何支援 LSP 的編輯器。

使用 `zc-lsp` 啟動服務器。

### Zen C 調試

Zen C 程序可以使用標準的 C 調試器（如 **LLDB** 或 **GDB**）進行調試。

#### Visual Studio Code

為了在 VS Code 中獲得最佳體驗，請安裝官方的 [Zen C 擴充功能](https://marketplace.visualstudio.com/items?itemName=Z-libs.zenc)。對於調試，您可以使用 **C/C++**（由 Microsoft 提供）或 **CodeLLDB** 擴充功能。

將這些配置添加到您的 `.vscode` 目錄中，以啟用一鍵調試：

**`tasks.json`** (構建任務):
```json
{
    "label": "Zen C: Build Debug",
    "type": "shell",
    "command": "zc",
    "args": [ "${file}", "-g", "-o", "${fileDirname}/app", "-O0" ],
    "group": { "kind": "build", "isDefault": true }
}
```

**`launch.json`** (調試器):
```json
{
    "name": "Zen C: Debug (LLDB)",
    "type": "lldb",
    "request": "launch",
    "program": "${fileDirname}/app",
    "preLaunchTask": "Zen C: Build Debug"
}
```

## 編譯器支持與兼容性

Zen C 旨在與大多數 C11 編譯器配合使用。某些特性依賴於 GNU C 擴展，但這些擴展通常在其他編譯器中也能工作。使用 `--cc` 標誌切換後端。

```bash
zc run app.zc --cc clang
zc run app.zc --cc zig
```

### 測試套件狀態

<details>
<summary>點擊查看編譯器支持詳情</summary>

| 編譯器 | 通過率 | 受支持特性 | 已知局限性 |
|:---|:---:|:---|:---|
| **GCC** | **100% (全面)** | 所有特性 | 無. |
| **Clang** | **100% (全面)** | 所有特性 | 無. |
| **Zig** | **100% (全面)** | 所有特性 | 無. 使用 `zig cc` 作為替代 C 編譯器. |
| **TCC** | **98% (高)** | 結構體, 泛型, Trait, 模式匹配 | 不支持 Intel ASM, 不支持 `__attribute__((constructor))`. |

</details>

> [!WARNING]
> **編譯器構建警告：** 雖然 **Zig CC** 作為 Zen C 程序的後端非常出色，但使用它構建 *Zen C 編譯器本身*可能會通過驗證，但會生成無法通過測試的不穩定二進制文件。我們建議使用 **GCC** 或 **Clang** 構建編譯器，並僅將 Zig 用作操作代碼的後端。


### MISRA C:2012 合規性測試

Zen C 測試套件包含針對 MISRA C:2012 指南的驗證。

> [!IMPORTANT]
> **MISRA 免責聲明**
> 本項目完全獨立，與 MISRA (Motor Industry Software Reliability Association) 沒有任何關聯、官方認可或商業合作關係。 
> 
> 由於嚴格的版權限制，測試用例僅通過數字標識符列出指南，避免發布內部具體規範。需要原始文檔的用戶，請前往 [MISRA 官方門戶網站](https://www.misra.org.uk/) 獲取真實可靠的指南材料。

### 使用 Zig 構建


Zig 的 `zig cc` 命令提供了 GCC/Clang 的替代方案，具有出色的跨平台編譯支持。使用 Zig：

```bash
# 使用 Zig 編譯並運行 Zen C 程序
zc run app.zc --cc zig

# 使用 Zig 構建 Zen C 編譯器本身
make zig
```

### 輸出後端

Zen C 通過 `--backend` 標誌支援多種輸出後端。每個後端生成不同的目標格式：

| 後端 | 標誌 | 副檔名 | 描述 |
|:---|:---|:---:|:---|
| **C** | `--backend c` | `.c` | 預設 — GNU C11 |
| **C++** | `--backend cpp` | `.cpp` | 相容 C++11（也可使用 `--cpp`） |
| **CUDA** | `--backend cuda` | `.cu` | NVIDIA CUDA C++（也可使用 `--cuda`） |
| **Objective-C** | `--backend objc` | `.m` | Objective-C（也可使用 `--objc`） |
| **JSON** | `--backend json` | `.json` | 機器可讀的 AST，用於工具 |
| **AST 轉儲** | `--backend ast-dump` | `.ast` | 人類可讀的 AST 樹（除錯） |
| **Lisp** | `--backend lisp` | `.lisp` | 轉譯為 Common Lisp（`sbcl --script`） |
| **Graphviz** | `--backend dot` | `.dot` | 視覺化 AST 圖（`dot -Tpng ast.dot -o ast.png`） |

後端特定選項可以通過 `--backend-opt` 設定：

```bash
# 美化輸出 JSON
zc transpile file.zc --backend json --backend-opt pretty

# 顯示完整原始內容（不截斷）
zc transpile file.zc --backend lisp --backend-opt full-content

# 或使用便捷別名：
zc transpile file.zc --backend json --json-pretty
zc transpile file.zc --backend lisp --backend-full-content
```

所有後端選項都是自說明的 — 未知的 `--` 標誌會自動對照已註冊的後端別名進行檢查。

### C++ 互操作

Zen C 可以通過 `--backend cpp` 標誌（簡稱 `--cpp`）生成 C++ 兼容的代碼，從而實現與 C++ 庫的無縫集成。

```bash
# 直接使用 g++ 編譯
zc app.zc --backend cpp

# 或者轉譯用於手動構建
zc transpile app.zc --backend cpp
g++ out.cpp my_cpp_lib.o -o app
```

#### 在 Zen C 中使用 C++

包含 C++ 頭文件並在 `raw` 塊中使用 C++ 代碼：

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
> `--cpp` 標誌會將後端切換為 `g++` 並發出 C+ 兼容的代碼（使用 `auto` 代替 `__auto_type`，使用函數重載代替 `_Generic`，以及對 `void*` 進行顯式轉換）。

#### CUDA 互操作

Zen C 通過轉譯為 **CUDA C++** 來支持 GPU 編程，使用 `--backend cuda` 標誌（簡稱 `--cuda`）。這使你在維持 Zen C 人體工程學語法的同時，能夠利用內核中的強大 C++ 特性（模板、constexpr）。

```bash
# 直接使用 nvcc 編譯
zc run app.zc --backend cuda

# 或者轉譯用於手動構建
zc transpile app.zc --backend cuda -o app.cu
nvcc app.cu -o app
```

#### CUDA 特定屬性

| 屬性 | CUDA 等效項 | 描述 |
|:---|:---|:---|
| `@global` | `__global__` | 內核函數 (運行在 GPU，從主機調用) |
| `@device` | `__device__` | 設備函數 (運行在 GPU，從 GPU 調用) |
| `@host` | `__host__` | 主機函數 (明確僅 CPU 運行) |

#### 內核啟動語法

Zen C 提供了一個簡潔的 `launch` 語句用於調用 CUDA 內核：

```zc
launch kernel_name(args) with {
    grid: num_blocks,
    block: threads_per_block,
    shared_mem: 1024,  // 可選
    stream: my_stream   // 可選
};
```

這轉譯為：`kernel_name<<<grid, block, shared, stream>>>(args);`

#### 編寫 CUDA 內核

使用帶有 `@global` 的 Zen C 函數語法和 `launch` 語句：

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

    // ... 初始化數據 ...
    
    launch add_kernel(d_a, d_b, d_c, N) with {
        grid: (N + 255) / 256,
        block: 256
    };
    
    cuda_sync();
}
```

#### 標準庫 (`std/cuda.zc`)
Zen C 為常見的 CUDA 操作提供了一個標準庫，以減少 `raw` 塊的使用：

```zc
import "std/cuda.zc"

// 內存管理
let d_ptr = cuda_alloc<float>(1024);
cuda_copy_to_device(d_ptr, h_ptr, 1024 * sizeof(float));
defer cuda_free(d_ptr);

// 同步
cuda_sync();

// 線程索引 (在內核內部使用)
let i = thread_id(); // 全局索引
let bid = block_id();
let tid = local_id();
```


> [!NOTE]
> **注意：** `--cuda` 標誌設置 `nvcc` 為編譯器並隱含 `--cpp` 模式。需要安裝 NVIDIA CUDA Toolkit。

### C23 支援

當使用相容的後端編譯器（GCC 14+, Clang 14+）時，Zen C 支援現代 C23 特性。

-   **`auto`**: 如果 `__STDC_VERSION__ >= 202300L`，Zen C 會自動將型別推導映射到標準 C23 `auto`。
-   **`_BitInt(N)`**: 使用 `iN` 和 `uN` 型別（例如 `i256`, `u12`, `i24`）存取 C23 任意位元寬度整數。

### Objective-C 互操作

Zen C 可以通過 `--backend objc` 標誌（簡稱 `--objc`）編譯為 Objective-C (`.m`)，允許你使用 Objective-C 框架（如 Cocoa/Foundation）和語法。

```bash
# 使用 clang 編譯（或 gcc/gnustep）
zc app.zc --backend objc --cc clang
```

#### 在 Zen C 中使用 Objective-C

使用 `include` 包含頭文件，並在 `raw` 塊中使用 Objective-C 語法 (`@interface`, `[...]`, `@""`)。

```zc
//> macos: framework: Foundation
//> linux: cflags: -fconstant-string-class=NSConstantString -D_NATIVE_OBJC_EXCEPTIONS
//> linux: link: -lgnustep-base -lobjc

include <Foundation/Foundation.h>

fn main() {
    raw {
        NSAutoreleasePool *pool = [[NSAutoreleasePool alloc] init];
        NSLog(@"來自 Objective-C 的問候！");
        [pool drain];
    }
    println "Zen C 也能正常工作！";
}
```

> [!NOTE]
> **注意：** Zen C 字符串插值通過調用 `debugDescription` 或 `description` 同樣適用於 Objective-C 對象 (`id`)。

### 18. 單元測試框架

Zen C 内置了測試框架，允許你使用 `test` 關鍵字直接在源文件中編寫單元測試。

#### 語法
`test` 塊包含一個描述性名稱和要執行的代碼體。測試不需要 `main` 函數即可運行。

```zc
test "unittest1" {
    "這是一個單元測試";

    let a = 3;
    assert(a > 0, "a 應該是一個正整數");

    "unittest1 通過。";
}
```

#### 運行測試
要運行文件中的所有測試，請使用 `run` 命令。編譯器將自動檢測並執行所有頂層 `test` 塊。

```bash
zc run my_file.zc
```

#### 斷言
使用內置的 `assert(condition, message)` 函數來驗證預期。如果條件為假，測試將失敗並打印提供的消息。

---

### 公共 API（嵌入）

Zen C 可以通過 `src/public/*.h` 中的公共標頭檔作為 C 函式庫使用。這些標頭檔無需 `-DZC_ALLOW_INTERNAL` 即可編譯，並提供了將編譯器嵌入到您自己的工具中的穩定 API：

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

**編譯方式：**

```bash
cc -I src/public -I src -I src/utils my_tool.c -o my_tool
```

**安裝後（`make install`）：**

```bash
cc -I /usr/local/include/zenc my_tool.c -o my_tool
```

公共 API 涵蓋：
- **`zc_core.h`** — `CompilerConfig`、`ZenCompiler`、`ASTNode`、`Type` 類型，解析器入口點，類型內省輔助函數
- **`zc_driver.h`** — `driver_run()`、`driver_compile()`（完整管線編排）
- **`zc_codegen.h`** — `codegen_node()`、`emit_preamble()`、`format_expression_as_c()`
- **`zc_analysis.h`** — `check_program()`、`check_moves_only()`、`resolve_alias()`
- **`zc_diag.h`** — `zerror_at()`、`zwarn_at()`、`zpanic_at()`，診斷報告
- **`zc_utils.h`** — `Emitter`（輸出緩衝區）、`load_file()`、`z_resolve_path()`

使用 `sudo make install` 安裝以部署標頭檔、二進位檔、手冊頁和標準函式庫。

---

## 貢獻

我們歡迎各類貢獻！無論是修復 Bug、完善文檔，還是提出新功能建議。

請參閱 [CONTRIBUTING_ZH_TW.md](CONTRIBUTING_ZH_TW.md) 了解有關如何貢獻、運行測試和提交拉取請求的詳細指南。

---

## 安全

關於安全漏洞報告的說明，請參閱 [SECURITY_ZH_TW.md](SECURITY_ZH_TW.md)。

---

## 致謝與歸属

本項目使用了第三方庫。完整許可證文本可在 `LICENSES/` 目錄中找到。

*   **[cJSON](https://github.com/DaveGamble/cJSON)** (MIT 許可證)：用於語言服務器中的 JSON 解析和生成。
*   **[zc-ape](https://github.com/OEvgeny/zc-ape)** (MIT 許可證)：由 [Eugene Olonov](https://github.com/OEvgeny) 開發的原版 Zen-C 實際上便攜的可執行文件 (APE) 端口。
*   **[Cosmopolitan Libc](https://github.com/jart/cosmopolitan)** (ISC 許可證)：使 APE 成為可能納基礎庫。
*   **[TRE](https://github.com/laurikari/tre)** (BSD 許可證)：用於標準庫中的正則表達式引擎。
*   **[zenc.vim](https://github.com/zenc-lang/zenc.vim)** (MIT 許可證)：官方 Vim/Neovim 插件，主要由 **[davidscholberg](https://github.com/davidscholberg)** 編寫。
*   **[TinyCC](https://github.com/TinyCC/tinycc)** (LGPL 許可證)：用於高效能 REPL 評估的基礎 JIT 引擎。

---

<div align="center">
  <p>
    Copyright © 2026 Zen C 編程語言。<br>
    今天就開始你的旅程。
  </p>
  <p>
    <a href="https://discord.com/invite/q6wEsCmkJP">Discord</a> •
    <a href="https://github.com/zenc-lang/zenc">GitHub</a> •
    <a href="https://github.com/zenc-lang/docs">文檔</a> •
    <a href="https://github.com/zenc-lang/awesome-zenc">範例</a> •
    <a href="https://github.com/zenc-lang/rfcs">RFC</a> •
    <a href="CONTRIBUTING_ZH_TW.md">貢獻</a>
  </p>
</div>
