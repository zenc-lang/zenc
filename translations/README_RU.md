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
  <h3>Современная эргономика. Никаких накладных расходов. Чистый Си.</h3>
  <br>
  <p>
    <a href="#"><img src="https://img.shields.io/badge/build-passing-brightgreen" alt="Build Status"></a>
    <a href="#"><img src="https://img.shields.io/badge/license-MIT-blue" alt="License"></a>
    <a href="#"><img src="https://img.shields.io/github/v/release/zenc-lang/zenc?label=version&color=orange" alt="Version"></a>
    <a href="#"><img src="https://img.shields.io/badge/platform-linux%20%7C%20windows%20%7C%20macos-lightgrey" alt="Platform"></a>
  </p>
  <p><em>Пишите как на высокоуровневом языке, запускайте как Си.</em></p>
</div>

<hr>

<div align="center">
  <p>
    <b><a href="#обзор">Обзор</a></b> •
    <b><a href="#сообщество">Сообщество</a></b> •
    <b><a href="#быстрый-старт">Быстрый старт</a></b> •
    <b><a href="#экосистема">Экосистема</a></b> •
    <b><a href="#справочник">Справочник</a></b> •
    <b><a href="#стандартная-библиотека">Стандартная библиотека</a></b> •
    <b><a href="#инструменты">Инструменты</a></b>
  </p>
</div>

---

## Обзор

**Zen C** — это современный язык системного программирования, который компилируется в человекочитаемом `GNU C`/`C11`. Он предоставляет богатый набор возможностей, включая вывод типов, сопоставление с паттернами, генерику, трейты, async/await и ручное управление памятью с возможностями RAII, при этом поддерживая 100% совместимость с ABI Си.

## Сообщество

Приглашаем вас присоединиться к нам на официальном Discord-сервере Zen C! Здесь можно обсуждать проект, делиться примерами, задавать вопросы и сообщать об ошибках.

