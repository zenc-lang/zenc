# Zen C Symbol Mangling

This document describes how Zen C maps user-level names (functions, structs,
methods, generics, traits) to the C symbols emitted by the code generator.
It is meant as a guide for contributors and for anyone reading generated C or
debugger/crash output.

## Overview

Zen C symbols are built from `__`-separated segments. The scheme is designed to
be **readable** (the segments mirror the source) and **injective** (distinct
source declarations map to distinct symbols).

| Construct | Symbol | Example |
|:---|:---|:---|
| Global function | `Name` | `fib` |
| Module-qualified function | `Module__Name` | `math__sqrt` |
| Struct method | `Struct__Method` | `String__free` |
| Trait method | `Struct__Trait__Method` | `Vec__Drop__drop` |
| Generic method | `Struct__TypeArgs__Method` | `Vec__int32_t__push` |
| Enum | `Enum` | `Color` |

## The `__` separator

`__` separates segments. Because `__` can also appear inside a single
identifier, the following rule keeps the scheme injective:

> **A leading underscore of a method name is preserved.** When a method is
> named `_foo`, the emitted symbol keeps the resulting three-underscore run
> (`Struct___foo`), which is distinct from the plain `Struct__foo` of a method
> named `foo`.

Without this rule, `merge_underscores` would collapse `Struct___foo` to
`Struct__foo` and the two methods would collide.

## Underscore collapsing (`merge_underscores`)

Historical/legacy names sometimes contain runs of many underscores (e.g. from
older mangling or nested segments). `merge_underscores` normalizes them:

* A run of **4 or more** underscores collapses to **2** (`____` -> `__`).
* A run of **exactly 3** underscores is **preserved** — it encodes the `__`
  separator plus an identifier that begins with `_` (see above), so collapsing
  it would break injectivity.

## `link_name` / `extern` and C interop

Names controlled by the user that must match real C symbols are **not**
mangled:

* `@link("c_name")` / `link_name` attributes.
* `extern` declarations (which reference symbols directly).

These are emitted verbatim so that Zen C code can call into C libraries and be
called from C.

## Where mangling happens

Method symbols are produced by `mangle_method_symbol()` in
`src/parser/struct/struct_shared.c`, used by method registration
(`src/parser/struct/struct_impl.c`) and method resolution
(`src/parser/expr/expr_prec.c`). Generic method instantiation
(`src/parser/utils/utils_template_inst.c`) re-mangles the method name with the
concrete type arguments, preserving any leading underscore of the method name.

If you add a new place that constructs a `Struct__Method`-style symbol, use
`mangle_method_symbol()` so the injectivity rule stays consistent everywhere.
