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
  <h3>Ergonomia moderna. Zero overhead. C puro.</h3>
  <br>
  <p>
    <a href="#"><img src="https://img.shields.io/badge/build-passing-brightgreen" alt="Status do Build"></a>
    <a href="#"><img src="https://img.shields.io/badge/license-MIT-blue" alt="Licença"></a>
    <a href="#"><img src="https://img.shields.io/github/v/release/zenc-lang/zenc?label=versão&color=orange" alt="Versão"></a>
    <a href="#"><img src="https://img.shields.io/badge/platform-linux%20%7C%20windows%20%7C%20macos-lightgrey" alt="Plataforma"></a>
  </p>
  <p><em>Programe como linguagem de alto nível, execute como C.</em></p>
</div>

<hr>

<div align="center">
  <p>
    <b><a href="#visão-geral">Visão Geral</a></b> •
    <b><a href="#comunidade">Comunidade</a></b> •
    <b><a href="#início-rápido">Início Rápido</a></b> •
    <b><a href="#ecossistema">Ecossistema</a></b> •
    <b><a href="#referência-da-linguagem">Referência da Linguagem</a></b> •
    <b><a href="#biblioteca-padrão">Biblioteca Padrão</a></b> •
    <b><a href="#ferramentas">Ferramentas</a></b>
  </p>
</div>

---

## Visão Geral

**Zen C** é uma moderna linguagem de programação de sistemas que compila para `GNU C`/`C11` legível para humanos. Oferece um rico conjunto de funcionalidades, incluindo inferência de tipos, correspondência de padrões, genéricos, traits, async/await, e gerenciamento manual de memória com capacidades RAII - tudo enquanto mantém 100% de compatibilidade ABI com C.

## Comunidade

Participe das discussões, compartilhe demos, pergunte, ou reporte bugs no servidor oficial de Discord da Zen C!