- Discord: [Присоединиться](https://discord.com/invite/q6wEsCmkJP)
- RFC: [Предложить функции](https://github.com/zenc-lang/rfcs)

## Экосистема

Проект Zen C состоит из нескольких репозиториев. Ниже приведены основные из них:

| Репозиторий | Описание | Статус |
| :--- | :--- | :--- |
| **[zenc](https://github.com/zenc-lang/zenc)** | Ядро компилятора Zen C (`zc`), CLI и стандартная библиотека. | Активная разработка |
| **[docs](https://github.com/zenc-lang/docs)** | Официальная техническая документация и спецификация языка. | Активен |
| **[rfcs](https://github.com/zenc-lang/rfcs)** | Репозиторий запросов на комментарии (RFC). Формируйте будущее языка. | Активен |
| **[vscode-zenc](https://github.com/zenc-lang/vscode-zenc)** | Официальное расширение VS Code (подсветка синтаксиса, сниппеты). | Alpha |
| **[www](https://github.com/zenc-lang/www)** | Исходный код `zenc-lang.org`. | Активен |
| **[awesome-zenc](https://github.com/zenc-lang/awesome-zenc)** | Курируемый список отличных примеров Zen C. | Растет |
| **[zenc.vim](https://github.com/zenc-lang/zenc.vim)** | Официальный плагин для Vim/Neovim (Синтаксис, Отступы). | Активен |

## Галерея

Посмотрите на эти проекты, созданные на Zen C:

- **[ZC-pong-3ds](https://github.com/5quirre1/ZC-pong-3ds)**: Клон Pong для Nintendo 3DS.
- **[zen-c-parin](https://github.com/Kapendev/zen-c-parin)**: Базовый пример использования Zen C с Parin.
- **[almond](https://git.sr.ht/~leanghok/almond)**: Минималистичный веб-браузер, написанный на Zen C.

---

## Содержание
<table align="center">
  <tr>
    <th width="50%">Общее</th>
    <th width="50%">Справочник</th>
  </tr>
  <tr>
    <td valign="top">
      <ul>
        <li><a href="#обзор">Обзор</a></li>
        <li><a href="#сообщество">Сообщество</a></li>
        <li><a href="#экосистема">Экосистема</a></li>
        <li><a href="https://github.com/zenc-lang/rfcs">RFC</a></li>
        <li><a href="#быстрый-старт">Быстрый старт</a></li>
        <li><a href="https://github.com/zenc-lang/docs">Документация</a></li>
        <li><a href="#стандартная-библиотека">Стандартная библиотека</a></li>
        <li><a href="#инструменты">Инструменты</a>
          <ul>
            <li><a href="#протокол-языкового-сервера-lsp">LSP</a></li>
            <li><a href="#отладка-zen-c">Отладка</a></li>
          </ul>
        </li>
        <li><a href="#поддержка-компилятора-и-совместимость">Поддержка компилятора</a></li>
        <li><a href="#участие-в-разработке">Участие в разработке</a></li>
        <li><a href="#благодарности">Благодарности</a></li>
      </ul>
    </td>
    <td valign="top">
      <p><a href="https://docs.zenc-lang.org/tour/"><b>Browse the Language Reference</b></a></p>
    </td>
  </tr>
</table>
---

## Быстрый старт

### Установка
```bash
git clone https://github.com/zenc-lang/zenc.git
cd zenc
make clean # удалить старые файлы сборки
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

Zen C имеет полную нативную поддержку Windows (x86_64). Вы можете выполнить сборку, используя прилагаемый пакетный скрипт с GCC (MinGW):

```cmd
build.bat
```

Это соберет компилятор (`zc.exe`). Сетевые операции, операции с файловой системой и процессами полностью поддерживаются через уровень абстракции платформы (PAL).

Кроме того, вы можете использовать `make`, если у вас есть Unix-подобная среда (MSYS2, Cygwin, git-bash).

### Портативная сборка (APE)
Zen C можно скомпилировать как **Actually Portable Executable (APE)** с помощью [Cosmopolitan Libc](https://github.com/jart/cosmopolitan). Это создаёт один исполняемый файл (`.com`), работающий нативно на Linux, macOS, Windows, FreeBSD, OpenBSD и NetBSD на архитектурах x86_64 и aarch64.

**Требования:**
- Набор инструментов `cosmocc` (должен быть в PATH)

**Сборка и установка:**
```bash
make ape
sudo env "PATH=$PATH" make install-ape
```

**Созданные файлы:**
- `out/bin/zc.com`: Портативный компилятор Zen-C с встроенной стандартной библиотекой.
- `out/bin/zc-boot.com`: Установщик для создания новых проектов Zen-C.

**Использование:**
```bash
# Запустить на любой поддерживаемой ОС
./out/bin/zc.com build hello.zc -o hello
```

### Модульная сборка

Zen C разделен на опциональные модули. Используйте флаги `ZC_*` для выбора функций при сборке:

| Флаг | По умолч. | Исключает |
|---|---|---|
| `ZC_LSP=0` | 1 | LSP-сервер (~8 файлов) |
| `ZC_REPL=0` | 1 | Интерактивная REPL (~6 файлов) |
| `ZC_PLUGINS=0` | 1 | Система плагинов |
| `ZC_ZEN=0` | 1 | Режимы `--doc` / `--facts` |
| `ZC_BACKENDS=0` | 1 | Бэкенды не-C (JSON, Lisp, и т.д.) |
| `ZC_TRE=0` | 1 | Библиотека TRE regex |

```bash
make                     # Все функции (3.3 MB)
make lite                # Без LSP, REPL, Zen (2.9 MB)
make core                # Только компилятор (2.7 MB)
make minimal             # Минимальная сборка (2.6 MB)

# Выборочная сборка:
make ZC_LSP=0 ZC_REPL=0  # Исключить LSP и REPL
```

Отключенные команды показывают понятное сообщение вместо аварийного завершения: `zc-lsp` → "LSP support not included".

### Использование
```bash
# Компилировать и запустить
zc run hello.zc

# Создать исполняемый файл
zc build hello.zc -o hello

# Интерактивная оболочка
zc-repl

# Документация (Рекурсивно)
zc-doc main.zc

# Документация (Один файл, без проверки)
zc-doc --no-recursive-doc main.zc

```

### Переменные окружения
Установите `ZC_ROOT` для указания пути к стандартной библиотеке (для импортов типа `import "std/vec.zc"`). Это позволяет запускать `zc` из любого каталога.
```bash
export ZC_ROOT=/path/to/zenc
```

---

## Справочник

Для получения более подробной информации см. официальный [Справочник по языку](https://docs.zenc-lang.org/tour/01-variables-constants/).

## Стандартная библиотека

Zen C включает стандартную библиотеку (`std`), охватывающую основную функциональность.

[Просмотрите документацию стандартной библиотеки](../docs/std/README.md)

### Ключевые модули

<details>
<summary>Нажмите, чтобы посмотреть все модули стандартной библиотеки</summary>

| Модуль               | Описание                                           | Docs                        |
|:---------------------|:---------------------------------------------------|:----------------------------|
| **`std/bigfloat.zc`** | Арифметика с плавающей запятой произвольной точности. | [Docs](../docs/std/bigfloat.md) |
| **`std/bigint.zc`** | Целое число произвольной точности `BigInt`. | [Docs](../docs/std/bigint.md) |
| **`std/bits.zc`** | Низкоуровневые побитовые операции (`rotl`, `rotr` и т.д.). | [Docs](../docs/std/bits.md) |
| **`std/complex.zc`** | Арифметика комплексных чисел `Complex`. | [Docs](../docs/std/complex.md) |
| **`std/vec.zc`**     | Растущий динамический массив `Vec<T>`.             | [Docs](../docs/std/vec.md)     |
| **`std/string.zc`**  | Выделенный в heap тип `String` с поддержкой UTF-8. | [Docs](../docs/std/string.md)  |
| **`std/queue.zc`**   | FIFO очередь (Ring Buffer).                        | [Docs](../docs/std/queue.md)   |
| **`std/map.zc`**     | Обобщённая хеш-таблица `Map<V>`.                   | [Docs](../docs/std/map.md)     |
| **`std/fs.zc`**      | Операции файловой системы.                         | [Docs](../docs/std/fs.md)      |
| **`std/io.zc`**      | Стандартный ввод/вывод (`print`/`println`).        | [Docs](../docs/std/io.md)      |
| **`std/option.zc`**  | Опциональные значения (`Some`/`None`).             | [Docs](../docs/std/option.md)  |
| **`std/result.zc`**  | Обработка ошибок (`Ok`/`Err`).                     | [Docs](../docs/std/result.md)  |
| **`std/path.zc`**    | Кроссплатформенная манипуляция путями.             | [Docs](../docs/std/path.md)    |
| **`std/env.zc`**     | Переменные окружения процесса.                     | [Docs](../docs/std/env.md)     |
| **`std/net/`**     | TCP, UDP, HTTP, DNS, URL.              | [Docs](../docs/std/net.md)     |
| **`std/thread.zc`**  | Потоки и синхронизация.                            | [Docs](../docs/std/thread.md)  |
| **`std/time.zc`**    | Измерение времени и сон.                           | [Docs](../docs/std/time.md)    |
| **`std/json.zc`**    | Парсинг и сериализация JSON.                       | [Docs](../docs/std/json.md)    |
| **`std/stack.zc`**   | LIFO стек `Stack<T>`.                              | [Docs](../docs/std/stack.md)   |
| **`std/set.zc`**     | Обобщённое хеш-множество `Set<T>`.                 | [Docs](../docs/std/set.md)     |
| **`std/process.zc`** | Выполнение и управление процессами.                | [Docs](../docs/std/process.md) |
| **`std/regex.zc`** | Регулярные выражения (на основе TRE).              | [Docs](../docs/std/regex.md) |
| **`std/simd.zc`** | Нативные SIMD-векторные типы.                       | [Docs](../docs/std/simd.md) |

</details>

---

## Инструменты

Zen C предоставляет встроенный языковой сервер и REPL для того, чтобы вам было комфортно программировать на этом ЯП!

### Языковой сервер (LSP)

Языковой сервер Zen C (LSP) поддерживает стандартные функции LSP для интеграции в редактор, предоставляя:

*   **Переходы к определению**
*   **Нахождения ссылок**
*   **Информация при наведении**
*   **Автозаполнения:** (имена функций/структур, dot-дополнение для методов/полей)
*   **Символы документа** (outline)
*   **Справка для сигнатур**
*   **Диагностика** (ошибки синтаксиса/семантики)

Чтобы запустить языковой сервер (обычно настраивается в настройках LSP вашего редактора):

```bash
zc-lsp
```

Работает это через стандартный I/O (JSON-RPC 2.0).

### REPL

Цикл Read-Eval-Print Loop (REPL) позволяет вам интерактивно экспериментировать с кодом Zen C, используя современную **внутрипроцессную JIT-компиляцию** (на базе LibTCC).

```bash
zc-repl
```

#### Особенности

*   **JIT-исполнение**: Код компилируется в памяти и выполняется непосредственно внутри процесса REPL для мгновенной обратной связи.

*   **Интерактивное кодирование**: Вводите выражения или операторы для немедленного вычисления.
*   **Постоянная история**: Команды сохраняются в `~/.zprep_history`.
*   **Стартовый скрипт**: Автоматически загружает команды из `~/.zprep_init.zc`.

#### Команды

| Команда         | Описание                                                         |
|:----------------|:-----------------------------------------------------------------|
| `:help`         | Показать доступные команды.                                      |
| `:reset`        | Очистить историю текущей сессии (переменные/функции).            |
| `:vars`         | Показать активные переменные.                                    |
| `:funcs`        | Показать определённые пользователем функции.                     |
| `:structs`      | Показать определённые пользователем структуры.                   |
| `:imports`      | Показать активные импорты.                                       |
| `:history`      | Показать историю ввода сессии.                                   |
| `:type <expr>`  | Показать тип выражения.                                          |
| `:c <stmt>`     | Показать сгенерированный код C для оператора.                    |
| `:time <expr>`  | Провести бенчмарк выражения (запускает 1000 итераций).           |
| `:edit [n]`     | Редактировать команду `n` (по умолчанию: последняя) в `$EDITOR`. |
| `:save <file>`  | Сохранить текущую сессию в файл `.zc`.                           |
| `:load <file>`  | Загрузить и выполнить файл `.zc` в сессию.                       |
| `:watch <expr>` | Наблюдать за выражением (переоценивается после каждого ввода).   |
| `:unwatch <n>`  | Удалить наблюдение.                                              |
| `:undo`         | Удалить последнюю команду из сессии.                             |
| `:delete <n>`   | Удалить команду с индексом `n`.                                  |
| `:clear`        | Очистить экран.                                                  |
| `:quit`         | Выйти из REPL.                                                   |
| `! <cmd>`       | Запустить команду оболочки (например `!ls`).                     |

---

### Протокол языкового сервера (LSP)

Zen C включает встроенный языковой сервер для интеграции с редакторами.

- **[Руководство по установке и настройке](translations/LSP_RU.md)**
- **Поддерживаемые редакторы**: VS Code, Neovim, Vim, Zed и любой редактор с поддержкой LSP.

Используйте `zc-lsp` для запуска сервера.

### Отладка Zen C

Программы на Zen C можно отлаживать с помощью стандартных отладчиков C, таких как **LLDB** или **GDB**.

#### Visual Studio Code

Для наилучшей работы в VS Code установите официальное [расширение Zen C](https://marketplace.visualstudio.com/items?itemName=Z-libs.zenc). Для отладки вы можете использовать расширения **C/C++** (от Microsoft) или **CodeLLDB**.

Добавьте эти конфигурации в вашу директорию `.vscode`, чтобы включить отладку одним щелчком мыши:

**`tasks.json`** (Задача сборки):
```json
{
    "label": "Zen C: Build Debug",
    "type": "shell",
    "command": "zc",
    "args": [ "${file}", "-g", "-o", "${fileDirname}/app", "-O0" ],
    "group": { "kind": "build", "isDefault": true }
}
```

**`launch.json`** (Отладчик):
```json
{
    "name": "Zen C: Debug (LLDB)",
    "type": "lldb",
    "request": "launch",
    "program": "${fileDirname}/app",
    "preLaunchTask": "Zen C: Build Debug"
}
```

## Поддержка компилятора и совместимость

Zen C разработан для работы с большинством компиляторов C11. Некоторые функции полагаются на расширения GNU C, но они часто работают на других компиляторах. Используйте флаг `--cc` для переключения бэкендов.

```bash
zc run app.zc --cc clang
zc run app.zc --cc zig
```

### Статус набора тестов

<details>
<summary>Нажмите, чтобы посмотреть детали поддержки компиляторов</summary>

| Компилятор | Процент успеха | Поддерживаемые функции               | Известные ограничения                                    |
|:-----------|:--------------:|:-------------------------------------|:---------------------------------------------------------|
| **GCC**    |    **100% (Полная)**    | Все функции                          | Нет.                                                     |
| **Clang**  |    **100% (Полная)**    | Все функции                          | Нет.                                           |
| **Zig** | **100% (Полная)** | Все функции | Нет. Использует `zig cc` как замену C компилятора. |
| **TCC** | **98% (Высокая)** | Структуры, Дженерики, Трейты, Сопоставление с образцом | Нет Intel ASM, Нет `__attribute__((constructor))`. |

</details>

> [!WARNING]
> **ПРЕДУПРЕЖДЕНИЕ О СБОРКЕ:** Хотя **Zig CC** отлично работает в качестве бэкенда для ваших программ Zen C, сборка *самого компилятора Zen C* с его помощью может пройти проверку, но создать нестабильный бинарный файл, который не пройдет тесты. Мы рекомендуем собирать компилятор с помощью **GCC** или **Clang** и использовать Zig только как бэкенд для вашего рабочего кода.

> [!TIP]
> 
### Тестирование на Соответствие MISRA C:2012

Набор тестов Zen C включает проверку на соответствие рекомендациям MISRA C:2012.

> [!IMPORTANT]
> **Отказ от Ответственности MISRA**
> Этот проект является полностью независимым и не имеет никакого отношения, официального одобрения или корпоративной связи с MISRA (Motor Industry Software Reliability Association). 
> 
> В связи со строгими ограничениями авторского права, тестовые случаи перечисляют директивы только по их числовым идентификаторам и избегают публикации внутренних спецификаций. Пользователям, которым требуется первоисточник документации, рекомендуется приобрести подлинные материалы руководств на [официальном портале MISRA](https://www.misra.org.uk/).

### Сборка с Zig


Команда `zig cc` Zig предоставляет drop-in замену для GCC/Clang с отличной поддержкой кроссплатформенной компиляции. Чтобы использовать Zig:

```bash
# Компилировать и запустить программу Zen C с Zig
zc run app.zc --cc zig

# Собрать сам компилятор Zen C с Zig
make zig
```

### Бэкенды вывода

Zen C поддерживает множество бэкендов вывода через флаг `--backend`. Каждый бэкенд производит свой целевой формат:

| Бэкенд | Флаг | Расширение | Описание |
|:---|:---|:---:|:---|
| **C** | `--backend c` | `.c` | По умолчанию — GNU C11 |
| **C++** | `--backend cpp` | `.cpp` | Совместимость с C++11 (также доступен как `--cpp`) |
| **CUDA** | `--backend cuda` | `.cu` | NVIDIA CUDA C++ (также доступен как `--cuda`) |
| **Objective-C** | `--backend objc` | `.m` | Objective-C (также доступен как `--objc`) |
| **JSON** | `--backend json` | `.json` | Машиночитаемое AST для инструментов |
| **AST dump** | `--backend ast-dump` | `.ast` | Человекочитаемое AST-дерево (отладка) |
| **Lisp** | `--backend lisp` | `.lisp` | Транспиляция в Common Lisp (`sbcl --script`) |
| **Graphviz** | `--backend dot` | `.dot` | Визуальный граф AST (`dot -Tpng ast.dot -o ast.png`) |

Опции бэкенда можно задать через `--backend-opt`:

```bash
# Вывод JSON с форматированием
zc transpile file.zc --backend json --backend-opt pretty

# Показать полное содержимое (без сокращения)
zc transpile file.zc --backend lisp --backend-opt full-content

# Или использовать псевдонимы:
zc transpile file.zc --backend json --json-pretty
zc transpile file.zc --backend lisp --backend-full-content
```

Все опции бэкенда самодокументируемы — неизвестные флаги `--` автоматически проверяются по зарегистрированным псевдонимам бэкендов.

### Взаимодействие с C++

Zen C может генерировать код, совместимый с C++, с флагом `--backend cpp` (`--cpp` кратко), позволяя беспрепятственную интеграцию с библиотеками C++.

```bash
# Прямая компиляция с g++
zc app.zc --backend cpp

# Или транспилировать для ручной сборки
zc transpile app.zc --backend cpp
g++ out.cpp my_cpp_lib.o -o app
```

#### Использование C++ в Zen C

Включите заголовки C++ и используйте сырые блоки для кода C++:

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

> **Примечание:** Флаг `--cpp` переключает бэкенд на `g++` и выводит совместимый с C++ код (использует `auto` вместо `__auto_type`, перегрузки функций вместо `_Generic` и явные приведения для `void*`).

### Взаимодействие с CUDA

Zen C поддерживает GPU-программирование путём транспиляции в **CUDA C++** через флаг `--backend cuda` (`--cuda` кратко). Это позволяет вам использовать мощные функции C++ (шаблоны, constexpr) в ваших ядрах, сохраняя эргономичный синтаксис Zen C.

```bash
# Прямая компиляция с nvcc
zc run app.zc --backend cuda

# Или транспилировать для ручной сборки
zc transpile app.zc --backend cuda -o app.cu
nvcc app.cu -o app
```

#### Специфичные для CUDA атрибуты

| Атрибут   | Эквивалент CUDA | Описание                                                   |
|:----------|:----------------|:-----------------------------------------------------------|
| `@global` | `__global__`    | Функция ядра (запускается на GPU, вызывается с хоста).     |
| `@device` | `__device__`    | Функция устройства (запускается на GPU, вызывается с GPU). |
| `@host`   | `__host__`      | Функция хоста (явно только для CPU).                       |

#### Синтаксис запуска ядра

Zen C предоставляет чистый оператор `launch` для вызова ядер CUDA:

```zc
launch kernel_name(args) with {
    grid: num_blocks,
    block: threads_per_block,
    shared_mem: 1024,  // Опционально
    stream: my_stream  // Опционально
};
```

Это транспилируется в: `kernel_name<<<grid, block, shared, stream>>>(args);`

#### Написание ядер CUDA

Используйте синтаксис функции Zen C с `@global` и оператором `launch`:

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

#### Стандартная библиотека (`std/cuda.zc`)
Zen C предоставляет стандартную библиотеку для общих операций CUDA, чтобы уменьшить использование `raw` блоков:

```zc
import "std/cuda.zc"

// Управление памятью
let d_ptr = cuda_alloc<float>(1024);
cuda_copy_to_device(d_ptr, h_ptr, 1024 * sizeof(float));
defer cuda_free(d_ptr);

// Синхронизация
cuda_sync();

// Индексирование потоков (использование внутри ядер)
let i = thread_id(); // Глобальный индекс
let bid = block_id();
let tid = local_id();
```

> [!NOTE]
> **Примечание:** Флаг `--cuda` устанавливает `nvcc` как компилятор и подразумевает режим `--cpp`. Требует NVIDIA CUDA Toolkit.

### Поддержка C23

Zen C поддерживает современные функции стандарта C23 при использовании совместимого компилятора бэкенда (GCC 14+, Clang 14+, TCC (частичная)).

- **`auto`**: Zen C автоматически отображает вывод типов на стандартный C23 `auto`, если `__STDC_VERSION__ >= 202300L`.
- **`_BitInt(N)`**: Используйте типы `iN` и `uN` (например, `i256`, `u12`, `i24`) для доступа к целым числам произвольной ширины C23.

### Взаимодействие с Objective-C

Zen C может компилировать в Objective-C (`.m`), используя флаг `--backend objc` (`--objc` кратко), позволяя вам использовать фреймворки Objective-C (такие как Cocoa/Foundation) и синтаксис.

```bash
# Компилировать с clang (или gcc/gnustep)
zc app.zc --backend objc --cc clang
```

#### Использование Objective-C в Zen C

Используйте `include` для заголовков и `raw` блоки для синтаксиса Objective-C (`@interface`, `[...]`, `@""`).

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
> **Примечание:** Интерполяция строк Zen C работает с объектами Objective-C (`id`), вызывая `debugDescription` или `description`.

---

### Публичное API (Встраивание)

Zen C можно использовать как библиотеку C через публичные заголовки в `src/public/*.h`. Эти заголовки компилируются без `-DZC_ALLOW_INTERNAL` и предоставляют стабильный API для встраивания компилятора в ваши собственные инструменты:

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

**Компилировать с:**

```bash
cc -I src/public -I src -I src/utils my_tool.c -o my_tool
```

**После установки (`make install`):**

```bash
cc -I /usr/local/include/zenc my_tool.c -o my_tool
```

Публичное API охватывает:
- **`zc_core.h`** — `CompilerConfig`, `ZenCompiler`, `ASTNode`, `Type` типы, точки входа парсера, вспомогательные функции для типов
- **`zc_driver.h`** — `driver_run()`, `driver_compile()` (полная оркестровка конвейера)
- **`zc_codegen.h`** — `codegen_node()`, `emit_preamble()`, `format_expression_as_c()`
- **`zc_analysis.h`** — `check_program()`, `check_moves_only()`, `resolve_alias()`
- **`zc_diag.h`** — `zerror_at()`, `zwarn_at()`, `zpanic_at()`, диагностические отчеты
- **`zc_utils.h`** — `Emitter` (выходной буфер), `load_file()`, `z_resolve_path()`

Установите с помощью `sudo make install` для развертывания заголовков, бинарного файла, man-страниц и стандартной библиотеки.

---

## Участие в разработке

Мы приветствуем ваш вклад! Исправление ошибок, добавление документации или предложение новых функций.

Пожалуйста, ознакомьтесь с [CONTRIBUTING_RU.md](CONTRIBUTING_RU.md) для получения подробных инструкций о том, как внести свой вклад, запустить тесты и отправить запрос на слияние.

---

## Безопасность

Инструкции по сообщению об уязвимостях см. в [SECURITY_RU.md](SECURITY_RU.md).

---

## Благодарности

Этот проект использует сторонние библиотеки. Полные тексты лицензий можно найти в каталоге `LICENSES/`.

*   **[cJSON](https://github.com/DaveGamble/cJSON)** (MIT License): Используется для парсинга JSON и генерации в языковом сервере.
*   **[zc-ape](https://github.com/OEvgeny/zc-ape)** (MIT License): Оригинальный порт Actually Portable Executable Zen-C от [Eugene Olonov](https://github.com/OEvgeny).
*   **[Cosmopolitan Libc](https://github.com/jart/cosmopolitan)** (ISC License): Основополагающая библиотека, которая делает APE возможной.
*   **[TRE](https://github.com/laurikari/tre)** (BSD License): Используется для движка регулярных выражений в стандартной библиотеке.
*   **[zenc.vim](https://github.com/zenc-lang/zenc.vim)** (Лицензия MIT): Официальный плагин Vim/Neovim, в основном написанный **[davidscholberg](https://github.com/davidscholberg)**.
*   **[TinyCC](https://github.com/TinyCC/tinycc)** (Лицензия LGPL): Фундаментальный JIT-движок, используемый для высокопроизводительной оценки REPL.

---

<div align="center">
  <p>
    Copyright © 2026 Язык Программирования Zen C.<br>
    Начните своё путешествие сегодня.
  </p>
  <p>
    <a href="https://discord.com/invite/q6wEsCmkJP">Discord</a> •
    <a href="https://github.com/zenc-lang/zenc">GitHub</a> •
    <a href="https://github.com/zenc-lang/docs">Документация</a> •
    <a href="https://github.com/zenc-lang/awesome-zenc">Примеры</a> •
    <a href="https://github.com/zenc-lang/rfcs">RFC</a> •
    <a href="CONTRIBUTING_RU.md">Внести вклад</a>
  </p>
</div>
