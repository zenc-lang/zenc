# Beitrag zu Zen C

Zunächst einmal vielen Dank, dass du eine Mitarbeit bei Zen C in Erwägung ziehst! Menschen wie du machen dieses Projekt so wertvoll.

Wir freuen uns über jeden Beitrag, egal ob es sich um Fehlerbehebungen, das Hinzufügen von Dokumentation, Vorschläge für neue Funktionen oder einfach nur um das Melden von Problemen handelt.

## So kannst du mitwirken

Der allgemeine Workflow für Mitwirkende ist wie folgt:

1. **Repository forken**: Verwende den Standard-GitHub-Workflow, um das Repository in dein eigenes Konto zu forken.
2. **Feature-Branch erstellen**: Erstelle einen neuen Branch für dein Feature oder deinen Bugfix. Dadurch bleiben deine Änderungen übersichtlich und vom Haupt-Branch getrennt.
```bash
git checkout -b feature/NewThing
```
3. **Änderungen vornehmen**: Schreibe deinen Code oder deine Dokumentationsänderungen.
4. **Überprüfen**: Stelle sicher, dass deine Änderungen wie erwartet funktionieren und keine bestehenden Funktionen beeinträchtigen (siehe [Tests ausführen](#testausfuehrung)).
5. **Pull Request einreichen**: Pushen deinen Branch in deinen Fork und sende einen Pull Request (PR) an das Haupt-Repository von Zen C.

## Issues und Pull Requests

Wir verwenden GitHub Issues und Pull Requests, um Fehler und neue Funktionen zu erfassen. Um die Qualität zu sichern:

- **Vorlagen verwenden**: Bitte nutze beim Erstellen eines Issues oder Pull Requests die bereitgestellten Vorlagen.
	- **Fehlerbericht**: Zum Melden von Fehlern.
	- **Funktionsanfrage**: Für Vorschläge neuer Funktionen.
	- **Pull Request**: Zum Einreichen von Codeänderungen.
- **Aussagekräftige Beschreibung**: Bitte gebe so viele Details wie möglich an.
	- **Automatische Prüfung**: Wir haben einen automatisierten Workflow, der die Länge der Beschreibung neuer Issues und Pull Requests prüft. Ist die Beschreibung zu kurz (weniger als 50 Zeichen), wird das Issue automatisch geschlossen. So stellen wir sicher, dass wir genügend Informationen haben, um dir zu helfen.

## Entwicklungsrichtlinien

### Codierungsstil
- Halte dich an den im Quellcode vorhandenen C-Stil. Konsistenz ist entscheidend.
- Du kannst die bereitgestellte Datei `.clang-format` verwenden, um deinen Code zu formatieren.
- Achte auf sauberen und lesbaren Code.

### Projektstruktur
Wenn du den Compiler erweitern möchtest, findest du hier eine kurze Übersicht der Codebasis:
* **Parser**: `src/parser/` – Enthält die Implementierung des rekursiven Abstiegsparsers.
* **Codegenerierung**: `src/codegen/` – Enthält die Transpilerlogik, die Zen C in GNU C/C11 konvertiert.
* **Standardbibliothek**: `std/` – Die in Zen C geschriebenen Module der Standardbibliothek.

## Testausführung
Die Testsuite ist dein wichtigster Helfer bei der Entwicklung. Bitte stelle sicher, dass alle Tests erfolgreich durchlaufen, bevor du einen Pull Request einreichst.

### Alle Tests ausführen
Um die gesamte Testsuite mit dem Standardcompiler (normalerweise GCC) auszuführen:
```bash
make test
```

### Einzelnen Test ausführen
Um während der Entwicklung Zeit zu sparen, kannst du eine einzelne Testdatei direkt ausführen:
```bash
./zc run tests/language/control_flow/test_match.zc
```

Oder du kannst die Testsuite selektiv ausführen:
```bash
make test only="tests/language/control_flow/test_match.zc"
```

### Testen mit verschiedenen Backends
Zen C unterstützt mehrere C-Compiler als Backends. Du kannst Tests gezielt gegen diese ausführen:

**Clang**:
```bash
./tests/run_tests.sh --cc clang
```

**Zig (cc)**:
```bash
./tests/run_tests.sh --cc zig
```

**TCC (Tiny C Compiler)**:
```bash
./tests/run_tests.sh --cc tcc
```

## Pull-Request-Prozess

1. Stelle sicher, dass du Tests für alle neuen Funktionen hinzugefügt hast.
2. Stelle sicher, dass alle vorhandenen Tests erfolgreich sind.
3. Aktualisiere gegebenenfalls die Dokumentation (Markdown-Dateien in `docs/` oder `README.md`).
4. Beschreibe deine Änderungen klar und deutlich in der Pull-Request-Beschreibung. Verlinke gegebenenfalls auf relevante Issues.

Vielen Dank für deinen Beitrag!