- Discord: [Clique aqui](https://discord.com/invite/q6wEsCmkJP)
- RFCs: [Propor funcionalidades](https://github.com/zenc-lang/rfcs)

## Ecossistema

O projeto Zen C consiste em vários repositórios. Abaixo você pode encontrar os principais:

| Repositório | Descrição | Estado |
| :--- | :--- | :--- |
| **[zenc](https://github.com/zenc-lang/zenc)** | O compilador core de Zen C (`zc`), CLI e a Biblioteca Padrão. | Desenvolvimento Ativo |
| **[docs](https://github.com/zenc-lang/docs)** | A documentação técnica oficial e a especificação da linguagem. | Ativo |
| **[rfcs](https://github.com/zenc-lang/rfcs)** | O repositório de Pedido de Comentários (RFC). Molde o futuro da linguagem. | Ativo |
| **[vscode-zenc](https://github.com/zenc-lang/vscode-zenc)** | Extensão oficial do VS Code (Destaque de sintaxe, Snippets). | Alpha |
| **[www](https://github.com/zenc-lang/www)** | Código fonte de `zenc-lang.org`. | Ativo |
| **[awesome-zenc](https://github.com/zenc-lang/awesome-zenc)** | Uma lista curada de exemplos fantásticos de Zen C. | Em crescimento |

## Vitrine

Confira estes projetos construídos com Zen C:

- **[ZC-pong-3ds](https://github.com/5quirre1/ZC-pong-3ds)**: Um clone de Pong para Nintendo 3DS.
- **[zen-c-parin](https://github.com/Kapendev/zen-c-parin)**: Um exemplo básico usando Zen C com Parin.
- **[almond](https://git.sr.ht/~leanghok/almond)**: Um navegador web minimalista escrito em Zen C.

---

## Índice

<table align="center">
  <tr>
    <th width="50%">Geral</th>
    <th width="50%">Referência da Linguagem</th>
  </tr>
  <tr>
    <td valign="top">
      <ul>
        <li><a href="#visão-geral">Visão Geral</a></li>
        <li><a href="#comunidade">Comunidade</a></li>
        <li><a href="#ecossistema">Ecossistema</a></li>
        <li><a href="https://github.com/zenc-lang/rfcs">RFCs</a></li>
        <li><a href="#início-rápido">Início Rápido</a></li>
        <li><a href="https://github.com/zenc-lang/docs">Documentação</a></li>
        <li><a href="#biblioteca-padrão">Biblioteca Padrão</a></li>
        <li><a href="#ferramentas">Ferramentas</a>
          <ul>
            <li><a href="#protocolo-de-servidor-de-linguagem-lsp">LSP</a></li>
            <li><a href="#depuração-de-zen-c">Depuração</a></li>
          </ul>
        </li>
        <li><a href="#suporte-de-compiladores--compatibilidade">Suporte de Compiladores</a></li>
        <li><a href="#contribuindo">Contribuindo</a></li>
        <li><a href="#atribuições">Atribuições</a></li>
      </ul>
    </td>
    <td valign="top">
      <p><a href="https://docs.zenc-lang.org/tour/"><b>Browse the Language Reference</b></a></p>
    </td>
  </tr>
</table>

---

## Início Rápido

### Instalação

```bash
git clone https://github.com/zenc-lang/zenc.git
cd zenc
make clean # remove arquivos de build antigos
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

Zen C tem suporte nativo completo para Windows (x86_64). Você pode compilar usando o script em lote (batch) fornecido com GCC (MinGW):

```cmd
build.bat
```

Isso construirá o compilador (`zc.exe`). As operações de Rede, Sistema de Arquivos e Processo são totalmente suportadas através da Camada de Abstração de Plataforma (PAL).

Alternativamente, você pode usar `make` se tiver um ambiente semelhante ao Unix (MSYS2, Cygwin, git-bash).

### Build Portátil (APE)

Zen C pode ser compilado como um **Actually Portable Executable (APE)** usando [Cosmopolitan Libc](https://github.com/jart/cosmopolitan). Isso gera um único binário (`.com`) que executa nativamente em Linux, macOS, Windows, FreeBSD, OpenBSD, e NetBSD, tanto em arquiteturas x86_64 quanto aarch64.

**Pré-requisitos:**
- Toolchain `cosmocc` (precisa estar no seu PATH)

**Build & Instalação:**
```bash
make ape
sudo env "PATH=$PATH" make install-ape
```

**Artifacts:**
- `out/bin/zc.com`: O compilador portátil de Zen-C. Inclui a biblioteca padrão embutida no executável.
- `out/bin/zc-boot.com`: Um instalador bootstrap independente para configurar novos projetos Zen-C.

**Uso:**
```bash
# Executa em qualquer SO suportado
./out/bin/zc.com build hello.zc -o hello
```

### Build Modular

O Zen C é dividido em módulos opcionais. Use as flags `ZC_*` para selecionar recursos ao compilar:

| Flag | Padrão | Exclui |
|---|---|---|
| `ZC_LSP=0` | 1 | Servidor LSP (~8 arquivos) |
| `ZC_REPL=0` | 1 | REPL interativo (~6 arquivos) |
| `ZC_PLUGINS=0` | 1 | Sistema de plugins |
| `ZC_ZEN=0` | 1 | Modos `--doc` / `--facts` |
| `ZC_BACKENDS=0` | 1 | Backends não-C (JSON, Lisp, etc.) |
| `ZC_TRE=0` | 1 | Biblioteca de regex TRE |

```bash
make                     # Todos os recursos (3.3 MB)
make lite                # Sem LSP, REPL ou Zen (2.9 MB)
make core                # Apenas compilador (2.7 MB)
make minimal             # Mínimo absoluto (2.6 MB)

# Seleção personalizada:
make ZC_LSP=0 ZC_REPL=0  # Excluir LSP e REPL
```

Comandos desabilitados mostram uma mensagem clara em vez de falhar: `zc-lsp` → "LSP support not included".

### Uso

```bash
# Compila e executa
zc run hello.zc

# Build do executável
zc build hello.zc -o hello

# Shell interativo
zc-repl

# Documentação (Recursiva)
zc-doc main.zc

# Documentação (Arquivo único, sem verificação)
zc-doc --no-recursive-doc main.zc

```

#### Características

*   **Execução JIT**: O código é compilado na memória e executado diretamente dentro do processo REPL para um feedback instantâneo.

### Variáveis de Ambiente

Você pode configurar `ZC_ROOT` para especificar a localização da Biblioteca Padrão (imports padrões como `import "std/vec.zc"`). Isso permite que você execute `zc` de qualquer diretório.

```bash
export ZC_ROOT=/path/to/zenc
```

---

## Referência da Linguagem

Consulte a [Referência da Linguagem](https://docs.zenc-lang.org/tour/01-variables-constants/) oficial para mais detalhes.

## Biblioteca Padrão

O Zen C inclui a biblioteca padrão (`std`), que cobre as funcionalidades essenciais.

[Navegue pela Documentação da Biblioteca Padrão](../docs/std/README.md)

### Módulos Principais

<details>
<summary>Clique para ver todos os módulos da Biblioteca Padrão</summary>

| Módulo | Descrição | Docs |
| :--- | :--- | :--- |
| **`std/bigfloat.zc`** | Aritmética de ponto flutuante de precisão arbitrária. | [Docs](../docs/std/bigfloat.md) |
| **`std/bigint.zc`** | Inteiro de precisão arbitrária `BigInt`. | [Docs](../docs/std/bigint.md) |
| **`std/bits.zc`** | Operações bit-a-bit de baixo nível (`rotl`, `rotr`, etc). | [Docs](../docs/std/bits.md) |
| **`std/complex.zc`** | Aritmética de números complexos `Complex`. | [Docs](../docs/std/complex.md) |
| **`std/vec.zc`** | Growable dynamic array `Vec<T>`. | [Docs](../docs/std/vec.md) |
| **`std/string.zc`** | Heap-allocated `String` type with UTF-8 support. | [Docs](../docs/std/string.md) |
| **`std/queue.zc`** | FIFO queue (Ring Buffer). | [Docs](../docs/queue.md) |
| **`std/map.zc`** | Generic Hash Map `Map<V>`. | [Docs](../docs/std/map.md) |
| **`std/fs.zc`** | File system operations. | [Docs](../docs/std/fs.md) |
| **`std/io.zc`** | Standard Input/Output (`print`/`println`). | [Docs](../docs/std/io.md) |
| **`std/option.zc`** | Optional values (`Some`/`None`). | [Docs](../docs/std/option.md) |
| **`std/result.zc`** | Error handling (`Ok`/`Err`). | [Docs](../docs/std/result.md) |
| **`std/path.zc`** | Cross-platform path manipulation. | [Docs](../docs/std/path.md) |
| **`std/env.zc`** | Process environment variables. | [Docs](../docs/std/env.md) |
| **`std/net/`** | TCP, UDP, HTTP, DNS, URL. | [Docs](../docs/std/net.md) |
| **`std/thread.zc`** | Threads and Synchronization. | [Docs](../docs/std/thread.md) |
| **`std/time.zc`** | Time measurement and sleep. | [Docs](../docs/std/time.md) |
| **`std/json.zc`** | JSON parsing and serialization. | [Docs](../docs/std/json.md) |
| **`std/stack.zc`** | LIFO Stack `Stack<T>`. | [Docs](../docs/std/stack.md) |
| **`std/set.zc`** | Generic Hash Set `Set<T>`. | [Docs](../docs/std/set.md) |
| **`std/process.zc`** | Process execution and management. | [Docs](../docs/std/process.md) |
| **`std/regex.zc`** | Expressões Regulares (baseado em TRE). | [Docs](../docs/std/regex.md) |
| **`std/simd.zc`** | Tipos de vetores SIMD nativos. | [Docs](../docs/std/simd.md) |

</details>

---

## Ferramentas

Zen C inclui um Language Server embutido (`zc-lsp`) e um REPL para aprimorar a experiência do desenvolvimento.

### Language Server (LSP)

O Zen C Language Server (LSP) suporta funcionalidades padrão de LSP para integração com editores, fornecendo:

*   **Go to Definition** - Vá para definição
*   **Find References** - Encontrar referências
*   **Hover Information** - Informação com sobreposição do ponteiro do mouse
*   **Completion** - Auto-completar (Nomes de Função/Struct, compleção de ponto para métodos/campos)
*   **Document Symbols** - Símbolos de documento (Outline)
*   **Signature Help** - Ajuda de assinatura
*   **Diagnostics** - Diagnóstico (Sintaxe/Erros semânticos)

Para inicializar o servidor da linguagem (tipicamente configurado nas configurações LSP do seu editor):

```bash
zc-lsp
```

Ele se comunica via I/O padrão (JSON-RPC 2.0).

### REPL

O loop Read-Eval-Print (REPL) permite que você experimente o código Zen C interativamente usando a moderna **compilação JIT em processo** (alimentada por LibTCC).

#### Funcionalidades:

*   **Código Interativo**: Escreva expressões ou declarações para avaliação imediata.
*   **Histórico Persistente**: Comandos são salvos em `~/.zprep_history`.
*   **Script de Inicialização**: Automaticamente carrega comandos de `~/.zprep_init.zc`.

#### Comandos

| Comando | Descrição |
|:---|:---|
| `:help` | Mostra comandos disponíveis. |
| `:reset` | Reinicia a sessão atual (limpa todas as definições). |
| `:vars` | Mostra variáveis ativas. |
| `:funcs` | Mostra funções definidas pelo usuário. |
| `:structs` | Mostra structs definidos pelo usuário. |
| `:imports` | Mostra imports ativos. |
| `:history` | Mostra histórico de entrada da sessão. |
| `:type <expr>` | Mostra o tipo de uma expressão. |
| `:c <stmt>` | Mostra o código C gerado para uma declaração. |
| `:time <expr>` | Faz benchmark de uma expressão (executa 1000 iterações). |
| `:edit [n]` | Edita comando `n` (padrão: último) em `$EDITOR`. |
| `:save <file>` | Salva a sessão atual em um arquivo `.zc`. |
| `:load <file>` | Carrega e executa um arquivo `.zc` na sessão. |
| `:watch <expr>` | Observa uma expressão (reavaliada após cada entrada). |
| `:unwatch <n>` | Remove um watch. |
| `:undo` | Remove o último comando da sessão. |
| `:delete <n>` | Remove comando no índice `n`. |
| `:clear` | Limpa a tela. |
| `:quit` | Sai do REPL. |
| `! <cmd>` | Executa um comando shell (e.g. `!ls`). |

---

### Protocolo de Servidor de Linguagem (LSP)

O Zen C inclui um Servidor de Linguagem integrado para integração com editores.

- **[Guia de Instalação e Configuração](translations/LSP_PT_BR.md)**
- **Editores Suportados**: VS Code, Neovim, Vim, Zed, e qualquer editor capaz de LSP.

Use `zc-lsp` para iniciar o servidor.

### Depuração de Zen C

Os programas Zen C podem ser depurados usando depuradores C padrão, como **LLDB** ou **GDB**.

#### Visual Studio Code

Para a melhor experiência no VS Code, instale a [extensão oficial do Zen C](https://marketplace.visualstudio.com/items?itemName=Z-libs.zenc). Para depuração, você pode usar a extensão **C/C++** (da Microsoft) ou a **CodeLLDB**.

Adicione estas configurações ao seu diretório `.vscode` para habilitar a depuração com um clique:

**`tasks.json`** (Tarefa de Compilação):
```json
{
    "label": "Zen C: Build Debug",
    "type": "shell",
    "command": "zc",
    "args": [ "${file}", "-g", "-o", "${fileDirname}/app", "-O0" ],
    "group": { "kind": "build", "isDefault": true }
}
```

**`launch.json`** (Depurador):
```json
{
    "name": "Zen C: Debug (LLDB)",
    "type": "lldb",
    "request": "launch",
    "program": "${fileDirname}/app",
    "preLaunchTask": "Zen C: Build Debug"
}
```
## Suporte do Compilador e Compatibilidade

Zen C foi projetado para funcionar com a maioria dos compiladores C11. Algumas funcionalidades dependem de extensões GNU C, mas estas frequentemente funcionam em outros compiladores. Use a flag `--cc` para trocar backends.

```bash
zc run app.zc --cc clang
zc run app.zc --cc zig
```

### Status da Suíte de Testes

<details>
<summary>Clique para ver detalhes do Suporte de Compilador</summary>

| Compilador | Taxa de Aprovação | Funcionalidades Suportadas | Limitações Conhecidas |
|:---|:---:|:---|:---|
| **GCC** | **100% (Completo)** | Todas as Funcionalidades | Nenhuma. |
| **Clang** | **100% (Completo)** | Todas as Funcionalidades | Nenhuma. |
| **Zig** | **100% (Completo)** | Todas as Funcionalidades | Nenhuma. Usa `zig cc` como compilador C drop-in. |
| **TCC** | **98% (Alto)** | Estruturas, Genéricos, Traits, Pattern Matching | Sem ASM Intel, Sem `__attribute__((constructor))`. |

</details>

> [!WARNING]
> **AVISO DE COMPILAÇÃO:** Embora **Zig CC** funcione excelentemente como backend para seus programas Zen C, compilar o *próprio compilador Zen C* com ele pode verificar, mas produzir um binário instável que falha nos testes. Recomendamos compilar o compilador com **GCC** ou **Clang** e usar Zig apenas como backend para seu código operacional.


### Testes de Conformidade MISRA C:2012

A suíte de testes do Zen C inclui verificação contra as diretrizes MISRA C:2012.

> [!IMPORTANT]
> **Aviso de Isenção de Responsabilidade da MISRA**
> Este projeto é totalmente independente e não possui nenhuma afiliação, endosso oficial ou conexão corporativa com a MISRA (Motor Industry Software Reliability Association). 
> 
> Devido a restrições estritas de direitos autorais, os casos de teste apenas listam diretrizes por seus identificadores numéricos e evitam publicar especificações internas. Usuários que precisem da documentação primária são incentivados a adquirir os materiais autênticos das diretrizes no [Portal Oficial da MISRA](https://www.misra.org.uk/).

### Build com Zig


O comando `zig cc` do Zig fornece um substituto drop-in para GCC/Clang com excelente suporte de compilação cruzada. Para usar Zig:

```bash
# Compila e executa um programa Zen C com Zig
zc run app.zc --cc zig

# Faz build do próprio compilador Zen C com Zig
make zig
```

### Backends de Saída

Zen C suporta múltiplos backends de saída através da flag `--backend`. Cada backend produz um formato de destino diferente:

| Backend | Flag | Extensão | Descrição |
|:---|:---|:---:|:---|
| **C** | `--backend c` | `.c` | Padrão — GNU C11 |
| **C++** | `--backend cpp` | `.cpp` | Compatível com C++11 (também disponível como `--cpp`) |
| **CUDA** | `--backend cuda` | `.cu` | NVIDIA CUDA C++ (também disponível como `--cuda`) |
| **Objective-C** | `--backend objc` | `.m` | Objective-C (também disponível como `--objc`) |
| **JSON** | `--backend json` | `.json` | AST legível por máquina para ferramentas |
| **AST dump** | `--backend ast-dump` | `.ast` | Árvore AST legível por humanos (depuração) |
| **Lisp** | `--backend lisp` | `.lisp` | Transpilar para Common Lisp (`sbcl --script`) |
| **Graphviz** | `--backend dot` | `.dot` | Grafo AST visual (`dot -Tpng ast.dot -o ast.png`) |

Opções específicas do backend podem ser configuradas com `--backend-opt`:

```bash
# Saída JSON com formatação legível
zc transpile file.zc --backend json --backend-opt pretty

# Mostrar conteúdo completo sem truncamento
zc transpile file.zc --backend lisp --backend-opt full-content

# Ou usar alias de conveniência:
zc transpile file.zc --backend json --json-pretty
zc transpile file.zc --backend lisp --backend-full-content
```

Todas as opções de backend são autodocumentadas — flags `--` desconhecidas são verificadas automaticamente contra os aliases de backend registrados.

### Interoperabilidade C++

Zen C pode gerar código compatível com C++ com a flag `--backend cpp` (`--cpp` para abreviar), permitindo integração sem emendas com bibliotecas C++.

```bash
# Compilação direta com g++
zc app.zc --backend cpp

# Ou transpile para build manual
zc transpile app.zc --backend cpp
g++ out.cpp my_cpp_lib.o -o app
```

#### Usando C++ em Zen C

Inclua headers C++ e use blocos raw para código C++:

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

> **Nota:** A flag `--cpp` troca o backend para `g++` e emite código compatível com C++ (usa `auto` em vez de `__auto_type`, sobrecarga de função em vez de `_Generic`, e casts explícitos para `void*`).

### Interoperabilidade CUDA

Zen C suporta programação GPU transpilando para **CUDA C++** através da flag `--backend cuda` (`--cuda` para abreviar). Isso permite que você aproveite poderosas funcionalidades C++ (templates, constexpr) dentro de seus kernels enquanto mantém a sintaxe ergonômica do Zen C.

```bash
# Compilação direta com nvcc
zc run app.zc --backend cuda

# Ou transpile para build manual
zc transpile app.zc --backend cuda -o app.cu
nvcc app.cu -o app
```

#### Atributos Específicos do CUDA

| Atributo | Equivalente CUDA | Descrição |
|:---|:---|:---|
| `@global` | `__global__` | Função kernel (executa na GPU, chamada do host) |
| `@device` | `__device__` | Função device (executa na GPU, chamada da GPU) |
| `@host` | `__host__` | Função host (explicitamente apenas CPU) |

#### Sintaxe de Launch de Kernel

Zen C fornece uma instrução `launch` limpa para invocar kernels CUDA:

```zc
launch kernel_name(args) with {
    grid: num_blocks,
    block: threads_per_block,
    shared_mem: 1024,  // Opcional
    stream: my_stream   // Opcional
};
```

Isso transpila para: `kernel_name<<<grid, block, shared, stream>>>(args);`

#### Escrevendo Kernels CUDA

Use sintaxe de função Zen C com `@global` e a instrução `launch`:

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

#### Biblioteca Padrão (`std/cuda.zc`)
Zen C fornece uma biblioteca padrão para operações CUDA comuns para reduzir blocos `raw`:

```zc
import "std/cuda.zc"

// Gerenciamento de memória
let d_ptr = cuda_alloc<float>(1024);
cuda_copy_to_device(d_ptr, h_ptr, 1024 * sizeof(float));
defer cuda_free(d_ptr);

// Sincronização
cuda_sync();

// Indexação de Thread (use dentro de kernels)
let i = thread_id(); // Índice global
let bid = block_id();
let tid = local_id();
```

> [!NOTE]
> **Nota:** A flag `--cuda` define `nvcc` como o compilador e implica modo `--cpp`. Requer o NVIDIA CUDA Toolkit.

### Suporte C23

Zen C suporta funcionalidades modernas de C23 quando usa um compilador backend compatível (GCC 14+, Clang 14+, TCC (parcial)).

- **`auto`**: Zen C automaticamente mapeia inferência de tipo para o `auto` padrão C23 se `__STDC_VERSION__ >= 202300L`.
- **`_BitInt(N)`**: Use tipos `iN` e `uN` (e.g., `i256`, `u12`, `i24`) para acessar inteiros de largura arbitrária do C23.

### Interoperabilidade Objective-C

Zen C pode compilar para Objective-C (`.m`) usando a flag `--backend objc` (`--objc` para abreviar), permitindo que você use frameworks Objective-C (como Cocoa/Foundation) e sintaxe.

```bash
# Compila com clang (ou gcc/gnustep)
zc app.zc --backend objc --cc clang
```

#### Usando Objective-C em Zen C

Use `include` para headers e blocos `raw` para sintaxe Objective-C (`@interface`, `[...]`, `@""`).

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
> **Nota:** Interpolação de strings do Zen C funciona com objetos Objective-C (`id`) chamando `debugDescription` ou `description`.

### 18. Framework de Testes Unitários

O Zen C inclui um framework de testes integrado que permite escrever testes unitários diretamente nos arquivos-fonte usando a palavra-chave `test`.

#### Sintaxe
Um bloco `test` contém um nome descritivo e um corpo de código para execução. Os testes não exigem uma função `main` para serem executados.

```zc
test "unittest1" {
    "Este é um teste unitário";

    let a = 3;
    assert(a > 0, "a deve ser um inteiro positivo");

    "unittest1 passou.";
}
```

#### Executando Testes
Para executar todos os testes em um arquivo, use o comando `run`. O compilador detectará e executará automaticamente todos os blocos `test` de nível superior.

```bash
zc run meu_arquivo.zc
```

#### Asserções
Use a função integrada `assert(condição, mensagem)` para verificar as expectativas. Se a condição for falsa, o teste falhará e imprimirá a mensagem fornecida.


---

### API Pública (Incrustação)

Zen C pode ser usado como uma biblioteca C através dos cabeçalhos públicos em `src/public/*.h`. Estes cabeçalhos compilam sem `-DZC_ALLOW_INTERNAL` e fornecem uma API estável para incorporar o compilador em suas próprias ferramentas:

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

**Compilar com:**

```bash
cc -I src/public -I src -I src/utils my_tool.c -o my_tool
```

**Após instalar (`make install`):**

```bash
cc -I /usr/local/include/zenc my_tool.c -o my_tool
```

A API pública cobre:
- **`zc_core.h`** — Tipos `CompilerConfig`, `ZenCompiler`, `ASTNode`, `Type`, pontos de entrada do parser, auxiliares de introspecção de tipos
- **`zc_driver.h`** — `driver_run()`, `driver_compile()` (orquestração completa do pipeline)
- **`zc_codegen.h`** — `codegen_node()`, `emit_preamble()`, `format_expression_as_c()`
- **`zc_analysis.h`** — `check_program()`, `check_moves_only()`, `resolve_alias()`
- **`zc_diag.h`** — `zerror_at()`, `zwarn_at()`, `zpanic_at()`, relatórios de diagnóstico
- **`zc_utils.h`** — `Emitter` (buffer de saída), `load_file()`, `z_resolve_path()`

Instale com `sudo make install` para implantar cabeçalhos, binário, páginas man e biblioteca padrão.

---

## Contribuindo

Nós damos boas-vindas a contribuições! Seja consertando bugs, adicionando documentação ou propondo novas funcionalidades.

Por favor, veja [CONTRIBUTING_PT_BR.md](CONTRIBUTING_PT_BR.md) para diretrizes detalhadas sobre como contribuir, executar testes e submeter pull requests.

---

## Segurança

Para instruções sobre relatórios de segurança, por favor veja [SECURITY_PT_BR.md](SECURITY_PT_BR.md).

---

## Atribuições

Este projeto usa bibliotecas de terceiros. Textos completos de licença podem ser encontrados no diretório `LICENSES/`.

*   **[cJSON](https://github.com/DaveGamble/cJSON)** (Licença MIT): Usado para parsing e geração JSON no Language Server.
*   **[zc-ape](https://github.com/OEvgeny/zc-ape)** (Licença MIT): O port original Actually Portable Executable do Zen-C por [Eugene Olonov](https://github.com/OEvgeny).
*   **[Cosmopolitan Libc](https://github.com/jart/cosmopolitan)** (Licença ISC): A biblioteca fundadora que torna APE possível.
*   **[TRE](https://github.com/laurikari/tre)** (Licença BSD): Usado para o motor de expressões regulares na biblioteca padrão.
*   **[zenc.vim](https://github.com/zenc-lang/zenc.vim)** (Licença MIT): O plugin oficial para Vim/Neovim, escrito principalmente por **[davidscholberg](https://github.com/davidscholberg)**.
*   **[TinyCC](https://github.com/TinyCC/tinycc)** (Licença LGPL): O mecanismo JIT fundamental usado para a avaliação do REPL de alto desempenho.

---

<div align="center">
  <p>
    Copyright © 2026 Linguagem de Programação Zen C.<br>
    Comece sua jornada hoje.
  </p>
  <p>
    <a href="https://discord.com/invite/q6wEsCmkJP">Discord</a> •
    <a href="https://github.com/zenc-lang/zenc">GitHub</a> •
    <a href="https://github.com/zenc-lang/docs">Documentação</a> •
    <a href="https://github.com/zenc-lang/awesome-zenc">Exemplos</a> •
    <a href="https://github.com/zenc-lang/rfcs">RFCs</a> •
    <a href="CONTRIBUTING_PT_BR.md">Contribuir</a>
  </p>
</div>
