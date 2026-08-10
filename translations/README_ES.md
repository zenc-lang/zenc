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
  <h3>Ergonomía Moderna. Cero Overhead. C Puro.</h3>
  <br>
  <p>
    <a href="#"><img src="https://img.shields.io/badge/build-passing-brightgreen" alt="Estado de la Construcción"></a>
    <a href="#"><img src="https://img.shields.io/badge/license-MIT-blue" alt="Licencia"></a>
    <a href="#"><img src="https://img.shields.io/github/v/release/zenc-lang/zenc?label=version&color=orange" alt="Versión"></a>
    <a href="#"><img src="https://img.shields.io/badge/platform-linux%20%7C%20windows%20%7C%20macos-lightgrey" alt="Plataforma"></a>
  </p>
  <p><em>Escribe como un lenguaje de alto nivel, ejecuta como C.</em></p>
</div>

<hr>

<div align="center">
  <p>
    <b><a href="#descripción-general">Descripción General</a></b> •
    <b><a href="#comunidad">Comunidad</a></b> •
    <b><a href="#inicio-rápido">Inicio Rápido</a></b> •
    <b><a href="#ecosistema">Ecosistema</a></b> •
    <b><a href="#referencia-del-lenguaje">Referencia del Lenguaje</a></b> •
    <b><a href="#biblioteca-estándar">Biblioteca Estándar</a></b> •
    <b><a href="#herramientas">Herramientas</a></b>
  </p>
</div>

---

## Descripción General

**Zen C** es un lenguaje de programación de sistemas moderno que se compila a `GNU C`/`C11` legible por humanos. Proporciona un conjunto rico de características que incluyen inferencia de tipos, coincidencia de patrones (pattern matching), genéricos, traits, async/await y gestión manual de memoria con capacidades RAII, todo manteniendo una compatibilidad total con el ABI de C.

## Comunidad

¡Únete a la discusión, comparte demos, haz preguntas o reporta errores en el servidor oficial de Discord de Zen C!

