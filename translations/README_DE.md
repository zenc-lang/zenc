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
  <h3>Moderne Ergonomie. Null Overhead. Pures C.</h3>  
  <br>  
  <p>  
	<a href="#"><img src="https://img.shields.io/badge/build-passing-brightgreen" alt="Build Status"></a>  
	<a href="#"><img src="https://img.shields.io/badge/license-MIT-blue" alt="Lizenz"></a>  
	<a href="#"><img src="https://img.shields.io/github/v/release/zenc-lang/zenc?label=version&color=orange" alt="Version"></a>  
	<a href="#"><img src="https://img.shields.io/badge/platform-linux%20%7C%20windows%20%7C%20macos-lightgrey" alt="Plattform"></a>  
  </p>  
  <p><em>Schreiben wie in einer Hochsprache, ausführen wie in C.</em></p>  
</div>  

<hr>  

<div align="center">  
  <p>  
	<b><a href="#übersicht">Übersicht</a></b> •  
	<b><a href="#community">Community</a></b> •  
	<b><a href="#schnellstart">Schnellstart</a></b> •  
	<b><a href="#ökosystem">Ökosystem</a></b> •  
	<b><a href="#sprachreferenz">Sprachreferenz</a></b> •  
	<b><a href="#standardbibliothek">Standardbibliothek</a></b> •  
	<b><a href="#tooling">Tooling</a></b>  
  </p>  
</div>  

---

## Übersicht

**Zen C** ist eine moderne Systemprogrammiersprache, die zu menschenlesbarem `GNU C`/`C11` kompiliert. Es bietet einen reichhaltigen Funktionsumfang, darunter Typinferenz, Pattern Matching, Generics, Traits, Async/Await und manuelles Speichermanagement mit RAII-Fähigkeiten – und das alles bei 100%iger C-ABI-Kompatibilität.

## Community

Diskutiere mit, teile Demos, stelle Fragen oder melde Fehler auf dem offiziellen Zen C Discord-Server!

