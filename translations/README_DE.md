<div align="center">  
  <p>  
	<a href="../README.md">English</a> •  
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
	  <ul>  
		<li><a href="#1-variablen-und-konstanten">1. Variablen & Konstanten</a></li>  
		<li><a href="#2-primitive-typen">2. Primitive Typen</a>
		 <ul>
			 <li><a href="#unicode-und-runen">Unicode & Runen</a></li>
		 </ul>
		</li>
		<li><a href="#3-zusammengesetzte-typen">3. Zusammengesetzte Typen</a></li>  
		<li><a href="#4-funktionen--lambdas">4. Funktionen & Lambdas</a></li>  
		<li><a href="#5-kontrollfluss">5. Kontrollfluss</a></li>  
		<li><a href="#6-operatoren">6. Operatoren</a></li>  
		<li><a href="#7-ausgabe-und-string-interpolation">7. Ausgabe & String-Interpolation</a></li>  
		<li><a href="#8-speicherverwaltung">8. Speicherverwaltung</a></li>  
		<li><a href="#9-objektorientierte-programmierung-oop">9. OOP</a></li>
		<li><a href="#10-generische-typen">10. Generische Typen</a></li>  
		<li><a href="#11-nebenläufigkeit-asyncawait">11. Nebenläufigkeit</a></li>  
		<li><a href="#12-metaprogrammierung">12. Metaprogrammierung</a></li>  
		<li><a href="#13-attribute">13. Attribute</a></li>
        <li><a href="#14-inline-assembly">14. Inline-Assembler</a></li>
        <li><a href="#15-build-direktiven">15. Build-Direktiven</a></li>
        <li><a href="#16-schlüsselwörter">16. Schlüsselwörter</a></li>
		<li><a href="#17-c-interoperabilität">17. C-Interoperabilität</a></li>  
		<li><a href="#18-unit-testing-framework">18. Unit-Tests</a></li>  
	  </ul>  
	</td>  
</tr>  
</table>  

---

## Schnellstart

### Installation

```bash
git clone https://github.com/zenc-lang/zenc.git
cd Zen-C
make clean # Entferne alte Build-Dateien
make
sudo make install
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

### Verwendung

```bash
# Kompilieren und Ausführen
zc run hello.zc

# Executable erstellen
zc build hello.zc -o hello

# Interaktive Shell
zc repl

# Zen-Fakten zeigen
zc build hello.zc --zen
```

### Umgebungsvariablen

Du kannst `ZC_ROOT` setzen, um den Speicherort der Standardbibliothek anzugeben (Standardimporte wie `import "std/vec.zc"`). Dadurch kannst du `zc` aus jedem beliebigen Verzeichnis ausführen.

```bash
export ZC_ROOT=/path/to/Zen-C
```

---

## Sprachreferenz

### 1. Variablen und Konstanten

Zen C unterscheidet zwischen Compile-Zeit-Konstanten und Laufzeit-Variablen.

#### Manifeste Konstanten (`def`)

Werte, die nur zur Kompilierzeit existieren (im Code enthalten sind). Verwende diese für Arraygrößen, feste Konfigurationen und magische Zahlen.

``` zc
def MAX_SIZE = 1024;
let buffer: char[MAX_SIZE]; // Gültige Arraygröße
```

#### Variablen (`let`)
Speicherorte im Arbeitsspeicher. Können veränderlich oder schreibgeschützt (`const`) sein.

```zc
let x = 10;             // Veränderlich
x = 20;                 // OK

let y: const int = 10;  // Schreibgeschützt (Typqualifiziert)
// y = 20;              // Error: kann nicht einer Konstante zugewiesen werden
```

> [!TIP]  
> **Typinferenz**: Zen C leitet automatisch Datentypen für initialisierte Variablen ab. Auf unterstützten Compilern wird der Datentyp zu C23 `auto` kompiliert, andernfalls zur GCC-Erweiterung `__auto_type`.

### 2. Primitive Typen

| Typ | C-Äquivalent | Beschreibung |
|:---|:---|:---|
| `int`, `uint` | `int32_t`, `uint32_t` | 32-Bit vorzeichenbehaftete/vorzeichenlose Ganzzahl |
| `c_char`, `c_uchar` | `char`, `unsigned char` | C-`char` / `unsigned char` (Interoperabilität) |
| `c_short`, `c_ushort` | `short`, `unsigned short` | C-`short` / `unsigned short` (Interoperabilität) |
| `c_int`, `c_uint` | `int`, `unsigned int` | C-`int` / `unsigned int` (Interoperabilität) |
| `c_long`, `c_ulong` | `long`, `unsigned long` | C-`long` / `unsigned long` (Interoperabilität) |
| `c_long_long`, `c_ulong_long` | `long long`, `unsigned long long` | C-`long long` / `unsigned long long` (Interoperabilität) |
| `I8` .. `I128` oder `i8` .. `i128` | `int8_t` .. `__int128_t` | Vorzeichenbehaftete Ganzzahlen mit fester Bitbreite |
| `U8` .. `U128` oder `u8` .. `u128` | `uint8_t` .. `__uint128_t` | Vorzeichenlose Ganzzahlen mit fester Bitbreite |
| `isize`, `usize` | `ptrdiff_t`, `size_t` | Ganzzahlen mit Zeigergröße |
| `byte` | `uint8_t` | Alias für `U8` |
| `F32`, `F64` oder `f32`, `f64` | `float`, `double` | Gleitkommazahlen |
| `bool` | `bool` | `true` oder `false` |
| `char` | `char` | Einzelnes Zeichen |
| `string` | `char*` | C-String (nullterminiert) |
| `U0`, `u0`, `void` | `void` | Leerer Typ |
| `iN` (z. B. `i256`) | `_BitInt(N)` | Ganzzahl mit beliebiger Bitbreite (vorzeichenbehaftet, C23) |
| `uN` (z. B. `u42`) | `unsigned _BitInt(N)` | Ganzzahl mit beliebiger Bitbreite (vorzeichenlos, C23) |
| `rune` | `uint32_t` | Unicode-Skalarwert (UTF-32-Codepunkt) |

#### Literale
- **Ganzzahlen**: Dezimal (`123`), Hexadezimal (`0xFF`), Oktal (`0o755`), Binär (`0b1011`).
- *Hinweis*: Zahlen mit führenden Nullen werden als Dezimalzahlen behandelt (`0123` ist `123`), anders als in C.
- *Hinweis*: Zahlen können zur besseren Lesbarkeit Unterstriche enthalten (`1_000_000`, `0b_1111_0000`).
- **Gleitkommazahlen**: Standard (`3.14`), Wissenschaftlich (`1e-5`, `1.2E3`). Gleitkommazahlen unterstützen auch Unterstriche (`3_14.15_92`).

#### Unicode und Runen

Zen C bietet erstklassige Unterstützung für Unicode-Skalarwerte über den Typ `rune`. Eine `rune` repräsentiert einen einzelnen Unicode-Codepunkt (kodiert als 32-Bit-Ganzzahl ohne Vorzeichen).

| Literal | Beschreibung |
|:---|:---|
| `'a'` | Standard-ASCII-Zeichen |
| `'🚀'` | Mehrbyte-Unicode-Zeichen |
| `'\u{2764}'` | Unicode-Escape-Sequenz (Hex) |

```zc
import "std.zc"