- Discord: [Únete aquí](https://discord.com/invite/q6wEsCmkJP)
- RFCs: [Proponer características](https://github.com/zenc-lang/rfcs)

## Ecosistema

El proyecto Zen C consta de varios repositorios. A continuación se presentan los principales:

| Repositorio | Descripción | Estado |
| :--- | :--- | :--- |
| **[zenc](https://github.com/zenc-lang/zenc)** | El compilador central de Zen C (`zc`), la CLI y la biblioteca estándar. | Desarrollo Activo |
| **[docs](https://github.com/zenc-lang/docs)** | La documentación técnica oficial y la especificación del lenguaje. | Activo |
| **[rfcs](https://github.com/zenc-lang/rfcs)** | El repositorio de Solicitud de Comentarios (RFC). Dale forma al futuro del lenguaje. | Activo |
| **[vscode-zenc](https://github.com/zenc-lang/vscode-zenc)** | Extensión oficial de VS Code (Resaltado de sintaxis, Snippets). | Alpha |
| **[www](https://github.com/zenc-lang/www)** | Código fuente de `zenc-lang.org`. | Activo |
| **[awesome-zenc](https://github.com/zenc-lang/awesome-zenc)** | Una lista curada de ejemplos asombrosos de Zen C. | Creciendo |
| **[zenc.vim](https://github.com/zenc-lang/zenc.vim)** | Plugin oficial para Vim/Neovim (Sintaxis, Sangría). | Activo |

## Proyectos Destacados

Echa un vistazo a estos proyectos construidos con Zen C:

- **[ZC-pong-3ds](https://github.com/5quirre1/ZC-pong-3ds)**: Un clon de Pong para Nintendo 3DS.
- **[zen-c-parin](https://github.com/Kapendev/zen-c-parin)**: Un ejemplo básico usando Zen C con Parin.
- **[almond](https://git.sr.ht/~leanghok/almond)**: Un navegador web minimalista escrito en Zen C.

---

## Índice

<table align="center">
  <tr>
    <th width="50%">General</th>
    <th width="50%">Referencia del Lenguaje</th>
  </tr>
  <tr>
    <td valign="top">
      <ul>
        <li><a href="#descripción-general">Descripción General</a></li>
        <li><a href="#comunidad">Comunidad</a></li>
        <li><a href="#ecosistema">Ecosistema</a></li>
        <li><a href="https://github.com/zenc-lang/rfcs">RFCs</a></li>
        <li><a href="#inicio-rápido">Inicio Rápido</a></li>
        <li><a href="https://github.com/zenc-lang/docs">Documentación</a></li>
        <li><a href="#biblioteca-estándar">Biblioteca Estándar</a></li>
        <li><a href="#herramientas">Herramientas</a>
          <ul>
            <li><a href="#protocolo-de-servidor-de-lenguaje-lsp">LSP</a></li>
            <li><a href="#depuración-de-zen-c">Depuración</a></li>
          </ul>
        </li>
        <li><a href="#soporte-del-compilador-y-compatibilidad">Soporte del Compilador</a></li>
        <li><a href="#contribuyendo">Contribuyendo</a></li>
        <li><a href="#atribuciones">Atribuciones</a></li>
      </ul>
    </td>
    <td valign="top">
      <p><a href="https://docs.zenc-lang.org/tour/"><b>Browse the Language Reference</b></a></p>
    </td>
  </tr>
</table>

---

## Inicio Rápido

### Instalación

```bash
git clone https://github.com/zenc-lang/zenc.git
cd zenc
make clean # eliminar archivos de construcción antiguos
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

Zen C tiene soporte nativo completo para Windows (x86_64). Puedes construirlo usando el script de procesamiento por lotes (batch) proporcionado con GCC (MinGW):

```cmd
build.bat
```

Esto construirá el compilador (`zc.exe`). Las operaciones de Red, Sistema de Archivos y Procesos están totalmente soportadas a través de la Capa de Abstracción de Plataforma (PAL).

Alternativamente, puedes usar `make` si tienes un entorno tipo Unix (MSYS2, Cygwin, git-bash).

### Construcción Portable (APE)

Zen C puede compilarse como un **Ejecutable Realmente Portable (APE)** usando [Cosmopolitan Libc](https://github.com/jart/cosmopolitan). Esto produce un único binario (`.com`) que se ejecuta de forma nativa en Linux, macOS, Windows, FreeBSD, OpenBSD y NetBSD en arquitecturas x86_64 y aarch64.

**Prerrequisitos:**
- Toolchain `cosmocc` (debe estar en tu PATH)

**Construcción e Instalación:**
```bash
make ape
sudo env "PATH=$PATH" make install-ape
```

**Artefactos:**
- `out/bin/zc.com`: El compilador Zen-C portable. Incluye la biblioteca estándar embebida dentro del ejecutable.
- `out/bin/zc-boot.com`: Un instalador bootstrap autónomo para configurar nuevos proyectos Zen-C.

**Uso:**
```bash
# Ejecutar en cualquier SO compatible
./out/bin/zc.com build hello.zc -o hello
```

### Construcción Modular

Zen C está dividido en módulos opcionales. Usa las banderas `ZC_*` para seleccionar características al compilar:

| Bandera | Por defecto | Excluye |
|---|---|---|
| `ZC_LSP=0` | 1 | Servidor LSP (~8 archivos) |
| `ZC_REPL=0` | 1 | REPL interactivo (~6 archivos) |
| `ZC_PLUGINS=0` | 1 | Sistema de plugins |
| `ZC_ZEN=0` | 1 | Modos `--doc` / `--facts` |
| `ZC_BACKENDS=0` | 1 | Backends no-C (JSON, Lisp, etc.) |
| `ZC_TRE=0` | 1 | Biblioteca de regex TRE |

```bash
make                     # Todas las características (3.3 MB)
make lite                # Sin LSP, REPL ni Zen (2.9 MB)
make core                # Solo compilador (2.7 MB)
make minimal             # Mínimo absoluto (2.6 MB)

# Selección personalizada:
make ZC_LSP=0 ZC_REPL=0  # Excluir LSP y REPL
```

Los comandos deshabilitados muestran un mensaje claro en vez de fallar: `zc-lsp` → "LSP support not included".

### Uso

```bash
# Compilar y ejecutar
zc run hello.zc

# Construir ejecutable
zc build hello.zc -o hello

# Shell Interactiva
zc-repl

# Documentación (Recursiva)
zc-doc main.zc

# Documentación (Archivo único, sin comprobación)
zc-doc --no-recursive-doc main.zc

```

### Variables de Entorno

Puedes configurar `ZC_ROOT` para especificar la ubicación de la Biblioteca Estándar (importaciones estándar como `import "std/vector.zc"`). Esto te permite ejecutar `zc` desde cualquier directorio.

```bash
export ZC_ROOT=/ruta/a/zenc
```

---

## Referencia del Lenguaje

Consulte la [Referencia del Lenguaje](https://docs.zenc-lang.org/tour/01-variables-constants/) oficial para obtener más detalles.

## Biblioteca Estándar

Zen C incluye una biblioteca estándar (`std`) que cubre las funcionalidades esenciales.

[Explorar la Documentación de la Biblioteca Estándar](../docs/std/README.md)

### Módulos Clave

<details>
<summary>Click para ver todos los módulos de la Biblioteca Estándar</summary>

| Módulo | Descripción | Docs |
| :--- | :--- | :--- |
| **`std/bigfloat.zc`** | Aritmética de punto flotante de precisión arbitraria. | [Docs](../docs/std/bigfloat.md) |
| **`std/bigint.zc`** | Entero de precisión arbitraria `BigInt`. | [Docs](../docs/std/bigint.md) |
| **`std/bits.zc`** | Operaciones bit a bit de bajo nivel (`rotl`, `rotr`, etc). | [Docs](../docs/std/bits.md) |
| **`std/complex.zc`** | Aritmética de números complejos `Complex`. | [Docs](../docs/std/complex.md) |
| **`std/vec.zc`** | Array dinámico creíble `Vec<T>`. | [Docs](../docs/std/vec.md) |
| **`std/string.zc`** | Tipo `String` asignado en el heap con soporte UTF-8. | [Docs](../docs/std/string.md) |
| **`std/queue.zc`** | Cola FIFO (Ring Buffer). | [Docs](../docs/std/queue.md) |
| **`std/map.zc`** | Mapa Hash Genérico `Map<V>`. | [Docs](../docs/std/map.md) |
| **`std/fs.zc`** | Operaciones del sistema de archivos. | [Docs](../docs/std/fs.md) |
| **`std/io.zc`** | Entrada/Salida estándar (`print`/`println`). | [Docs](../docs/std/io.md) |
| **`std/option.zc`** | Valores opcionales (`Some`/`None`). | [Docs](../docs/std/option.md) |
| **`std/result.zc`** | Gestión de errores (`Ok`/`Err`). | [Docs](../docs/std/result.md) |
| **`std/path.zc`** | Manipulación de rutas multiplataforma. | [Docs](../docs/std/path.md) |
| **`std/env.zc`** | Variables de entorno del proceso. | [Docs](../docs/std/env.md) |
| **`std/net/`** | TCP, UDP, HTTP, DNS, URL. | [Docs](../docs/std/net.md) |
| **`std/thread.zc`** | Hilos y Sincronización. | [Docs](../docs/std/thread.md) |
| **`std/time.zc`** | Medición de tiempo y espera (sleep). | [Docs](../docs/std/time.md) |
| **`std/json.zc`** | Parseo y serialización de JSON. | [Docs](../docs/std/json.md) |
| **`std/stack.zc`** | Pila LIFO `Stack<T>`. | [Docs](../docs/std/stack.md) |
| **`std/set.zc`** | Conjunto Hash Genérico `Set<T>`. | [Docs](../docs/std/set.md) |
| **`std/process.zc`** | Ejecución y gestión de procesos. | [Docs](../docs/std/process.md) |
| **`std/regex.zc`** | Expresiones Regulares (basado en TRE). | [Docs](../docs/std/regex.md) |
| **`std/simd.zc`** | Tipos de vectores SIMD nativos. | [Docs](../docs/std/simd.md) |

</details>

---

## Herramientas

Zen C proporciona un Servidor de Lenguaje y un REPL integrados para mejorar la experiencia de desarrollo.

### Servidor de Lenguaje (LSP)

El Servidor de Lenguaje de Zen C (LSP) soporta las características estándar de LSP para integración con editores, proporcionando:

*   **Ir a la Definición**
*   **Encontrar Referencias**
*   **Información al pasar el ratón (Hover)**
*   **Autocompletado** (Nombres de funciones/structs, autocompletado tras punto para métodos/campos)
*   **Símbolos del Documento** (Esquema)
*   **Ayuda de Firma**
*   **Diagnósticos** (Errores sintácticos/semánticos)

Para iniciar el servidor de lenguaje (normalmente configurado en los ajustes de LSP de tu editor):

```bash
zc-lsp
```

Se comunica mediante I/O estándar (JSON-RPC 2.0).

### REPL

El bucle Read-Eval-Print (REPL) le permite experimentar con código Zen C de forma interactiva utilizando la moderna **compilación JIT en proceso** (con tecnología LibTCC).

```bash
zc-repl
```

#### Características

*   **Ejecución JIT**: El código se compila en memoria y se ejecuta directamente dentro del proceso del REPL para una respuesta instantánea.

*   **Codificación Interactiva**: Escribe expresiones o sentencias para su evaluación inmediata.
*   **Historial Persistente**: Los comandos se guardan en `~/.zprep_history`.
*   **Script de Inicio**: Carga automáticamente comandos desde `~/.zprep_init.zc`.

#### Comandos

| Comando | Descripción |
|:---|:---|
| `:help` | Muestra los comandos disponibles. |
| `:reset` | Limpia el historial de la sesión actual (variables/funciones). |
| `:vars` | Muestra las variables activas. |
| `:funcs` | Muestra las funciones definidas por el usuario. |
| `:structs` | Muestra los structs definidos por el usuario. |
| `:imports` | Muestra las importaciones activas. |
| `:history` | Muestra el historial de entrada de la sesión. |
| `:type <expr>` | Muestra el tipo de una expresión. |
| `:c <stmt>` | Muestra el código C generado para una sentencia. |
| `:time <expr>` | Benchmark de una expresión (ejecuta 1000 iteraciones). |
| `:edit [n]` | Edita el comando `n` (por defecto: el último) en `$EDITOR`. |
| `:save <file>` | Guarda la sesión actual en un archivo `.zc`. |
| `:load <file>` | Carga y ejecuta un archivo `.zc` en la sesión. |
| `:watch <expr>` | Observa una expresión (se revalúa tras cada entrada). |
| `:unwatch <n>` | Elimina una observación. |
| `:undo` | Elimina el último comando de la sesión. |
| `:delete <n>` | Elimina el comando en el índice `n`. |
| `:clear` | Limpia la pantalla. |
| `:quit` | Sale del REPL. |
| `! <cmd>` | Ejecuta un comando de shell (ej. `!ls`). |

---


### Protocolo de Servidor de Lenguaje (LSP)

Zen C incluye un Servidor de Lenguaje integrado para la integración con editores.

- **[Guía de Instalación y Configuración](translations/LSP_ES.md)**
- **Editores Soportados**: VS Code, Neovim, Vim, Zed, y cualquier editor capaz de LSP.

Usa `zc-lsp` para iniciar el servidor.

### Depuración de Zen C

Los programas de Zen C se pueden depurar utilizando depuradores de C estándar como **LLDB** o **GDB**.

#### Visual Studio Code

Para obtener la mejor experiencia en VS Code, instale la [extensión oficial de Zen C](https://marketplace.visualstudio.com/items?itemName=Z-libs.zenc). Para la depuración, puede utilizar la extensión **C/C++** (de Microsoft) o **CodeLLDB**.

Agregue estas configuraciones a su directorio `.vscode` para habilitar la depuración con un solo clic:

**`tasks.json`** (Tarea de compilación):
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

## Soporte del Compilador y Compatibilidad

Zen C está diseñado para funcionar con la mayoría de los compiladores C11. Algunas características dependen de extensiones de GNU C, pero estas suelen funcionar en otros compiladores. Usa la flag `--cc` para cambiar de backend.

```bash
zc run app.zc --cc clang
zc run app.zc --cc zig
```

### Estado de la Suite de Pruebas

<details>
<summary>Click para ver detalles de Soporte del Compilador</summary>

| Compilador | Tasa de Acierto | Características Soportadas | Limitaciones Conocidas |
|:---|:---:|:---|:---|
| **GCC** | **100% (Completo)** | Todas las características | Ninguna. |
| **Clang** | **100% (Completo)** | Todas las características | Ninguna. |
| **Zig** | **100% (Completo)** | Todas las características | Ninguna. Usa `zig cc` como compilador C. |
| **TCC** | **98% (Alto)** | Estructuras, Genéricos, Traits, Coincidencia de Patrones | Sin ASM Intel, Sin `__attribute__((constructor))`. |

</details>

> [!WARNING]
> **ADVERTENCIA DE COMPILACIÓN:** Aunque **Zig CC** funciona excelentemente como backend para tus programas Zen C, compilar el *propio compilador Zen C* con el puede verificar pero producir un binario inestable que falla en las pruebas. Recomendamos compilar el compilador con **GCC** o **Clang** y usar Zig solo como backend para tu código operativo.


### Pruebas de Cumplimiento de MISRA C:2012

La suite de pruebas de Zen C incluye verificaciones según las directrices de MISRA C:2012.

> [!IMPORTANT]
> **Descargo de responsabilidad de MISRA**
> Este proyecto es completamente independiente y no tiene ninguna afiliación, respaldo oficial o conexión corporativa con MISRA (Motor Industry Software Reliability Association). 
> 
> Debido a restricciones estrictas de derechos de autor, los casos de prueba solo enumeran las directrices mediante sus identificadores numéricos y evitan publicar especificaciones internas. Se alienta a los usuarios que necesiten la documentación principal a adquirir los materiales de las directrices auténticos desde el [Portal oficial de MISRA](https://www.misra.org.uk/).

### Construyendo con Zig


El comando `zig cc` de Zig proporciona un reemplazo directo para GCC/Clang con un excelente soporte de compilación cruzada (cross-compilation). Para usar Zig:

```bash
# Compilar y ejecutar un programa Zen C con Zig
zc run app.zc --cc zig

# Construir el propio compilador Zen C con Zig
make zig
```

### Backends de Salida

Zen C soporta múltiples backends de salida mediante la flag `--backend`. Cada backend produce un formato de destino diferente:

| Backend | Flag | Extensión | Descripción |
|:---|:---|:---:|:---|
| **C** | `--backend c` | `.c` | Predeterminado — GNU C11 |
| **C++** | `--backend cpp` | `.cpp` | Compatible con C++11 (también disponible como `--cpp`) |
| **CUDA** | `--backend cuda` | `.cu` | NVIDIA CUDA C++ (también disponible como `--cuda`) |
| **Objective-C** | `--backend objc` | `.m` | Objective-C (también disponible como `--objc`) |
| **JSON** | `--backend json` | `.json` | AST legible por máquina para herramientas |
| **AST dump** | `--backend ast-dump` | `.ast` | Árbol AST legible por humanos (depuración) |
| **Lisp** | `--backend lisp` | `.lisp` | Transpilar a Common Lisp (`sbcl --script`) |
| **Graphviz** | `--backend dot` | `.dot` | Grafo AST visual (`dot -Tpng ast.dot -o ast.png`) |

Las opciones específicas del backend se pueden configurar con `--backend-opt`:

```bash
# Salida JSON con formato legible
zc transpile file.zc --backend json --backend-opt pretty

# Mostrar contenido completo sin truncar
zc transpile file.zc --backend lisp --backend-opt full-content

# O usar alias de conveniencia:
zc transpile file.zc --backend json --json-pretty
zc transpile file.zc --backend lisp --backend-full-content
```

Todas las opciones de backend se autodocumentan — las flags `--` desconocidas se verifican automáticamente contra los alias de backend registrados.

### Interop con C++

Zen C puede generar código compatible con C++ con la flag `--backend cpp` (`--cpp` para abreviar), permitiendo una integración perfecta con bibliotecas de C++.

```bash
# Compilación directa con g++
zc app.zc --backend cpp

# O transpilar para construcción manual
zc transpile app.zc --backend cpp
g++ out.cpp mi_lib_cpp.o -o app
```

#### Usando C++ en Zen C

Incluye cabeceras de C++ y usa bloques `raw` para el código C++:

```zc
include <vector>
include <iostream>

raw {
    std::vector<int> hacer_vec(int a, int b) {
        return {a, b};
    }
}

fn main() {
    let v = hacer_vec(1, 2);
    raw { std::cout << "Tamaño: " << v.size() << std::endl; }
}
```

> [!NOTE]
> La flag `--cpp` cambia el backend a `g++` y emite código compatible con C++ (usa `auto` en lugar de `__auto_type`, sobrecarga de funciones en lugar de `_Generic`, y casts explícitos para `void*`).

#### Interop con CUDA

Zen C soporta la programación de GPU transpilando a **CUDA C++** mediante la flag `--backend cuda` (`--cuda` para abreviar). Esto te permite aprovechar las potentes características de C++ (plantillas, constexpr) dentro de tus kernels mientras mantienes la sintaxis ergonómica de Zen C.

```bash
# Compilación directa con nvcc
zc run app.zc --backend cuda

# O transpilar para construcción manual
zc transpile app.zc --backend cuda -o app.cu
nvcc app.cu -o app
```

#### Atributos Específicos de CUDA

| Atributo | Equivalente CUDA | Descripción |
|:---|:---|:---|
| `@global` | `__global__` | Función de kernel (se ejecuta en GPU, se llama desde el host) |
| `@device` | `__device__` | Función de dispositivo (se ejecuta en GPU, se llama desde GPU) |
| `@host` | `__host__` | Función de host (explícitamente solo CPU) |

#### Sintaxis de Lanzamiento de Kernel

Zen C proporciona una sentencia `launch` limpia para invocar kernels de CUDA:

```zc
launch nombre_del_kernel(args) with {
    grid: num_bloques,
    block: hilos_por_bloque,
    shared_mem: 1024,  // Opcional
    stream: mi_stream   // Opcional
};
```

Esto se transpila a: `nombre_del_kernel<<<grid, bloque, compartido, stream>>>(args);`

#### Escribiendo Kernels de CUDA

Usa la sintaxis de funciones de Zen C con `@global` y la sentencia `launch`:

```zc
import "std/cuda.zc"

@global
fn kernel_suma(a: float*, b: float*, c: float*, n: int) {
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

    // ... inicialización de datos ...
    
    launch kernel_suma(d_a, d_b, d_c, N) with {
        grid: (N + 255) / 256,
        block: 256
    };
    
    cuda_sync();
}
```

#### Biblioteca Estándar (`std/cuda.zc`)
Zen C proporciona una biblioteca estándar para operaciones comunes de CUDA para reducir los bloques `raw`:

```zc
import "std/cuda.zc"

// Gestión de memoria
let d_ptr = cuda_alloc<float>(1024);
cuda_copy_to_device(d_ptr, h_ptr, 1024 * sizeof(float));
defer cuda_free(d_ptr);

// Sincronización
cuda_sync();

// Indexación de hilos (usar dentro de kernels)
let i = thread_id(); // Índice global
let bid = block_id();
let tid = local_id();
```


> [!NOTE]
> **Nota:** La flag `--cuda` establece `nvcc` como el compilador e implica el modo `--cpp`. Requiere el NVIDIA CUDA Toolkit.

### Soporte C23

Zen C soporta características modernas de C23 cuando se utiliza un compilador backend compatible (GCC 14+, Clang 14+).

- **`auto`**: Zen C mapea automáticamente la inferencia de tipos a `auto` estándar de C23 si `__STDC_VERSION__ >= 202300L`.
- **`_BitInt(N)`**: Use tipos `iN` y `uN` (ej. `i256`, `u12`, `i24`) para acceder a enteros de ancho arbitrario de C23.

### Interop con Objective-C

Zen C puede compilarse a Objective-C (`.m`) usando la flag `--backend objc` (`--objc` para abreviar), permitiéndote usar frameworks de Objective-C (como Cocoa/Foundation) y su sintaxis.

```bash
# Compilar con clang (o gcc/gnustep)
zc app.zc --backend objc --cc clang
```

#### Usando Objective-C en Zen C

Usa `include` para las cabeceras y bloques `raw` para la sintaxis de Objective-C (`@interface`, `[...]`, `@""`).

```zc
//> macos: framework: Foundation
//> linux: cflags: -fconstant-string-class=NSConstantString -D_NATIVE_OBJC_EXCEPTIONS
//> linux: link: -lgnustep-base -lobjc

include <Foundation/Foundation.h>

fn main() {
    raw {
        NSAutoreleasePool *pool = [[NSAutoreleasePool alloc] init];
        NSLog(@"¡Hola desde Objective-C!");
        [pool drain];
    }
    println "¡Zen C también funciona!";
}
```

> [!NOTE]
> **Nota:** La interpolación de cadenas de Zen C funciona con objetos de Objective-C (`id`) llamando a `debugDescription` o `description`.

### 18. Marco de Pruebas Unitarias

Zen C incluye un marco de pruebas integrado que permite escribir pruebas unitarias directamente en los archivos fuente utilizando la palabra clave `test`.

#### Sintaxis
Un bloque `test` contiene un nombre descriptivo y un cuerpo de código para ejecutar. Las pruebas no requieren una función `main` para ejecutarse.

```zc
test "unittest1" {
    "Esta es una prueba unitaria";

    let a = 3;
    assert(a > 0, "a debería ser un entero positivo");

    "unittest1 pasado.";
}
```

#### Ejecución de Pruebas
Para ejecutar todas las pruebas en un archivo, usa el comando `run`. El compilador detectará y ejecutará automáticamente todos los bloques `test` de nivel superior.

```bash
zc run mi_archivo.zc
```

#### Aserciones
Usa la función integrada `assert(condición, mensaje)` para verificar las expectativas. Si la condición es falsa, la prueba fallará y se imprimirá el mensaje proporcionado.

---

### API Pública (Incrustación)

Zen C se puede utilizar como biblioteca de C mediante los encabezados públicos en `src/public/*.h`. Estos encabezados compilan sin `-DZC_ALLOW_INTERNAL` y proporcionan una API estable para incrustar el compilador en tus propias herramientas:

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

**Compilar con:**

```bash
cc -I src/public -I src -I src/utils my_tool.c -o my_tool
```

**Después de instalar (`make install`):**

```bash
cc -I /usr/local/include/zenc my_tool.c -o my_tool
```

La API pública cubre:
- **`zc_core.h`** — Tipos `CompilerConfig`, `ZenCompiler`, `ASTNode`, `Type`, puntos de entrada del analizador, ayudantes de introspección de tipos
- **`zc_driver.h`** — `driver_run()`, `driver_compile()` (orquestación completa del pipeline)
- **`zc_codegen.h`** — `codegen_node()`, `emit_preamble()`, `format_expression_as_c()`
- **`zc_analysis.h`** — `check_program()`, `check_moves_only()`, `resolve_alias()`
- **`zc_diag.h`** — `zerror_at()`, `zwarn_at()`, `zpanic_at()`, informes de diagnóstico
- **`zc_utils.h`** — `Emitter` (búfer de salida), `load_file()`, `z_resolve_path()`

Instala con `sudo make install` para desplegar los encabezados, el binario, las páginas man y la biblioteca estándar.

---

## Contribuyendo

¡Damos la bienvenida a las contribuciones! Ya sea corrigiendo errores, añadiendo documentación o proponiendo nuevas características.

Por favor, consulta [CONTRIBUTING_ES.md](CONTRIBUTING_ES.md) para ver las guías detalladas sobre cómo contribuir, ejecutar pruebas y enviar pull requests.

---

## Seguridad

Para instrucciones sobre reportes de seguridad, por favor vea [SECURITY_ES.md](SECURITY_ES.md).

---

## Atribuciones

Este proyecto utiliza bibliotecas de terceros. Los textos completos de las licencias pueden encontrarse en el directorio `LICENSES/`.

*   **[cJSON](https://github.com/DaveGamble/cJSON)** (Licencia MIT): Usado para el parseo y generación de JSON en el Servidor de Lenguaje.
*   **[zc-ape](https://github.com/OEvgeny/zc-ape)** (Licencia MIT): El port original de Ejecutable Realmente Portable de Zen-C por [Eugene Olonov](https://github.com/OEvgeny).
*   **[Cosmopolitan Libc](https://github.com/jart/cosmopolitan)** (Licencia ISC): La biblioteca fundamental que hace posible APE.
*   **[TRE](https://github.com/laurikari/tre)** (Licencia BSD): Usado para el motor de expresiones regulares en la biblioteca estándar.
*   **[zenc.vim](https://github.com/zenc-lang/zenc.vim)** (Licencia MIT): El plugin oficial para Vim/Neovim, escrito principalmente por **[davidscholberg](https://github.com/davidscholberg)**.
*   **[TinyCC](https://github.com/TinyCC/tinycc)** (Licencia LGPL): El motor JIT fundamental utilizado para la evaluación de alto rendimiento del REPL.

---

<div align="center">
  <p>
    Copyright © 2026 Lenguaje de Programación Zen C.<br>
    Comienza tu viaje hoy.
  </p>
  <p>
    <a href="https://discord.com/invite/q6wEsCmkJP">Discord</a> •
    <a href="https://github.com/zenc-lang/zenc">GitHub</a> •
    <a href="https://github.com/zenc-lang/docs">Documentación</a> •
    <a href="https://github.com/zenc-lang/awesome-zenc">Ejemplos</a> •
    <a href="https://github.com/zenc-lang/rfcs">RFCs</a> •
    <a href="CONTRIBUTING_ES.md">Contribuir</a>
  </p>
</div>
