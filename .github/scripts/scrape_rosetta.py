#!/usr/bin/env python3
"""Rosetta Code Zen C auto-scraper.

Scrapes Zen C solutions from Rosetta Code and writes one .zc file per *valid*
solution. Robust handling:

  * A task's "Zen C" section often contains several <lang>/<syntaxhighlight>
    blocks. They may be (a) separate complete solutions, (b) parts of one
    program, or (c) a module plus a main that imports it.
  * We split blocks, classify each as a "program" (has a top-level `fn main`)
    or a "module" (no main). Every program is kept -- but only if it passes
    validation (zc transpile + build when zc is available, otherwise a
    structural check). Separate solutions therefore become separate files
    ({title}.zc, {title}_2.zc, ...) instead of being merged into one broken
    file.
  * A program that does `import "NAME.zc"` where a module block declares
    `/* NAME.zc */` gets that module inlined (the import line is replaced by
    the module body), turning a multi-file solution into a single valid file.
  * Blocks are NOT filtered by their lang= attribute: contributors commonly
    mislabel Zen C blocks (e.g. lang="rust"), but the section is already
    scoped to the Zen C header.
"""

import json
import os
import re
import subprocess
import sys
import tempfile
import urllib.request as urllib_request
from urllib.parse import quote as urllib_quote

API_URL = "https://rosettacode.org/w/api.php"
CATEGORY = "Category:Zen_C"
UA = "Zen-C-AutoScraper/1.0"

ZC_BINARY = os.environ.get("ZC_BINARY", "./zc")
ZC_ROOT = os.environ.get("ZC_ROOT", os.getcwd())


def fetch_json(url):
    req = urllib_request.Request(url, headers={"User-Agent": UA})
    with urllib_request.urlopen(req) as response:
        return json.loads(response.read().decode())


def wiki_to_markdown(wiki_text, page_url):
    # Convert <lang> or <syntaxhighlight> blocks
    def repl_code(match):
        return "\n```zc\n%s\n```\n" % match.group(1).strip()

    md = re.sub(
        r"(?:<lang[^>]*>|<syntaxhighlight[^>]*>|<highlight[^>]*>)(.*?)(?:</lang>|</syntaxhighlight>|</highlight>)",
        repl_code,
        wiki_text,
        flags=re.DOTALL | re.IGNORECASE,
    )

    md = re.sub(r"\{\{out\}\}", r"\n**Output:**\n", md, flags=re.IGNORECASE)

    def repl_pre(match):
        return "\n```\n%s\n```\n" % match.group(1).strip()

    md = re.sub(r"<pre[^>]*>(.*?)</pre>", repl_pre, md, flags=re.DOTALL | re.IGNORECASE)

    def repl_header(match):
        level = len(match.group(1))
        content = match.group(2).strip()
        return "\n%s %s\n" % ("#" * level, content)

    md = re.sub(r"^(=+)\s*(.*?)\s*\1\s*$", repl_header, md, flags=re.MULTILINE)

    md = re.sub(r"\[\[([^\]|]+)\|([^\]]+)\]\]", r"[\2](https://rosettacode.org/wiki/\1)", md)
    md = re.sub(r"\[\[([^\]]+)\]\]", r"[\1](https://rosettacode.org/wiki/\1)", md)
    md = re.sub(r"'''(.*?)'''", r"**\1**", md)
    md = re.sub(r"''(.*?)''", r"*\1*", md)
    md = re.sub(r"\n{3,}", "\n\n", md)
    return md.strip()


# --- Block classification ---------------------------------------------------

BLOCK_RE = re.compile(
    r'<(?:lang|syntaxhighlight|highlight)\s+lang=["\']?([^"\'>]*)["\']?[^>]*>(.*?)'
    r"</(?:lang|syntaxhighlight|highlight)>",
    re.DOTALL | re.IGNORECASE,
)
MAIN_RE = re.compile(r"(?m)^\s*fn\s+main\s*\(")
MODULE_HEADER_RE = re.compile(r"^\s*/\*\s*([\w.-]+\.zc)\s*\*/")
IMPORT_RE = re.compile(r'^\s*import\s+"([^"]+)"\s*;?\s*$', re.MULTILINE)