fn main() {
    let c = 'a';
    println "Das Zeichen '{c}' hat den Code {(int)c} in ASCII/Unicode";

    let code = 97;
    println "Der Code {code} entspricht dem Zeichen {(char)code}";

    let r: rune = '🚀';
    println "Die Rune '{r}' hat den Unicode-Code {(uint)r}";
    
    let r_code: uint = 128640;
    println "Der Code {r_code} entspricht der Rune '{(rune)r_code}'";

    let r_esc: rune = '\u{2764}';
    println "Die Rune '{r_esc}' hat den Code {(uint)r_esc} (0x{(uint)r_esc:X})";
}
```

>[!IMPORTANT]
> **Bewährte Vorgehensweisen für portablen Code**
> 
> - Verwende **portable Datentypen** (`int`, `uint`, `i64`, `u8` usw.) für die gesamte reine Zen-C-Logik. `int` ist auf allen Architekturen garantiert 32-Bit-Datentypen mit Vorzeichen.
> - Verwende **C-Interop-Datentypen** (`c_int`, `c_char`, `c_long`, `c_ulong`, `c_long_long`, `c_ulong_long`) **nur** bei der Interaktion mit C-Bibliotheken (FFI). Deren Größe variiert je nach Plattform und C-Compiler (z. B. unterscheidet sich die Größe von `c_long` zwischen Windows und Linux).
> - Verwende `isize` und `usize` für Array-Indizierung und Speicherzeigerarithmetik.

### 3. Zusammengesetzte Typen

#### Arrays
Festgrößen-Arrays mit Wertsemantik.
```
def SIZE = 5;
let ints: int[SIZE] = [1, 2, 3, 4, 5];
let zeros: [int; SIZE]; // Nullinitialisiert
```

#### Tupel
Mehrere Werte gruppieren, Elemente über den Index aufrufen.
```zc
let pair = (1, "Hallo");
let x = pair.0;  // 1
let s = pair.1;  // "Hallo"
```

**Mehrere Rückgabewerte**

Funktionen können Tupel zurückgeben, um mehrere Ergebnisse zu liefern:
```zc
fn addiere_und_subtrahiere(a: int, b: int) -> (int, int) {
    return (a + b, a - b);
}

let ergebnis = addiere_und_subtrahiere(3, 2);
let summe = ergebnis.0;        // 5
let differenz = ergebnis.1;    // 1
```

**Destrukturierung**

Tupel können direkt in Variablen zerlegt werden:
```zc
let (summe, differenz) = addiere_und_subtrahiere(3, 2);
// summe = 5, differenz = 1
```

Typisierte Tupel-Destrukturierung ermöglicht explizite Typannotationen:
```zc
let (a: string, b: u8) = ("Hallo", 42);
let (x, y: i32) = (1, 2);  // Gemischt: x abgeleitet, y explizit
```

#### Structs
Datenstrukturen mit optionalen Bitfeldern.
```
struct Punkt {
    x: int;
    y: int;
}

// Strukturinitialisierung
let p = Punkt { x: 10, y: 20 };

// Bitfelder
struct Flags {
    valid: U8 : 1;
    mode:  U8 : 3;
}
```

> [!IMPORTANT]
> Strukturen verwenden standardmäßig die [Move-Semantik](#move-semantics--copy-safety). Felder können auch über `.` auf Zeiger zugegriffen werden (automatische Dereferenzierung).

#### Opake Strukturen
Du kannst eine Struktur als `opaque` definieren, um den Zugriff auf ihre Felder nur auf das definierende Modul zu beschränken, während die Struktur weiterhin auf dem Stack alloziert werden kann (die Größe ist bekannt).

```zc
// In user.zc
opaque struct Benutzer {
    id: int;
    name: string;
}

fn neuer_benutzer(name: string) -> Benutzer {
    return Benutzer{id: 1, name: name}; // OK: innerhalb des Moduls
}

// In main.zc
import "user.zc";

fn main() {
    let u = neuer_benutzer("Alice");
    // let id = u.id; // Fehler: Kein Zugriff auf privates Feld 'id'
}
```

#### Enums
Markierte Vereinigungen (Summentypen), die Daten speichern können.
```zc
enum Form {
    Kreis(float),       // speichert Radius
    Rechteck(float, float), // speichert Breite und Höhe
    Punkt               // keine Daten
}
```

#### Unions
Standard-C-Unions (unsicherer Zugriff).
```zc
union Daten {
    i: int;
    f: float;
}
```

#### SIMD-Vektoren
Native SIMD-Vektortypen unter Verwendung der GCC-/Clang-Vektorerweiterungen. Annotiere eine Struktur mit `@vector(N)`, um einen Vektor mit N Elementen zu definieren.
```zc
import "std/simd.zc";

fn main() {
    let a = f32x4{v: 1.0};              // Broadcast: {1.0, 1.0, 1.0, 1.0}
    let b = f32x4{1.0, 2.0, 3.0, 4.0};  // Initialisierung pro Element
    let c = a + b;                       // elementweise Addition
    let x = c[0];                        // Elementzugriff (float)
}
```

Arithmetische (`+`, `-`, `*`, `/`) und bitweise (`&`, `|`, `^`) Operatoren arbeiten elementweise. Vordefinierte Typen findest du in [`std/simd.zc`](std/simd.zc).

#### Typ-Aliase
Erstelle einen neuen Namen für einen bestehenden Typ.
```zc
alias ID = int;
alias PunktMap = Map<string, Punkt>;
alias OpFunktion = fn(int, int) -> int;
```

#### Opake Typ-Aliase
Du kannst einen Typ-Alias als `opaque` definieren, um einen neuen Typ zu erstellen, der **außerhalb des definierenden Moduls** vom zugrunde liegenden Typ unterscheidbar ist.  
Dies bietet **starke Kapselung** und **Typensicherheit**, ohne den Laufzeit-Overhead einer Wrapper-Struktur.

```zc
// In library.zc
opaque alias Handle = int;

fn erstelle_handle(v: int) -> Handle {
    return v; // Implizite Konvertierung innerhalb des Moduls erlaubt
}

// In main.zc
import "library.zc";

fn main() {
    let h: Handle = erstelle_handle(42);
    // let i: int = h; // Fehler: Typprüfung fehlgeschlagen
    // let h2: Handle = 10; // Fehler: Typprüfung fehlgeschlagen
}
```

### 4. Funktionen & Lambdas

#### Funktionen
```zc
fn addiere(a: int, b: int) -> int {
    return a + b;
}