- Discord: [Hier beitreten](https://discord.com/invite/q6wEsCmkJP)
- RFCs: [Features vorschlagen](https://github.com/zenc-lang/rfcs)

## Ökosystem

Das Zen C-Projekt besteht aus mehreren Repositories:

| Repository | Beschreibung | Status |
| :--- | :--- | :--- |
| **[zenc](https://github.com/zenc-lang/zenc)** | Der Kern-Compiler (zc), CLI und Standardbibliothek. | Aktive Entwicklung |
| **[docs](https://github.com/zenc-lang/docs)** | Offizielle Dokumentation und Spezifikation. | Aktiv |
| **[rfcs](https://github.com/zenc-lang/rfcs)** | Request for Comments (RFCs). Gestalte die Zukunft mit. | Aktiv |
| **[vscode-zenc](https://github.com/zenc-lang/vscode-zenc)** | Offizielle VS Code Erweiterung. | Alpha |
| **[www](https://github.com/zenc-lang/www)** | Quellcode für zenc-lang.org.| Aktiv |
| **[awesome-zenc](https://github.com/zenc-lang/awesome-zenc)** | Eine sorgfältig zusammengestellte Liste großartiger Zen C-Beispiele. | Wachsend |
| **[zenc.vim](https://github.com/zenc-lang/zenc.vim)** | Offizielles Vim/Neovim-Plugin (Syntax, Einrückung). | Aktiv |

## Showcase

Projekte, die mit Zen C erstellt wurden:

- **[ZC-pong-3ds](https://github.com/5quirre1/ZC-pong-3ds)**: Ein Pong-Klon für den Nintendo 3DS.
- **[zen-c-parin](https://github.com/Kapendev/zen-c-parin)**: Ein einfaches Beispiel mit Zen C und Parin.
- **[almond](https://git.sr.ht/~leanghok/almond)**: Ein minimaler Webbrowser in Zen C.

---

## Index

<table align="center">  
<tr>  
	<th width="50%">Allgemeines</th>  
	<th width="50%">Sprachreferenz</th>  
  </tr>  
  <tr>  
	<td valign="top">  
	  <ul>  
		<li><a href="#übersicht">Übersicht</a></li>  
		<li><a href="#community">Community</a></li>  
		<li><a href="#schnellstart">Schnellstart</a></li>  
		<li><a href="#ökosystem">Ökosystem</a></li>  
		<li><a href="https://github.com/zenc-lang/docs">Documentation</a></li>
		<li><a href="#standardbibliothek">Standardbibliothek</a></li>  
		<li><a href="#tooling">Tooling</a>
		  <ul>  
			 <li><a href="#language-server-protocol-lsp">LSP</a></li>
	        <li><a href="#debugging-zen-c">Debugging</a></li>
	      </ul>
	    </li>
	    <li><a href="#compilerunterstützung--kompatibilität">Compilerunterstützung & Kompatibilität</a></li>
	    <li><a href="#mitwirken">Mitwirken</a></li>
	    <li><a href="#quellenangaben">Quellenangaben</a></li>
	  </ul>
	</td>  
	<td valign="top">
      <p><a href="https://docs.zenc-lang.org/tour/"><b>Browse the Language Reference</b></a></p>  
	</td>  
</tr>  
</table>  

---

## Schnellstart

### Installation

```bash
git clone https://github.com/zenc-lang/zenc.git
cd zenc
make clean # Entferne alte Build-Dateien
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

### Windows

Zen C unterstützt Windows (x86_64) nativ. Nutze das Batch-Skript mit GCC (MinGW):

```cmd
build.bat
```

Dadurch wird der Compiler (`zc.exe`) erstellt. Netzwerk-, Dateisystem- und Prozessoperationen werden vollständig über die Plattformabstraktionsschicht (PAL) unterstützt.

Alternativ kannst du `make` verwenden, wenn du eine Unix-ähnliche Umgebung (MSYS2, Cygwin, git-bash) nutzt.

### Portable Binärdatei (APE)

Zen C kann als **Actually Portable Executable (APE)** mit [Cosmopolitan Libc](https://github.com/jart/cosmopolitan) kompiliert werden. Dies erzeugt eine einzige Datei (`.com`), die nativ auf Linux, macOS, Windows, FreeBSD, OpenBSD und NetBSD sowohl auf x86_64- als auch auf aarch64-Architekturen läuft.

**Voraussetzungen:**
- `cosmocc`-Toolchain (muss sich im PATH befinden)

**Build & Installation:**
```bash
make ape
sudo env "PATH=$PATH" make install-ape
```

**Artefakte:**
- `out/bin/zc.com`: Der portable Zen-C-Compiler. Enthält die Standardbibliothek in der ausführbaren Datei.
- `out/bin/zc-boot.com`: Ein eigenständiges Bootstrap-Installationsprogramm zum Einrichten neuer Zen-C-Projekte.

**Verwendung:**
```bash
# Läuft auf jedem unterstützten Betriebssystem
./out/bin/zc.com build hello.zc -o hello
```

### Modulare Erstellung

Zen C ist in optionale Module aufgeteilt. Verwende `ZC_*`-Flags zur Auswahl beim Bau:

| Flag | Standard | Ausgeschlossen |
|---|---|---|
| `ZC_LSP=0` | 1 | LSP-Server (~8 Dateien) |
| `ZC_REPL=0` | 1 | Interaktive REPL (~6 Dateien) |
| `ZC_PLUGINS=0` | 1 | Plugin-System |
| `ZC_ZEN=0` | 1 | `--doc` / `--facts` Modi |
| `ZC_BACKENDS=0` | 1 | Nicht-C-Backends (JSON, Lisp, usw.) |
| `ZC_TRE=0` | 1 | TRE-Regex-Bibliothek |

```bash
make                     # Alle Funktionen (3.3 MB)
make lite                # Ohne LSP, REPL, Zen (2.9 MB)
make core                # Nur Compiler (2.7 MB)
make minimal             # Minimal (2.6 MB)

# Eigene Auswahl:
make ZC_LSP=0 ZC_REPL=0  # LSP und REPL ausschließen
```

Deaktivierte Befehle zeigen eine klare Meldung statt abzustürzen: `zc-lsp` → "LSP support not included".

### Verwendung

```bash
# Kompilieren und Ausführen
zc run hello.zc

# Executable erstellen
zc build hello.zc -o hello

# Interaktive Shell
zc-repl

# Dokumentation (Rekursiv)
zc-doc main.zc

# Dokumentation (Einzelne Datei, ohne Prüfung)
zc-doc --no-recursive-doc main.zc

```

### Umgebungsvariablen

Du kannst `ZC_ROOT` setzen, um den Speicherort der Standardbibliothek anzugeben (Standardimporte wie `import "std/vec.zc"`). Dadurch kannst du `zc` aus jedem beliebigen Verzeichnis ausführen.

```bash
export ZC_ROOT=/path/to/zenc
```

---

## Sprachreferenz

Weitere Details finden Sie in der offiziellen [Sprachreferenz](https://docs.zenc-lang.org/tour/01-variables-constants/).

## Standardbibliothek

Zen C enthält eine Standardbibliothek (`std`), die grundlegende Funktionalität abdeckt.

[Zur Dokumentation der Standardbibliothek](docs/std/README.md)

### Wichtige Module

<details>
<summary>Klicke, um alle Standardbibliotheks-Module zu sehen</summary>

| Modul | Beschreibung | Docs |
| :--- | :--- | :--- |
| **`std/bigfloat.zc`** | Gleitkomma-Arithmetik mit beliebiger Genauigkeit. | [Docs](docs/std/bigfloat.md) |
| **`std/bigint.zc`** | Ganzzahlen mit beliebiger Genauigkeit `BigInt`. | [Docs](docs/std/bigint.md) |
| **`std/bits.zc`** | Niedrigstufige Bitoperationen (`rotl`, `rotr`). | [Docs](docs/std/bits.md) |
| **`std/complex.zc`** | Komplexe Zahlen `Complex`. | [Docs](docs/std/complex.md) |
| **`std/vec.zc`** | Dynamisches, wachsendes Array `Vec<T>`. | [Docs](docs/std/vec.md) |
| **`std/string.zc`** | Heap-allokierter `String` mit UTF-8 Unterstützung. | [Docs](docs/std/string.md) |
| **`std/queue.zc`** | FIFO-Warteschlange (Ringpuffer). | [Docs](docs/std/queue.md) |
| **`std/map.zc`** | Generische Hash-Map `Map<V>`. | [Docs](docs/std/map.md) |
| **`std/fs.zc`** | Dateisystemoperationen. | [Docs](docs/std/fs.md) |
| **`std/io.zc`** | Standard Ein-/Ausgabe (`print`/`println`). | [Docs](docs/std/io.md) |
| **`std/option.zc`** | Optionale Werte (`Some`/`None`). | [Docs](docs/std/option.md) |
| **`std/result.zc`** | Fehlerbehandlung (`Ok`/`Err`). | [Docs](docs/std/result.md) |
| **`std/path.zc`** | Plattformübergreifende Pfadmanipulation. | [Docs](docs/std/path.md) |
| **`std/env.zc`** | Prozess-Umgebungsvariablen. | [Docs](docs/std/env.md) |
| **`std/net/`** | TCP, UDP, HTTP, DNS, URL. | [Docs](docs/std/net.md) |
| **`std/thread.zc`** | Threads und Synchronisation. | [Docs](docs/std/thread.md) |
| **`std/time.zc`** | Zeitmessung und Sleep-Funktionen. | [Docs](docs/std/time.md) |
| **`std/json.zc`** | JSON Parsing und Serialisierung. | [Docs](docs/std/json.md) |
| **`std/stack.zc`** | LIFO-Stack `Stack<T>`. | [Docs](docs/std/stack.md) |
| **`std/set.zc`** | Generisches Hash-Set `Set<T>`. | [Docs](docs/std/set.md) |
| **`std/process.zc`** | Prozessausführung und Management. | [Docs](docs/std/process.md) |
| **`std/regex.zc`** | Reguläre Ausdrücke (TRE-basiert). | [Docs](docs/std/regex.md) |
| **`std/simd.zc`** | Native SIMD-Vektortypen. | [Docs](docs/std/simd.md) |

</details>

### 18. Unit-Testing-Framework

Zen C bietet ein eingebautes Test-Framework mit **Test-Isolation**, **benannter Ausgabe** und **nicht-fatalen Assertions**.

#### Syntax
Ein `test`-Block enthält einen beschreibenden Namen und einen Codeblock, der ausgeführt wird. Es wird keine `main`-Funktion benötigt.

```zc
test "beschreibender Name" {
    let a = 3;
    assert(a > 0, "a sollte positiv sein");
}
```

#### Tests ausführen
```bash
zc run my_file.zc
```

Die Ausgabe zeigt jeden Test mit Namen:
```
  TEST: beschreibender Name ... OK
  TEST: weiterer Test ... FEHLGESCHLAGEN

1 test(s) failed
```

#### Assertions
| Funktion | Verhalten |
|:---|:---|
| `assert(cond, msg)` | Zeichnet Fehler auf, fährt mit nächstem Test fort |
| `expect(cond, msg)` | Nicht-fatal — zeichnet Fehler auf, fährt im selben Test fort |

```zc
test "beispiel" {
    expect(ergebnis != null, "ergebnis sollte nicht null sein");
    expect(ergebnis.code == 200, "status sollte 200 sein");
}
```

#### Exit-Code
Das Binärprogramm beendet sich mit der Anzahl fehlgeschlagener Tests (0 = alle bestanden).

---

## Tooling

Zen C bietet einen eingebauten **Language Server** und eine REPL, um die Entwicklungsarbeit zu erleichtern. Außerdem kann Zen C mit LLDB oder GDB debuggt werden.

### Language Server (LSP)

Der Zen C Language Server unterstützt das Language Server Protocol (LSP) und bietet die typischen Editor-Funktionen:

* **Gehe zu Definition** (`Go to Definition`)
* **Finde Referenzen** (`Find References`)
* **Hover-Informationen**
* **Autovervollständigung** (Funktions-/Struct-Namen, Methoden/Felder via Punkt)
* **Dokumentstruktur** (`Document Symbols` / Outline)
* **Signatur-Hilfe**
* **Diagnosen** (Syntax- und Semantikfehler)

Starten des Sprachserver (normalerweise in den LSP-Einstellungen deinem Editors konfiguriert):

```bash
zc-lsp
```

Es kommuniziert über Standard I/O (JSON-RPC 2.0).

### REPL

Die Read-Eval-Print-Schleife (REPL) ermöglicht es Ihnen, mit Zen C-Code interaktiv zu experimentieren, unter Verwendung der modernen **In-Process-JIT-Kompilierung** (unterstützt von LibTCC).

```bash
zc-repl
```

#### Features

*   **JIT-Ausführung**: Der Code wird im Speicher kompiliert und direkt im REPL-Prozess ausgeführt, für blitzschnelles Feedback.

*   **Interaktives Coden**: Ausdrücke oder Statements sofort auswerten.
*   **Persistente Historie**: Befehle werden in `~/.zprep_history` gespeichert.
*   **Startup-Skript**: Lädt automatisch `~/.zprep_init.zc`.

#### Befehle

| Befehl | Beschreibung |
|:---|:---|
| `:help` | Zeigt alle verfügbaren Kommandos an |
| `:reset` | Löscht aktuelle Session-Historie (Variablen/Funktionen) |
| `:vars` | Zeigt aktive Variablen |
| `:funcs` | Zeigt benutzerdefinierte Funktionen |
| `:structs` | Zeigt benutzerdefinierte Structs |
| `:imports` | Zeigt aktive Importe |
| `:history` | Zeigt Session-Eingabeverlauf |
| `:type <expr>` | Zeigt den Typ eines Ausdrucks |
| `:c <stmt>` | Zeigt den generierten C-Code für ein Statement |
| `:time <expr>` | Benchmark eines Ausdrucks (1000 Iterationen) |
| `:edit [n]` | Bearbeite Befehl `n` im `$EDITOR` (Standard: letzter) |
| `:save <file>` | Speichert die aktuelle Session in einer `.zc` Datei |
| `:load <file>` | Lädt und führt eine `.zc` Datei in die Session aus |
| `:watch <expr>` | Beobachtet einen Ausdruck (automatisch nach jeder Eingabe aktualisiert) |
| `:unwatch <n>` | Entfernt einen Watch |
| `:undo` | Entfernt den letzten Befehl aus der Session |
| `:delete <n>` | Löscht Befehl an Index `n` |
| `:clear` | Bildschirm leeren |
| `:quit` | REPL beenden |
| `! <cmd>` | Führe Shell-Befehl aus (z.B. `!ls`) |

---

### Language Server Protocol (LSP)

Zen C enthält einen integrierten Sprachserver zur Editorintegration.

- **[Installations- und Einrichtungsanleitung](../docs/LSP.md)**
- **Unterstützte Editoren**: VS Code, Neovim, Vim ([zenc.vim](https://github.com/zenc-lang/zenc.vim)), Zed und alle LSP-fähigen Editoren.

Verwende `zc-lsp`, um den Server zu starten.

### Debugging Zen C

Zen C Programme können mit Standard-C-Debuggern wie **LLDB** oder **GDB** debuggt werden.

#### Visual Studio Code

Für eine optimale Benutzererfahrung in VS Code installiere die offizielle [Zen C-Erweiterung](https://marketplace.visualstudio.com/items?itemName=Z-libs.zenc). Verwende zum Debuggen die **C/C++**-Erweiterung (von Microsoft) oder die **CodeLLDB**-Erweiterung.

Füge diese Konfigurationen in den `.vscode`-Verzeichnis hinzu, um das Debuggen mit einem Klick zu aktivieren:

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

## Compilerunterstützung & Kompatibilität

Zen C ist so konzipiert, dass es mit den meisten **C11-Compilern** funktioniert. Einige Features basieren auf **GNU-C-Erweiterungen**, funktionieren aber oft auch in anderen Compilern. Mit dem `--cc`-Flag kannst du das Backend wechseln.

```bash
zc run app.zc --cc clang
zc run app.zc --cc zig
```

### Status der Test-Suite

<details>
<summary>Klicke, um Compiler-Support-Details anzuzeigen</summary>

| Compiler | Erfolgsrate | Unterstützte Features | Bekannte Einschränkungen |
|:---|:---:|:---|:---|
| **GCC** | **100 % (Vollständig)** | Alle Features | Keine |
| **Clang** | **100 % (Vollständig)** | Alle Features | Keine |
| **Zig** | **100 % (Vollständig)** | Alle Features | Keine. Nutzt `zig cc` als Drop-in-C-Compiler |
| **TCC** | **98 % (Hoch)** | Structs, Generics, Traits, Pattern Matching | Kein Intel-ASM, kein `__attribute__((constructor))` |

</details>

> [!WARNING]
> **COMPILER BUILD WARNING:** Obwohl **Zig CC** hervorragend als Backend für Zen C Programme funktioniert, kann das **Bauen des Zen C Compilers selbst** damit zwar erfolgreich verifizieren, aber instabile Binaries erzeugen, die Tests nicht bestehen. Empfehlung: Den Compiler selbst mit **GCC** oder **Clang** bauen und Zig nur als Backend für Produktionscode verwenden.


### MISRA C:2012 Konformitätstests

Die Zen C-Testsuite enthält Verifizierungen gemäß den MISRA C:2012-Richtlinien.

> [!IMPORTANT]
> **MISRA-Haftungsausschluss**
> Dieses Projekt ist völlig unabhängig und steht in keiner Verbindung, offiziellen Unterstützung oder geschäftlichen Beziehung zu MISRA (Motor Industry Software Reliability Association). 
> 
> Aufgrund strenger urheberrechtlicher Beschränkungen listen Testfälle Richtlinien nur anhand ihrer numerischen Identifikatoren auf und vermeiden die Veröffentlichung interner Spezifikationen. Benutzer, die die Primärdokumentation benötigen, werden gebeten, authentische Richtlinienmaterialien über das [offizielle MISRA-Portal](https://www.misra.org.uk/) zu erwerben.

### Build mit Zig


Zigs `zig cc` dient als Drop-in-Ersatz für GCC/Clang mit exzellenter Cross-Compilation-Unterstützung. Um Zig zu verwenden:

```bash
# Zen C Programm mit Zig kompilieren und ausführen
zc run app.zc --cc zig

# Den Zen C Compiler selbst mit Zig bauen
make zig
```

### Ausgabe-Backends

Zen C unterstützt mehrere Ausgabe-Backends über die `--backend`-Flagge. Jedes Backend erzeugt ein anderes Zielformat:

| Backend | Flag | Erweiterung | Beschreibung |
|:---|:---|:---:|:---|
| **C** | `--backend c` | `.c` | Standard — GNU C11 |
| **C++** | `--backend cpp` | `.cpp` | C++11-kompatibel (auch verfügbar als `--cpp`) |
| **CUDA** | `--backend cuda` | `.cu` | NVIDIA CUDA C++ (auch verfügbar als `--cuda`) |
| **Objective-C** | `--backend objc` | `.m` | Objective-C (auch verfügbar als `--objc`) |
| **JSON** | `--backend json` | `.json` | Maschinenlesbares AST für Werkzeuge |
| **AST dump** | `--backend ast-dump` | `.ast` | Menschenlesbarer AST-Baum (Debugging) |
| **Lisp** | `--backend lisp` | `.lisp` | Nach Common Lisp transpilieren (`sbcl --script`) |
| **Graphviz** | `--backend dot` | `.dot` | Visueller AST-Graph (`dot -Tpng ast.dot -o ast.png`) |

Backend-spezifische Optionen können mit `--backend-opt` gesetzt werden:

```bash
# JSON-Ausgabe mit Einrückung
zc transpile file.zc --backend json --backend-opt pretty

# Vollständigen Rohinhalt anzeigen (ohne Kürzung)
zc transpile file.zc --backend lisp --backend-opt full-content

# Oder praktische Aliase verwenden:
zc transpile file.zc --backend json --json-pretty
zc transpile file.zc --backend lisp --backend-full-content
```

Alle Backend-Optionen sind selbstdokumentierend — unbekannte `--`-Flags werden automatisch gegen registrierte Backend-Aliase geprüft.

### C++-Interoperabilität

Zen C kann mit dem `--backend cpp`-Flag (`--cpp` kurz) C++-kompatiblen Code generieren und dadurch nahtlos mit C++-Bibliotheken interagieren.

```bash
# Direkte Kompilierung mit g++
zc app.zc --backend cpp

# Oder transpilen und manuell bauen
zc transpile app.zc --backend cpp
g++ out.cpp my_cpp_lib.o -o app
```

#### Verwendung von C++ in Zen C

C++-Header einbinden und raw-Blöcke für nativen C++-Code verwenden:

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
> Das `--cpp`-Flag wechselt auf `g++` als Backend und erzeugt C++-kompatiblen Code (`auto` statt `__auto_type`, Overloads statt `_Generic`, explizite `void*`-Casts).

### CUDA-Interoperabilität

Zen C unterstützt GPU-Programmierung durch Transpilieren nach **CUDA C++** über die `--backend cuda`-Flagge (`--cuda` kurz). Dies ermöglicht die Nutzung der leistungsstarken C++-Funktionen (Templates, constexpr) in Ihren Kernels, während die ergonomische Zen-C-Syntax erhalten bleibt.

```bash
# Direkte Kompilierung mit nvcc
zc run app.zc --backend cuda

# Oder transpilen für manuelles Bauen
zc transpile app.zc --backend cuda -o app.cu
nvcc app.cu -o app
```

#### CUDA-spezifische Attribute

| Attribut | CUDA-Äquivalent | Beschreibung |
|:---|:---|:---|
| `@global` | `__global__` | Kernel-Funktion (läuft auf GPU, wird vom Host aufgerufen) |
| `@device` | `__device__` | Device-Funktion (läuft auf GPU, wird von GPU aufgerufen) |
| `@host` | `__host__` | Host-Funktion (explizit CPU-only) |

#### Kernel-Launch-Syntax

Zen C bietet ein sauberes `launch`-Statement zum Aufruf von CUDA-Kernels:

```zc
launch kernel_name(args) with {
    grid: num_blocks,
    block: threads_per_block,
    shared_mem: 1024,  // Optional
    stream: my_stream   // Optional
};
```

This transpiles to: `kernel_name<<<grid, block, shared, stream>>>(args);`

#### Schreiben von CUDA-Kernels

Verwende normale Zen C Funktionen mit `@global` und `launch`:

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

    // ... Daten initialisieren ...
    
    launch add_kernel(d_a, d_b, d_c, N) with {
        grid: (N + 255) / 256,
        block: 256
    };
    
    cuda_sync();
}
```

#### Standardbibliothek (`std/cuda.zc`)
Zen C stellt eine Standardbibliothek für gängige CUDA-Operationen zur Verfügung, um `raw`-Blöcke zu reduzieren:

```zc
import "std/cuda.zc"

// Speicherverwaltung
let d_ptr = cuda_alloc<float>(1024);
cuda_copy_to_device(d_ptr, h_ptr, 1024 * sizeof(float));
defer cuda_free(d_ptr);

// Synchronisation
cuda_sync();

// Thread-Indizes (innerhalb von Kernels)
let i = thread_id(); // Globaler Index
let bid = block_id();
let tid = local_id();
```

> [!NOTE]  
> **Hinweis:** Das `--cuda`-Flag setzt automatisch `nvcc` als Compiler und aktiviert implizit `--cpp`. Setzt NVIDIA CUDA Toolkit voraus.

### C23-Unterstützung

Zen C unterstützt moderne **C23-Features**, wenn ein kompatibler Backend-Compiler verwendet wird  
(GCC 14+, Clang 14+, TCC (teilweise)).

- **`auto`**: Zen C bildet Typinferenz automatisch auf das standardisierte C23-`auto` ab, wenn `__STDC_VERSION__ >= 202300L`.
- **`_BitInt(N)`**: Verwende `iN`- und `uN`-Typen (z. B. `i256`, `u12`, `i24`), um auf Ganzzahlen mit beliebiger Bitbreite aus C23 zuzugreifen.

### Objective-C-Interoperabilität

Zen C kann mit dem `--backend objc`-Flag (`--objc` kurz) nach **Objective-C (`.m`)** kompilieren, sodass Objective-C-Frameworks (wie Cocoa/Foundation) und deren Syntax direkt genutzt werden können.

```bash
# Kompilieren mit clang (oder gcc/gnustep)
zc app.zc --backend objc --cc clang
```

#### Verwendung von Objective-C in Zen C

Verwende `include` für Header und `raw`-Blöcke für Objective-C-Syntax (`@interface`, `[...]`, `@""`).

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
    println "Zen C funktioniert ebenfalls!";
}
```

> [!NOTE]  
> **Hinweis:** Zen C String-Interpolation funktioniert auch mit Objective-C-Objekten (`id`), indem automatisch `debugDescription` oder `description` aufgerufen wird.

---

### Öffentliche API (Einbettung)

Zen C kann als C-Bibliothek über die öffentlichen Header in `src/public/*.h` verwendet werden. Diese Header kompilieren ohne `-DZC_ALLOW_INTERNAL` und bieten eine stabile API zum Einbetten des Compilers in eigene Werkzeuge:

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

**Kompilieren mit:**

```bash
cc -I src/public -I src -I src/utils my_tool.c -o my_tool
```

**Nach der Installation (`make install`):**

```bash
cc -I /usr/local/include/zenc my_tool.c -o my_tool
```

Die öffentliche API umfasst:
- **`zc_core.h`** — `CompilerConfig`, `ZenCompiler`, `ASTNode`, `Type`-Typen, Parser-Einstiegspunkte, Typrüfungshilfen
- **`zc_driver.h`** — `driver_run()`, `driver_compile()` (vollständige Pipeline-Orchestrierung)
- **`zc_codegen.h`** — `codegen_node()`, `emit_preamble()`, `format_expression_as_c()`
- **`zc_analysis.h`** — `check_program()`, `check_moves_only()`, `resolve_alias()`
- **`zc_diag.h`** — `zerror_at()`, `zwarn_at()`, `zpanic_at()`, Diagnoseberichte
- **`zc_utils.h`** — `Emitter` (Ausgabepuffer), `load_file()`, `z_resolve_path()`

Installiere mit `sudo make install`, um Header, Binärdatei, Handbuchseiten und die Standardbibliothek bereitzustellen.

---

## Mitwirken

Wir freuen uns über Beiträge!  
Egal ob Bugfixes, Dokumentation oder neue Sprachfeatures.

Siehe [CONTRIBUTING.md](4%20CONTRIBUTING_EN.md) für detaillierte Richtlinien zum Mitwirken, Testen und Einreichen von Pull Requests.

---

## Sicherheit

Hinweise zum Melden von Sicherheitslücken findest du in [SECURITY.md](5%20SECURITY_EN.md).

---

## Quellenangaben

Dieses Projekt verwendet Bibliotheken von Drittanbietern. Die vollständigen Lizenztexte befinden sich im Verzeichnis `LICENSES/`.

*   **[cJSON](https://github.com/DaveGamble/cJSON)** (MIT-Lizenz): Wird für JSON-Parsing und -Generierung im Language Server verwendet.
*   **[zc-ape](https://github.com/OEvgeny/zc-ape)** (MIT-Lizenz): Der ursprüngliche Actually Portable Executable Port von Zen C von **[Eugene Olonov](https://github.com/OEvgeny)**.
*   **[Cosmopolitan Libc](https://github.com/jart/cosmopolitan)** (ISC-Lizenz): Die zugrunde liegende Bibliothek, die APE ermöglicht.
*   **[TRE](https://github.com/laurikari/tre)** (BSD-Lizenz): Wird für die Regex-Engine der Standardbibliothek verwendet.
*   **[zenc.vim](https://github.com/zenc-lang/zenc.vim)** (MIT-Lizenz): Das offizielle Vim/Neovim-Plugin, hauptsächlich entwickelt von **[davidscholberg](https://github.com/davidscholberg)**.
*   **[TinyCC](https://github.com/TinyCC/tinycc)** (LGPL-Lizenz): Die fundamentale JIT-Engine, die für die Hochleistungs-REPL-Auswertung verwendet wird.

---

<div align="center">
  <p>
    Copyright © 2026 Zen C Programmiersprache.<br>
    Starte deine Reise noch heute.
  </p>
  <p>
    <a href="https://discord.com/invite/q6wEsCmkJP">Discord</a> •
    <a href="https://github.com/zenc-lang/zenc">GitHub</a> •
    <a href="https://github.com/zenc-lang/docs">Dokumentation</a> •
    <a href="https://github.com/zenc-lang/awesome-zenc">Beispiele</a> •
    <a href="https://github.com/zenc-lang/rfcs">RFCs</a> •
    <a href="CONTRIBUTING_DE.md">Mitwirken</a>
  </p>
</div>