# For solutions that reuse a helper defined in a sibling block of the same task
# (e.g. a second variant whose comment says "reusing the procedure defined
# above"), we inline the needed definitions so the emitted file is standalone.
MAIN_FN_RE = re.compile(r"(?m)^(\s*)fn\s+main\s*\(")
FN_NAME_RE = re.compile(r"(?m)^\s*fn\s+([A-Za-z_]\w*)\s*\(")
# gcc quotes the missing function with curly quotes (`‘times’`) or ASCII ('times').
IMPLICIT_DECL_RE = re.compile(r"implicit declaration of function [\u2018']([A-Za-z_]\w*)")


def _rename_main(code, new_name):
    """Rename the first top-level `fn main` so a sibling's program body can be
    inlined without colliding with the importing program's own main."""

    def repl(m):
        return "%sfn %s(" % (m.group(1), new_name)

    return MAIN_FN_RE.sub(repl, code, count=1)


def _fn_names(code):
    """Set of top-level `fn NAME(` definitions present in `code`."""
    return set(FN_NAME_RE.findall(code))


def split_blocks(section):
    """Return a list of (lang, code) blocks from the Zen C section."""
    blocks = []
    for m in BLOCK_RE.finditer(section):
        lang = m.group(1) or ""
        code = m.group(2).strip()
        if code:
            blocks.append((lang, code))
    return blocks


def is_program(code):
    return MAIN_RE.search(code) is not None


def module_name(code):
    m = MODULE_HEADER_RE.match(code)
    return m.group(1) if m else None


def structural_check(code, known_modules=()):
    """Cheap check used when zc is unavailable. Returns (ok, reason)."""
    if not code.strip():
        return False, "empty code block"
    mains = MAIN_RE.findall(code)
    if len(mains) != 1:
        return False, "expected exactly one top-level fn main (got %d)" % len(mains)
    for m in IMPORT_RE.finditer(code):
        name = m.group(1)
        if name.endswith(".zc") and "/" not in name and name not in known_modules:
            return False, "dangling module import: %s" % name
    return True, ""


def _zc_run(code, module_files, args, out_name):
    """Stage `code` (plus `module_files` next to it) in a temp dir and run
    `zc <args> src -o <tmp>/out_name`. Returns (returncode, stderr)."""
    with tempfile.TemporaryDirectory(prefix="zc_rosetta_") as td:
        src = os.path.join(td, "prog.zc")
        out = os.path.join(td, out_name)
        with open(src, "w", encoding="utf-8") as f:
            f.write(code)

        for name, mcode in (module_files or {}).items():
            with open(os.path.join(td, name), "w", encoding="utf-8") as f:
                f.write(mcode)

        env = dict(os.environ)
        if ZC_ROOT:
            env["ZC_ROOT"] = ZC_ROOT

        try:
            r = subprocess.run(
                [ZC_BINARY] + args + [src, "-o", out],
                capture_output=True, text=True, env=env, timeout=300,
            )
        except (OSError, subprocess.TimeoutExpired) as exc:
            return 1, "validation error: %s" % exc
        return r.returncode, r.stderr


def validate_code(code, module_files=None):
    """Validate a candidate program. Returns (ok, reason).

    `module_files` maps module filename -> source; those are staged next to the
    program so `import "NAME.zc"` resolves exactly as it will in the committed
    layout. When zc is available we run `zc transpile` and `zc build`; otherwise
    we fall back to a structural check so the scraper still works without the
    compiler binary.

    gcc only promotes `implicit declaration of function` to an error in newer
    C23 compilers; on older ones it is a warning, so a program relying on an
    undeclared function would build here but not elsewhere. We treat any such
    diagnostic as a failure so validation is independent of the gcc version.
    """
    if not os.path.exists(ZC_BINARY):
        return structural_check(code, known_modules=tuple((module_files or {}).keys()))

    rc, err = _zc_run(code, module_files, ["transpile"], "prog.c")
    if rc != 0:
        return False, _first_error(err)
    rc, err = _zc_run(code, module_files, ["build"], "prog")
    if rc != 0:
        return False, _first_error(err)
    if IMPLICIT_DECL_RE.search(err or ""):
        return False, "references undeclared function(s)"
    return True, ""