// Benannte Argumente werden in Aufrufen unterstützt
addiere(a: 10, b: 20);
```

> [!NOTE]
> Benannte Argumente müssen strikt der definierten Parameterreihenfolge folgen.  
> `addiere(b: 20, a: 10)` ist ungültig.

#### Const-Argumente
Funktionsargumente können als `const` markiert werden, um nur-lesende Semantik durchzusetzen.  
Dies ist ein Typqualifizierer, kein Konstantenwert.
```zc
fn drucke_wert(v: const int) {
    // v = 10; // Fehler: Zuweisung zu einer const-Variablen nicht erlaubt
    println "{v}";
}
```

#### Standardargumente
Funktionen können Standardwerte für nachgestellte Argumente definieren.  
Diese können Literale, Ausdrücke oder gültiger Zen C-Code sein (z. B. Struktur-Konstruktoren).
```zc
// Einfacher Standardwert
fn inkrement(val: int, menge: int = 1) -> int {
    return val + menge;
}

// Ausdruck als Standardwert (wird beim Aufruf ausgewertet)
fn versatz(val: int, pad: int = 10 * 2) -> int {
    return val + pad;
}

// Standardwert mit Struktur
struct Konfig { debug: bool; }
fn init(cfg: Konfig = Konfig { debug: true }) {
    if cfg.debug { println "Debug-Modus"; }
}

fn main() {
    inkrement(10);      // 11
    versatz(5);         // 25
    init();             // Gibt "Debug-Modus" aus
}
```

#### Lambdas (Closures)
Anonyme Funktionen, die ihre Umgebung erfassen können.
```zc
let faktor = 2;
let verdoppler = x -> x * faktor;  // Pfeil-Syntax
let komplett = fn(x: int) -> int { return x * faktor; }; // Block-Syntax

// Erfassen per Referenz (Block-Syntax)
let wert = 10;
let modifizieren = fn[&]() { wert += 1; }; 
modifizieren(); // wert ist jetzt 11

// Erfassen per Referenz (Pfeil-Syntax)
let modifizieren_pfeil = [&] x -> wert += x;
modifizieren_pfeil(5); // wert ist jetzt 16

// Erfassen per Referenz (Pfeil-Syntax mit mehreren Argumenten)
let summe_hinzu = [&] (a, b) -> wert += (a + b);
summe_hinzu(2, 2); // wert ist jetzt 20

// Erfassen per Wert (Standard)
let original = 100;
let implizit = x -> original + x;       // Implizites Erfassen per Wert (ohne Klammern)
let explizit = [=] x -> original + x;   // Explizites Erfassen per Wert
// let fehler = x -> original += x;     // Fehler: Zuweisung zu erfasstem Wert nicht erlaubt
```

#### Roh-Funktionszeiger
Zen C unterstützt Roh-Funktionszeiger wie in C über die `fn*`-Syntax.  
Dies ermöglicht eine nahtlose Interoperabilität mit C-Bibliotheken, die Funktionszeiger erwarten, ohne Closure-Overhead.

```zc
// Funktion, die einen Roh-Funktionszeiger als Parameter erhält
fn setze_callback(cb: fn*(int)) {
    cb(42);
}

// Funktion, die einen Roh-Funktionszeiger zurückgibt
fn hole_callback() -> fn*(int) {
    return mein_handler;
}

// Zeiger auf Funktionszeiger werden ebenfalls unterstützt (fn**)
let pptr: fn**(int) = &ptr;
```

#### Variadische Funktionen
Funktionen können eine variable Anzahl an Argumenten akzeptieren, indem `...` und der Typ `va_list` verwendet werden.
```zc
fn log(lvl: int, fmt: char*, ...) {
    let ap: va_list;
    va_start(ap, fmt);
    vprintf(fmt, ap); // Verwende C stdio
    va_end(ap);
}
```

### 5. Kontrollfluss

#### Bedingte Anweisungen
```zc
if x > 10 {
    print("Groß");
} else if x > 5 {
    print("Mittel");
} else {
    print("Klein");
}

// Ternäroperator
let y = x > 10 ? 1 : 0;

// If-Ausdruck (für komplexe Bedingungen)
let kategorie = if (x > 100) { "riesig" } else if (x > 10) { "groß" } else { "klein" };
```

#### Pattern Matching
Leistungsfähige Alternative zu `switch`.

```zc
match wert {
    1         => { print "Eins" },
    2 || 3    => { print "Zwei oder Drei" },    // ODER mit ||
    4 or 5    => { print "Vier oder Fünf" },    // ODER mit 'or'
    6, 7, 8   => { print "Sechs bis Acht" },    // ODER mit Komma
    10 .. 15  => { print "10 bis 14" },         // Exklusiver Bereich (Legacy)
    10 ..< 15 => { print "10 bis 14" },         // Exklusiver Bereich (explizit)
    20 ..= 25 => { print "20 bis 25" },         // Inklusiver Bereich
    _         => { print "Andere" },
}

// Destrukturierung von Enums
match form {
    Form::Kreis(r)       => { println "Radius: {r}" },
    Form::Rechteck(w, h) => { println "Fläche: {w*h}" },
    Form::Punkt          => { println "Punkt" },
}
```

#### Referenzbindung
Um einen Wert ohne Ownership-Übernahme zu inspizieren (kein Move), nutze das Schlüsselwort `ref` im Pattern.  
Wichtig für Typen, die Move-Semantik implementieren (z. B. `Option`, `Result`, Non-Copy-Strukturen).

```zc
let opt = Some(NonCopyVal{...});
match opt {
    Some(ref x) => {
        // 'x' ist ein Zeiger auf den Wert innerhalb von 'opt'
        // 'opt' wird hier NICHT verschoben oder verbraucht
        println "{x.feld}"; 
    },
    None => {}
}
```

#### Schleifen
```zc
// Bereich
for i in 0..10 { ... }      // Exklusiv (0 bis 9)
for i in 0..<10 { ... }     // Exklusiv (explizit)
for i in 0..=10 { ... }     // Inklusiv (0 bis 10)
for i in 0..10 step 2 { ... }
for i in 10..0 step -1 { ... }  // Absteigende Schleife

// Iterator (Vec oder eigene Iterable)
for element in vec { ... }

// Enumeriert: Index und Wert
for i, wert in arr { ... }           // i = 0, 1, 2, ...
for i, wert in 0..10 step 2 { ... }  // i = 0, 1, 2, ...; wert = 0, 2, 4, ...

// Direkte Iteration über Arrays fester Größe
let arr: int[5] = [1, 2, 3, 4, 5];
for wert in arr {
    // wert ist int
    println "{wert}";
}

// While-Schleife
while x < 10 { ... }

// Do-While-Schleife
do { ... } while x < 10;

// Endlosschleife mit Label
äußerer: loop {
    if erledigt { break äußerer; }
}