def merge_dependencies(program, siblings, module_files=None):
    """Make `program` standalone by inlining helpers it needs from sibling
    program blocks of the same task.

    Rosetta solutions sometimes reuse a helper defined in a sibling block of the
    same task (e.g. a second variant that says "reusing the procedure defined
    above"). Such a block is not standalone: `zc build` reports an undeclared
    function (an error on newer gcc, a warning on older ones -- see
    validate_code). When we detect that, we prepend the sibling block that
    defines the missing function (with its `fn main` renamed so it cannot clash
    with the program's own main) and re-validate, repeating until the program
    builds on its own. Returns the merged code if it builds, else None.
    """
    if not os.path.exists(ZC_BINARY):
        return None

    current = program
    merged_siblings = []
    for _ in range(len(siblings) + 1):
        rc, stderr = _zc_run(current, module_files, ["build"], "prog")
        missing = set(IMPLICIT_DECL_RE.findall(stderr or ""))
        if not missing:
            return current if rc == 0 else None
        missing = set(IMPLICIT_DECL_RE.findall(stderr or ""))
        if not missing:
            return None
        provider = next(
            (s for s in siblings
             if s not in merged_siblings
             and MAIN_FN_RE.search(s)
             and _fn_names(s) & missing),
            None,
        )
        if provider is None:
            return None
        merged_siblings.append(provider)
        current = "%s\n\n%s" % (
            _rename_main(provider, "__z_imported_main_%d" % len(merged_siblings)),
            current,
        )
    return None


def _first_error(stderr):
    for line in stderr.splitlines():
        line = line.strip()
        if line.startswith("error:"):
            return line
    return (stderr or "validation failed").strip().splitlines()[-1][:200]