// Wiederholung N-mal
for _ in 0..5 { ... }
```

#### Erweiterter Kontrollfluss
```zc
// Guard: else ausführen und zurückkehren, falls Bedingung falsch
guard ptr != NULL else { return; }

// Unless: Ausführen, falls Bedingung nicht wahr
unless ist_gueltig { return; }
```

### 6. Operatoren

Zen C unterstützt Operatorüberladung für selbst definierte Strukturen, indem bestimmte Methodennamen implementiert werden.

#### Überladbare Operatoren

| Kategorie | Operator | Methodenname |
|:---|:---|:---|
| **Arithmetisch** | `+`, `-`, `*`, `/`, `%`, `**` | `add`, `sub`, `mul`, `div`, `rem`, `pow` |
| **Vergleich** | `==`, `!=` | `eq`, `neq` |
| | `<`, `>`, `<=`, `>=` | `lt`, `gt`, `le`, `ge` |
| **Bitweise** | `&`, `\|`, `^` | `bitand`, `bitor`, `bitxor` |
| | `<<`, `>>` | `shl`, `shr` |
| **Unary (einfach)** | `-` | `neg` |
| | `!` | `not` |
| | `~` | `bitnot` |
| **Indexzugriff** | `a[i]` | `get(a, i)` |
| | `a[i, j]` | `get(a, i, j)` |
| | `a[i] = v` | `set(a, i, v)` |

> **Hinweis zur String-Gleichheit**:
> - `string == string` führt einen **Wertvergleich** durch (entspricht `strcmp`).  
> - `char* == char*` führt einen **Zeigervergleich** durch (prüft Speicheradressen).  
> - Gemischte Vergleiche (z. B. `string == char*`) verwenden standardmäßig **Zeigervergleich**.

**Beispiel:**
```zc
impl Punkt {
    fn add(self, other: Punkt) -> Punkt {
        return Punkt{x: self.x + other.x, y: self.y + other.y};
    }
}

let p3 = p1 + p2; // Ruft p1.add(p2) auf
```

**Beispiel für Mehrfach-Index:**
```zc
struct Matrix {
    daten: int[9];
}

impl Matrix {
    fn get(self, zeile: int, spalte: int) -> int {
        return self.daten[zeile * 3 + spalte];
    }
}

let m = Matrix{daten: [1,0,0, 0,1,0, 0,0,1]};
let wert = m[1, 2]; // Ruft Matrix.get(m, 1, 2) auf
```

#### Syntaktischer Zucker

Diese Operatoren sind eingebaute Sprachfeatures und können nicht direkt überladen werden.

| Operator | Name | Beschreibung |
|:---|:---|:---|
| `\|>` | Pipeline | `x \|> f(y)` wird zu `f(x, y)` expandiert |
| `??` | Null-Koaleszenz | `wert ?? default` gibt `default` zurück, falls `wert` NULL ist (Zeiger) |
| `??=` | Null-Zuweisung | `wert ??= init` weist zu, falls `wert` NULL ist |
| `?.` | Safe Navigation | `ptr?.feld` greift nur auf `feld` zu, wenn `ptr` nicht NULL ist |
| `?` | Try-Operator | `res?` gibt einen Fehler zurück, falls vorhanden (Result/Option-Typen) |

**Auto-Dereferenzierung**:  
Der Zugriff auf Zeigerfelder (`ptr.feld`) und Methodenaufrufe (`ptr.methode()`) dereferenziert automatisch den Zeiger, entspricht `(*ptr).feld`.

### 7. Ausgabe und String-Interpolation

Zen C bietet vielseitige Optionen zur Konsolenausgabe, einschließlich Schlüsselwörtern und kurzen Schreibweisen.

#### Schlüsselwörter

| Schlüsselwort | Beschreibung |
|:---|:---|
| `print "..."` | Gibt auf `stdout` aus ohne nachgestellten Zeilenumbruch. |
| `println "..."` | Gibt auf `stdout` aus **mit** nachgestelltem Zeilenumbruch. |
| `eprint "..."` | Gibt auf `stderr` aus ohne nachgestellten Zeilenumbruch. |
| `eprintln "..."` | Gibt auf `stderr` aus **mit** nachgestelltem Zeilenumbruch. |

#### Kurze Schreibweisen

Zen C erlaubt, String-Literale direkt als Anweisungen zu verwenden:

| Syntax | Entspricht | Beschreibung |
|:---|:---|:---|
| `"Hz"` | `println "Hz"` | Gibt auf `stdout` aus, mit Zeilenumbruch. |
| `"Hz"..` | `print "Hz"` | Gibt auf `stdout` aus, ohne Zeilenumbruch. |
| `!"Err"` | `eprintln "Err"` | Gibt auf `stderr` aus, mit Zeilenumbruch. |
| `!"Err"..` | `eprint "Err"` | Gibt auf `stderr` aus, ohne Zeilenumbruch. |

#### String-Interpolation

Ausdrücke können direkt in String-Literalen mit `{}` eingebettet werden.  
Dies funktioniert für alle Druckmethoden und String-Kurzschreibweisen.

Die Interpolation in Zen C ist **implizit**: enthält ein String `{...}`, wird es automatisch als interpolierter String geparst. Du kannst auch explizit mit `f` prefixen (z. B. `f"..."`), um Interpolation zu erzwingen.

```zc
let x = 42;
let name = "Zen";
println "Wert: {x}, Name: {name}";
"Wert: {x}, Name: {name}"; // Kurzschreibweise für println
```

**Escape für geschweifte Klammern**:  `{{` erzeugt `{`, `}}` erzeugt `}`:

```zc
let json = "JSON: {{\"key\": \"value\"}}";
// Ausgabe: JSON: {"key": "value"}
```

**Raw-Strings**: Strings, bei denen Interpolation und Escape-Sequenzen komplett ignoriert werden, werden mit `r` prefixiert (z. B. `r"..."`):

```zc
let regex = r"\w+"; // Enthält exakt \ w +
let raw_json = r'{"key": "value"}'; // Kein Escapen von Klammern nötig
```

#### Mehrzeilige Strings

Zen C unterstützt rohe mehrzeilige Strings mit `"""`-Delimiter. Sehr nützlich für eingebettete Sprachen (GLSL, HTML) oder zum Generieren von C-Code in `comptime`-Blöcken ohne manuelles Escapen.

Wie normale Strings unterstützen mehrzeilige Strings **implizite Interpolation**. Man kann auch explizit prefixen:  
- `f"""..."""`: explizit interpolierter Stringblock  
- `r"""..."""`: explizit roher Stringblock (keine Interpolation, keine Escape-Sequenzen)

```zc
let prompt = """
  Bitte geben Sie Ihren Namen ein:
  Tippen Sie "exit", um abzubrechen.
""";

let welt = "Welt";
let script = """
  fn hallo() {
      println "Hallo, {welt}!";
  }
""";

let nur_raw = r"""
  Hier sind {klammern} einfach Text, und \n ist buchstäblich Slash-n.
""";
```

#### Eingabeaufforderungen (`?`)

Zen C unterstützt eine Kurzschreibweise für Benutzereingaben mit dem `?`-Präfix.

- `? "Prompt-Text"`: Gibt die Eingabeaufforderung aus (ohne Zeilenumbruch) und wartet auf Eingabe (liest eine Zeile).  
- `? "Alter eingeben: " (alter)`: Gibt Prompt aus und speichert die Eingabe in der Variablen `alter`.  
    - Format-Spezifizierer werden automatisch anhand des Variablentyps bestimmt.

```zc
let alter: int;
? "Wie alt bist du? " (alter);
println "Du bist {alter} Jahre alt.";
```

### 8. Speicherverwaltung

Zen C erlaubt manuelles Speichermanagement mit ergonomischen Hilfen.

#### Defer
Führt Code aus, wenn der aktuelle Scope verlassen wird. Defer-Statements werden in LIFO-Reihenfolge (Last-In, First-Out) ausgeführt.

```zc
let f = fopen("file.txt", "r");
defer fclose(f);
```

> [!WARNING]
> Um undefiniertes Verhalten zu vermeiden, sind Kontrollfluss-Anweisungen (`return`, `break`, `continue`, `goto`) **nicht erlaubt** innerhalb eines `defer`-Blocks.

#### Autofree
Gibt die Variable automatisch frei, wenn der Scope endet.

```zc
autofree let types = malloc(1024);
```

#### Ressourcen-Semantik (Move by Default)
Zen C behandelt Typen mit Destruktoren (wie `File`, `Vec` oder malloc'd Pointer) als **Ressourcen**.  
Um Double-Free-Fehler zu vermeiden, können Ressourcen nicht implizit dupliziert werden.

- **Move by Default**: Zuweisung einer Ressourcen-Variable überträgt die Eigentümerschaft. Die ursprüngliche Variable wird ungültig (Moved).  
- **Copy-Typen**: Typen ohne Destruktor können sich für `Copy` entscheiden, wodurch Zuweisung eine Kopie erzeugt.

**Diagnose & Philosophie**:  
Fehlermeldung „Use of moved value“ bedeutet: *„Dieser Typ besitzt eine Ressource (z. B. Speicher oder Handle) und blindes Kopieren ist unsicher.“*

> [!NOTE]  
> **Im Unterschied zu C/C++** dupliziert Zen C Ressourcen-Werte nicht automatisch.

**Funktionsargumente**:  
Werte, die an Funktionen übergeben werden, folgen denselben Regeln wie Zuweisung: Ressourcen werden bewegt, sofern sie nicht per Referenz übergeben werden.

```zc
fn process(r: Resource) { ... } // 'r' wird in die Funktion verschoben
fn peek(r: Resource*) { ... }   // 'r' wird geliehen (Referenz)
```

**Explizites Klonen**:  
Falls du *tatsächlich* zwei Kopien einer Ressource benötigst, gebe dies explizit an:

```zc
let b = a.clone(); // Ruft die 'clone'-Methode des Clone-Traits auf
```

**Opt-in Copy (Value Types)**: Für kleine Typen ohne Destruktor:

```zc
struct Point { x: int; y: int; }
impl Copy for Point {} // Erlaubt implizite Duplikation

fn main() {
    let p1 = Point { x: 1, y: 2 };
    let p2 = p1; // Kopiert. p1 bleibt gültig.
}
```

#### RAII / Drop Trait
Implementiere `Drop`, um automatische Aufräumlogik auszuführen.
```zc
impl Drop for MyStruct {
    fn drop(self) {
        self.free();
    }
}
```

### 9. Objektorientierte Programmierung (OOP)

#### Methoden
Methoden auf Typen mit `impl` definieren.
```zc
impl Point {
    // Statische Methode (Konstruktor-Konvention)
    fn new(x: int, y: int) -> Self {
        return Point{x: x, y: y};
    }

    // Instanzmethode
    fn dist(self) -> float {
        return sqrt(self.x * self.x + self.y * self.y);
    }
}
```

**Self-Kurzschreibweise**: In Methoden mit `self` kann `.feld` als Kurzform für `self.feld` verwendet werden:
```zc
impl Point {
    fn dist(self) -> float {
        return sqrt(.x * .x + .y * .y);  // Entspricht self.x, self.y
    }
}
```

#### Primitive Methoden
Zen C erlaubt es dir, Methoden für primitive Datentypen (wie `int`, `bool` usw.) mit der gleichen `impl`-Syntax zu definieren.

```zc
impl int {
    fn abs(self) -> int {
        return *self < 0 ? -(*self) : *self;
    }
}

let x = -10;
let y = x.abs();    // 10
let z = (-5).abs(); // 5 (Literals unterstützt)
```

#### Traits
Definieren gemeinsames Verhalten.
```zc
struct Circle { radius: f32; }

trait Drawable {
    fn draw(self);
}

impl Drawable for Circle {
    fn draw(self) { ... }
}

let circle = Circle{};
let drawable: Drawable = &circle;
```

#### Standard-Traits
Zen C beinhaltet Standardmerkmale, die sich in die Sprachsyntax integrieren.

**Iterable**  

Implementiere `Iterable<T>`, um `for-in`-Schleifen für eigene Typen zu ermöglichen.

```zc
import "std/iter.zc"

// Iterator definieren
struct MyIter {
    curr: int;
    stop: int;
}

impl MyIter {
    fn next(self) -> Option<int> {
        if self.curr < self.stop {
            self.curr += 1;
            return Option<int>::Some(self.curr - 1);
        }
        return Option<int>::None();
    }
}

// Iterable implementieren
impl MyRange {
    fn iterator(self) -> MyIter {
        return MyIter{curr: self.start, stop: self.end};
    }
}

// Nutzung in Schleife
for i in my_range {
    println "{i}";
}
```

**Drop**  

Implementiere `Drop`, um einen Destruktor zu definieren, der ausgeführt wird, wenn das Objekt seinen Gültigkeitsbereich verlässt (RAII).

```zc
import "std/mem.zc"

struct Resource {
    ptr: void*;
}

impl Drop for Resource {
    fn drop(self) {
        if self.ptr != NULL {
            free(self.ptr);
        }
    }
}
```

> [!IMPORTANT]
> **Anmerkung:** Wenn eine Variable verschoben wird, wird `drop` NICHT für die ursprüngliche Variable aufgerufen. Dies folgt der [Ressourcen-Semantik](#ressourcen-semantik-move-by-default).

**Copy**  

Marker-Trait zur Aktivierung des `Copy`-Verhaltens (implizite Duplizierung) anstelle der Move-Semantik. Verwendung über `@derive(Copy)`.

> [!CAUTION] 
> **Regel:** Typen, die `Copy` implementieren, dürfen keinen Destruktor (`Drop`) definieren.

```zc
@derive(Copy)
struct Point { x: int; y: int; }

fn main() {
    let p1 = Point{x: 1, y: 2};
    let p2 = p1; // Kopiert! p1 bleibt gültig
}
```

**Clone**  

Implementiert `Clone` für explizites Duplizieren von Ressourcen-Typen.

```zc
import "std/mem.zc"

struct MyBox { val: int; }

impl Clone for MyBox {
    fn clone(self) -> MyBox {
        return MyBox{val: self.val};
    }
}

fn main() {
    let b1 = MyBox{val: 42};
    let b2 = b1.clone(); // Explizite Kopie
}
```

#### Komposition
Verwende `use`, um andere Strukturen einzubetten. Du kannst diese entweder direkt einbetten (Felder vereinfachen) oder ihnen Namen geben (Felder verschachteln).

```zc
struct Entity { id: int; }

struct Player {
	// Mixin (unbenannt): Glättet Felder
    use Entity;  // Fügt 'id' direkt hinzu
    name: string;
}

struct Match {
    // Komposition (benannt): Verschachtelte Felder
    use p1: Player; // Zugriff über match.p1
    use p2: Player; // Zugriff über match.p2
}
```

### 10. Generische Typen

Typ-sichere Templates für Structs und Funktionen.

```zc
// Generisches Struct
struct Box<T> {
    item: T;
}

// Generische Funktion
fn identity<T>(val: T) -> T {
    return val;
}

// Mehrere Typ-Parameter
struct Pair<K, V> {
    key: K;
    value: V;
}
```

### 11. Nebenläufigkeit (Async/Await)

Basierend auf pthreads.

```zc
async fn fetch_data() -> string {
    // Läuft im Hintergrund
    return "Daten";
}

fn main() {
    let future = fetch_data();
    let result = await future;
}
```

### 12. Metaprogrammierung

#### Comptime
Führt Code zur Compile-Zeit aus, um Quellcode zu generieren oder Nachrichten auszugeben.

```zc
comptime {
    // Generiert Code während der Kompilierung (wird auf stdout geschrieben)
    println "let build_date = \"2024-01-01\";";
}

println "Build Date: {build_date}";
```

<details>
<summary><b>Hilfsfunktionen</b></summary>

Spezielle Funktionen innerhalb von `comptime`-Blöcken für Code-Generierung und Diagnostik:
<table>
<tr>
<th>Funktion</th>
<th>Beschreibung</th>
</tr>
<tr>
<td><code>yield(str)</code></td>
<td>Generiert explizit Code (Alternative zu <code>printf</code>)</td>
</tr>
<tr>
<td><code>code(str)</code></td>
<td>Alias für <code>yield()</code>,  klarere Absicht für Code-Generierung</td>
</tr>
<tr>
<td><code>compile_error(msg)</code></td>
<td>Bricht die Kompilierung mit Fehlermeldung ab</td>
</tr>
<tr>
<td><code>compile_warn(msg)</code></td>
<td>Gibt eine Warnung zur Compile-Zeit aus (Kompilierung wird fortgesetzt)</td>
</tr>
</table>

**Beispiel:**
```zc
comptime {
    compile_warn("Generiere optimierten Code...");
    
    let ENABLE_FEATURE = 1;
    if (ENABLE_FEATURE == 0) {
        compile_error("Feature muss aktiviert sein!");
    }
    
    // Verwende code() mit Raw-Strings für saubere Generierung
    code(r"let FEATURE_ENABLED = 1;");
}
```
</details>

<details>
<summary><b>Build-Metadaten</b></summary>

Zugriff auf Compiler-Buildinformationen zur Compile-Zeit:

<table>
<tr>
<th>Konstante</th>
<th>Typ</th>
<th>Beschreibung</th>
</tr>
<tr>
<td><code>__COMPTIME_TARGET__</code></td>
<td>string</td>
<td>Plattform: <code>"linux"</code>, <code>"windows"</code>, oder <code>"macos"</code></td>
</tr>
<tr>
<td><code>__COMPTIME_FILE__</code></td>
<td>string</td>
<td>Aktueller Quellcode-Dateiname, der kompiliert wird</td>
</tr>
</table>

**Beispiel:**
```zc
comptime {
    // Plattform-spezifische Code-Generierung
    println "let PLATFORM = \"{__COMPTIME_TARGET__}\";";
}

println "Running on: {PLATFORM}";
```
</details>

> [!TIP]
> Verwende in der Kompilierzeit rohe Zeichenketten (`r"..."`), um das Maskieren von geschweiften Klammern zu vermeiden: `code(r"fn test() { return 42; }")`. Verwende andernfalls `{{` und `}}`, um geschweifte Klammern innerhalb regulärer Zeichenketten zu maskieren.


#### Embed
Binde Dateien als bestimmte Typen ein.
```zc
// Standard (Slice_char)
let data = embed "assets/logo.png";

// Typisierte Einbindung
let text = embed "shader.glsl" as string;    // Einbindung als C-String
let rom  = embed "bios.bin" as u8[1024];     // Einbindung als fixiertes Array
let wav  = embed "sound.wav" as u8[];        // Einbindung als Slice_u8
```

#### Plugins
Importiere Compiler-Plugins zur Erweiterung der Syntax.
```zc
import plugin "regex"
let re = regex! { ^[a-z]+$ };
```

#### Generische C-Makros
Leite Preprocessor-Makros direkt an C weiter.

> [!TIP]
> Für einfache Konstanten benutze lieber `def`. Nutze `#define` nur, wenn C-Präprozessor-Makros oder bedingte Kompilierung nötig sind.

```zc
#define MAX_BUFFER 1024
```

#### Bedingte Kompilierung
Mit `@cfg()` kannst du jede Top-Level-Deklaration bedingt ein- oder ausschließen, basierend auf `-D` Flags.

```zc
// Build mit: zc build app.zc -DUSE_OPENGL

@cfg(USE_OPENGL)
import "opengl_backend.zc";

@cfg(USE_VULKAN)
import "vulkan_backend.zc";

// Oder: inkludieren, wenn irgendein Backend gewählt ist
@cfg(any(USE_OPENGL, USE_VULKAN))
fn init_graphics() { /* ... */ }

// UND mit Negation
@cfg(not(USE_OPENGL))
@cfg(not(USE_VULKAN))
fn fallback_init() { println "Kein Backend ausgewählt"; }
```

| Form | Bedeutung |
|:---|:---|
| `@cfg(NAME)` | Einbinden, wenn `-DNAME` gesetzt ist |
| `@cfg(not(NAME))` | Einbinden, wenn `-DNAME` NICHT gesetzt ist |
| `@cfg(any(A, B, ...))` | Einbinden, wenn IRGENDEINE Bedingung wahr ist (OR) |
| `@cfg(all(A, B, ...))` | Einbinden, wenn ALLE Bedingungen wahr sind (AND) |

Mehrere `@cfg` auf einer Deklaration werden ANDed. `not()` kann in `any()` und `all()` verwendet werden. Funktioniert mit jeder Deklaration auf oberster Ebene: `fn`, `struct`, `import`, `impl`, `raw`, `def`, `test` etc.

### 13. Attribute

Dekoriere Funktionen und Strukturen, um das Verhalten des Compilers zu beeinflussen.

| Attribut | Geltungsbereich | Beschreibung |
|:---|:---|:---|
| `@required` | Fn | Warnung, wenn Rückgabewert ignoriert wird. |
| `@deprecated("msg")` | Fn/Struct | Warnung bei Nutzung mit Nachricht. |
| `@inline` | Fn | Hinweis an den Compiler zum Inlining. |
| `@noinline` | Fn | Verhindert Inlining. |
| `@packed` | Struct | Entfernt Padding zwischen Feldern. |
| `@align(N)` | Struct | Erzwingt Ausrichtung auf N Bytes. |
| `@constructor` | Fn | Wird vor `main` ausgeführt. |
| `@destructor` | Fn | Wird nach Beenden von `main` ausgeführt. |
| `@unused` | Fn/Var | Unterdrückt Warnungen für ungenutzte Variablen. |
| `@weak` | Fn | Schwache Symbolbindung (Weak linkage). |
| `@section("name")` | Fn | Platziert Code in einem bestimmten Abschnitt. |
| `@noreturn` | Fn | Funktion kehrt nicht zurück (z.B. `exit`). |
| `@pure` | Fn | Funktion ohne Seiteneffekte (Optimierungshinweis). |
| `@cold` | Fn | Funktion wird selten ausgeführt (Branch-Prediction-Hinweis). |
| `@hot` | Fn | Funktion wird häufig ausgeführt (Optimierungshinweis). |
| `@export` | Fn/Struct | Exportiert Symbol (Sichtbarkeit standardmäßig). |
| `@global` | Fn | CUDA: Kernel-Einstiegspunkt (`__global__`). |
| `@device` | Fn | CUDA: Device-Funktion (`__device__`). |
| `@host` | Fn | CUDA: Host-Funktion (`__host__`). |
| `@comptime` | Fn | Hilfsfunktion für Compile-Time-Ausführung. |
| `@cfg(NAME)` | Any | Bedingte Kompilierung: nur einbinden, wenn `-DNAME` gesetzt ist. Unterstützt `not()`, `any()`, `all()`. |
| `@derive(...)` | Struct | Implementiert automatisch Traits (`Debug`, `Eq`, `Copy`, `Clone`). |
| `@ctype("type")` | Fn Param | Überschreibt den generierten C-Typ eines Parameters. |
| `@<custom>` | Any | Leitet generische Attribute an C weiter (z.B. `@flatten`, `@alias("name")`). |

#### Eigene Attribute

Zen C unterstützt ein leistungsstarkes System **benutzerdefinierter Attribute**, mit dem du beliebige GCC/Clang-`__attribute__` direkt in Ihrem Code verwenden kannst. Jedes Attribut, das vom Zen-C-Compiler nicht explizit erkannt wird, wird als generisches Attribut behandelt und an den generierten C-Code weitergegeben.

Dies erlaubt Zugriff auf erweiterte Compiler-Funktionen, Optimierungen und Linker-Directives, ohne dass die Sprache selbst diese explizit unterstützen muss.

#### Syntax-Mapping
Zen C Attribute werden direkt auf C Attribute abgebildet:
- `@name` → `__attribute__((name))`  
- `@name(args)` → `__attribute__((name(args)))`  
- `@name("string")` → `__attribute__((name("string")))`  

#### Smart Derives

Zen C bietet "Smart Derives" mit Beachtung von Move-Semantics:

- **`@derive(Eq)`**: Generiert eine Gleichheitsmethode, die Argumente per Referenz nimmt (`fn eq(self, other: T*)`).  
    - Beim Vergleich zweier non-Copy-Strukturen (`a == b`) wird `b` automatisch per Referenz (`&b`) übergeben, um Move zu vermeiden.  
    - Rekursive Feldvergleiche bevorzugen ebenfalls Pointer-Zugriff, um Besitzübergaben zu verhindern.

### 14. Inline-Assembly

Zen C unterstützt Inline-Assembly vollständig, transpiliert direkt zu GCC-Extended `asm`.

#### Grundlegende Nutzung
Schreibe den Assembler-Code innerhalb von `asm`-Blöcken. Zeichenketten werden automatisch verkettet.
```zc
asm {
    "nop"
    "mfence"
}
```

#### Volatile
Verhindert, dass der Compiler Assembly-Code mit Seiteneffekten entfernt.
```zc
asm volatile {
    "rdtsc"
}
```

#### Benannte Constraints
Zen C vereinfacht die komplexe GCC-Syntax durch benannte Bindungen:

```zc
// Syntax: : out(variable) : in(variable) : clobber(reg)
// Platzhalter {variable} für bessere Lesbarkeit

fn summe(x: int) -> int {
    let ergebnis: int;
    asm {
        "mov {x}, {ergebnis}"
        "add $5, {ergebnis}"
        : out(ergebnis)
        : in(x)
        : clobber("cc")
    }
    return ergebnis;
}
```

| Typ | Syntax | GCC-Äquivalent |
|:---|:---|:---|
| **Output** | `: out(variable)` | `"=r"(variable)` |
| **Input** | `: in(variable)` | `"r"(variable)` |
| **Clobber** | `: clobber("rax")` | `"rax"` |
| **Memory** | `: clobber("memory")` | `"memory"` |

> **Hinweis:** Bei Verwendung der Intel-Syntax (über `-masm=intel`) muss der Build korrekt konfiguriert sein (z. B. `//> cflags: -masm=intel`). TCC unterstützt keine Assemblierung mit Intel-Syntax.

### 15. Build-Direktiven

Zen C unterstützt spezielle Kommentare am Anfang der Quellcode-Datei, um den Build-Prozess zu konfigurieren, ohne Makefile oder komplexes Build-System.

| Direktive | Argumente | Beschreibung |
|:---|:---|:---|
| `//> link:` | `-lfoo` oder `pfad/zu/lib.a` | Mit einer Bibliothek oder Objektdatei linken. |
| `//> lib:` | `pfad/zu/libs` | Bibliothekssuchpfad hinzufügen (`-L`). |
| `//> include:` | `pfad/zu/headers` | Include-Suchpfad hinzufügen (`-I`). |
| `//> framework:` | `Cocoa` | macOS-Framework linken. |
| `//> cflags:` | `-Wall -O3` | Beliebige Compiler-Flags für C übergeben. |
| `//> define:` | `MACRO` oder `KEY=VAL` | Preprocessor-Makro definieren (`-D`). |
| `//> pkg-config:` | `gtk+-3.0` | `pkg-config` ausführen und Flags & Libraries anhängen. |
| `//> shell:` | `command` | Shell-Befehl während Build ausführen. |
| `//> get:` | `http://url/file` | Datei herunterladen, wenn sie nicht existiert. |

#### Features

**1. OS-Guarding**  
Präfixe wie `linux:`, `windows:`, `macos:` (oder `darwin:`) lassen Direktiven nur auf bestimmten Plattformen gelten.

```zc
//> linux: link: -lm
//> windows: link: -lws2_32
//> macos: framework: Cocoa
```

**2. Environment-Variablen**  
`${VAR}` kann in Direktiven expandiert werden.

```zc
//> include: ${HOME}/mylib/include
//> lib: ${ZC_ROOT}/std
```

#### Beispiele

```zc
//> include: ./include
//> lib: ./libs
//> link: -lraylib -lm
//> cflags: -Ofast
//> pkg-config: gtk+-3.0

import "raylib.h"

fn main() { ... }
```

### 16. Schlüsselwörter

Zen C reserviert folgende Schlüsselwörter:

#### Deklarationen
`alias`, `def`, `enum`, `fn`, `impl`, `import`, `let`, `module`, `opaque`, `struct`, `trait`, `union`, `use`

#### Kontrollfluss
`async`, `await`, `break`, `catch`, `continue`, `defer`, `do`, `else`, `for`, `goto`, `guard`, `if`, `loop`, `match`, `return`, `try`, `unless`, `while`

#### Spezielle
`asm`, `assert`, `autofree`, `comptime`, `const`, `embed`, `launch`, `ref`, `sizeof`, `static`, `test`, `volatile`

#### Konstanten
`true`, `false`, `null`

#### C-Reserviert
Die folgenden Bezeichner sind reserviert, da sie Schlüsselwörter in C11 sind:
`auto`, `case`, `char`, `default`, `double`, `extern`, `float`, `inline`, `int`, `long`, `register`, `restrict`, `short`, `signed`, `switch`, `typedef`, `unsigned`, `void`, `_Atomic`, `_Bool`, `_Complex`, `_Generic`, `_Imaginary`, `_Noreturn`, `_Static_assert`, `_Thread_local`

#### Operatoren
`and`, `or`

### 17. C-Interoperabilität

Zen C bietet zwei Wege, um mit C-Code zu interagieren: **Trusted Imports** (praktisch) und **Explicit FFI** (sicher/präzise).

#### Methode 1: Trusted Imports (praktisch)

C-Headerdatei können direkt mit dem Schlüsselwort `import` und der Dateiendung `.h` importieren werden. Dadurch wird die Headerdatei als Modul behandelt und es wird davon ausgegangen, dass alle darüber aufgerufenen Symbole vorhanden sind.

```zc
//> link: -lm
import "math.h" as c_math;

fn main() {
	// Der Compiler vertraut auf die Korrektheit; gibt 'cos(...)' direkt aus
    let x = c_math::cos(3.14159);
}
```

> **Vorteile**: Keine Boilerplate. Alle Inhalte im Header sind sofort zugänglich.
> **Nachteil**: Zen C prüft die Typen nicht, Fehler werden vom C-Compiler erkannt.

#### Methode 2: Explicit FFI (sicher)

Für strenge Typprüfung oder wenn Header nicht eingebunden werden sollen. Nutze `extern fn`.

```zc
include <stdio.h> // Generiert #include <stdio.h>

// Strenge Signatur definieren
extern fn printf(fmt: char*, ...) -> c_int;

fn main() {
    printf("Hallo FFI: %d\n", 42); // Typprüfung durch Zen C
}
```

> **Vorteile**: Typprüfung durch Zen C.  
> **Nachteil**: Manuelle Deklaration der Funktionen erforderlich.

#### `import` vs `include`

- **`import "file.h"`**: Registriert Header als Modul; erlaubt impliziten Zugriff auf Symbole (`file::function()`).  
- **`include <file.h>`**: Fügt nur `#include` in generiertes C ein; Symbole müssen mit `extern fn` manuell deklariert werden.


---

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

Zen C bietet ein eingebautes Test-Framework, um Unit-Tests direkt in den Quellcode-Dateien zu schreiben, mittels des `test`-Schlüsselworts.

#### Syntax
Ein `test`-Block enthält einen beschreibenden Namen und einen Codeblock, der ausgeführt wird. Es wird keine `main`-Funktion benötigt.

```zc
test "unittest1" {
    "Dies ist ein Unit-Test";

    let a = 3;
    assert(a > 0, "a sollte eine positive Zahl sein");

    "unittest1 erfolgreich.";
}
```

#### Tests ausführen
Um alle Tests einer Datei auszuführen, nutze den `run`-Befehl. Der Compiler erkennt automatisch alle top-level `test`-Blöcke.

```bash
zc run my_file.zc
```

#### Assertions
Verwende die eingebaute Funktion `assert(condition, message)` zur Überprüfung von Erwartungen. Wenn die Bedingung falsch ist, schlägt der Test fehl und die Nachricht wird ausgegeben.

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
zc lsp
```

Es kommuniziert über Standard I/O (JSON-RPC 2.0).

### REPL

Die Read-Eval-Print-Schleife ermöglicht es, interaktiv mit Zen C-Code zu experimentieren.

```bash
zc repl
```

#### Features

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

- **[Installations- und Einrichtungsanleitung](docs/LSP.md)**
- **Unterstützte Editoren**: VS Code, Neovim, Vim ([zenc.vim](https://github.com/zenc-lang/zenc.vim)), Zed und alle LSP-fähigen Editoren.

Verwende `zc lsp`, um den Server zu starten.

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

### Build mit Zig

Zigs `zig cc` dient als Drop-in-Ersatz für GCC/Clang mit exzellenter Cross-Compilation-Unterstützung. Um Zig zu verwenden:

```bash
# Zen C Programm mit Zig kompilieren und ausführen
zc run app.zc --cc zig

# Den Zen C Compiler selbst mit Zig bauen
make zig
```

### C++-Interoperabilität

Zen C kann mit dem `--cpp`-Flag C++-kompatiblen Code generieren und dadurch nahtlos mit C++-Bibliotheken interagieren.

```bash
# Direkte Kompilierung mit g++
zc app.zc --cpp

# Oder transpilen und manuell bauen
zc transpile app.zc --cpp
g++ out.c my_cpp_lib.o -o app
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

Zen C unterstützt GPU-Programmierung durch Transpilierung nach **CUDA C++**. Dadurch lassen sich moderne C++-Features (Templates, constexpr) in CUDA-Kernels nutzen, während die ergonomische Zen C Syntax erhalten bleibt.

```bash
# Direkt mit nvcc kompilieren
zc run app.zc --cuda

# Oder transpilen und manuell bauen
zc transpile app.zc --cuda -o app.cu
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

Zen C kann mit dem `--objc`-Flag nach **Objective-C (`.m`)** kompilieren, sodass Objective-C-Frameworks (wie Cocoa/Foundation) und deren Syntax direkt genutzt werden können.

```bash
# Mit clang kompilieren (oder gcc/gnustep)
zc app.zc --objc --cc clang
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