def main():
    print("-> Fetching tasks from Rosetta Code...")

    pages = []
    cm_continue = {}
    while True:
        url = (
            API_URL + "?action=query&list=categorymembers&cmtitle=" + CATEGORY
            + "&cmlimit=500&format=json"
        )
        for key, val in cm_continue.items():
            url += "&%s=%s" % (key, urllib_quote(val))
        data = fetch_json(url)
        pages.extend(data["query"]["categorymembers"])
        cm_continue = data.get("continue", {})
        if not cm_continue:
            break

    print("-> %d tasks found" % len(pages))

    os.makedirs("examples/examples/rosetta", exist_ok=True)
    os.makedirs("website_out", exist_ok=True)

    # Pass 1: fetch and split every task, and collect the module files tasks
    # define (via a `/* NAME.zc */` header, e.g. Arithmetic/Rational's rat.zc).
    # Modules are emitted once as shared sibling files so `import "NAME.zc"`
    # resolves both within and across tasks.
    tasks = []
    module_files = {}  # filename -> source (first definition wins)

    for page in pages:
        title = page["title"]
        pageid = page["pageid"]

        content_url = (
            API_URL + "?action=query&prop=revisions&rvprop=content&rvslots=main"
            + "&pageids=%d&format=json" % pageid
        )
        content_data = fetch_json(content_url)
        text = content_data["query"]["pages"][str(pageid)]["revisions"][0]["slots"]["main"]["*"]

        parts = re.split(r"==\{\{header\|Zen[ _-]?C\}\}==", text, flags=re.IGNORECASE)

        if len(parts) <= 1:
            print("-> Could not find Zen C header in: %s" % title)
            continue

        # Stop at the next section header, case-insensitively: Rosetta uses
        # both `{{header|...}}` and `{{Header|...}}`. A case-sensitive split
        # would leak a following section (e.g. `{{Header|Zig}}`) into the Zen C
        # section.
        zen_c_section = re.split(r"(?i)==\{\{header\|", parts[1], maxsplit=1)[0].strip()
        blocks = split_blocks(zen_c_section)

        if not blocks:
            print("-> Found header, but NO code block in: %s" % title)
            continue

        programs = [code for _, code in blocks if is_program(code)]
        if not programs:
            print("-> No complete program in Zen C section of: %s" % title)
            continue

        safe_title = title.replace("/", "_").replace(" ", "_")
        page_url = "https://rosettacode.org/wiki/" + title.replace(" ", "_")
        tasks.append(
            {
                "title": title,
                "safe_title": safe_title,
                "page_url": page_url,
                "history_url": page_url + "?action=history",
                "zen_c_section": zen_c_section,
                "blocks": blocks,
                "programs": programs,
            }
        )

        for _, code in blocks:
            if not is_program(code):
                name = module_name(code)
                if name and name not in module_files:
                    module_files[name] = code

    # Emit shared module files once.
    produced = set()
    for name, code in module_files.items():
        with open("examples/examples/rosetta/%s" % name, "w", encoding="utf-8") as f:
            f.write(code + "\n")
        produced.add(name)
        print("-> Module: %s" % name)

    scraped = len(tasks)
    kept = 0
    skipped = 0

    # Pass 2: validate and emit each program (with the shared modules staged),
    # then write the documentation for the task.
    for task in tasks:
        title = task["title"]
        blocks = task["blocks"]
        written = 0
        for idx, program in enumerate(task["programs"]):
            ok, reason = validate_code(program, module_files)
            if not ok:
                merged = merge_dependencies(program, task["programs"], module_files)
                if merged is not None:
                    program = merged
                    ok = True
                    print("  ~ %s: block %d inlined helper(s) from sibling blocks" % (title, idx + 1))
                else:
                    print("  ! %s: block %d rejected (%s)" % (title, idx + 1, reason))
                    continue

            suffix = "" if written == 0 else "_%d" % (written + 1)
            zc_filename = "examples/examples/rosetta/%s%s.zc" % (task["safe_title"], suffix)
            with open(zc_filename, "w", encoding="utf-8") as f:
                f.write(program + "\n")
            produced.add(os.path.basename(zc_filename))
            written += 1
            kept += 1

        if written == 0:
            skipped += 1
            print("-> No valid program for: %s" % title)
            continue

        # Documentation (markdown) mirrors the whole Zen C section.
        md_filename = "website_out/%s.md" % task["safe_title"]
        content_md = wiki_to_markdown(task["zen_c_section"], task["page_url"])
        with open(md_filename, "w", encoding="utf-8") as f:
            f.write("+++\n")
            f.write('title = "%s"\n' % title)
            f.write("+++\n\n")
            f.write("# %s\n\n" % title)
            f.write(content_md + "\n\n")
            f.write("---\n")
            f.write(
                "**Attribution:** This is a community solution for the Rosetta Code task "
                "[**%s**](%s) in Zen C.\n\n" % (title, task["page_url"])
            )
            f.write(
                "*This article uses material from the Rosetta Code article **%s**, which is "
                "released under the [GNU Free Documentation License "
                "1.3](https://www.gnu.org/licenses/fdl-1.3.html). A list of the original "
                "authors can be found in the [page history](%s).*\n"
                % (title, task["history_url"])
            )

        print("-> Scraped: %s (%d blocks, %d valid program(s))" % (title, len(blocks), written))

    # Prune stale files. The awesome-zenc rosetta dir is fully auto-generated,
    # but the sync workflow only ever stages additions. Without pruning, a
    # solution that stops validating (e.g. the Rosetta page was edited after the
    # last sync) would linger forever as a broken file. Remove any *.zc that
    # this run did not produce.
    pruned = 0
    for fname in os.listdir("examples/examples/rosetta"):
        if fname.endswith(".zc") and fname not in produced:
            os.remove(os.path.join("examples/examples/rosetta", fname))
            pruned += 1
            print("-> Pruned stale file: %s" % fname)

    print("--------------------------------------------------")
    print("Tasks with a Zen C section : %d" % scraped)
    print("Modules emitted            : %d" % len(module_files))
    print("Programs kept              : %d" % kept)
    print("Tasks with no valid program: %d" % skipped)
    print("Stale files pruned         : %d" % pruned)


if __name__ == "__main__":
    main()
