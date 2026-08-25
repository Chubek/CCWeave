# Specs for `toolchain/ccwmk`

## 1. Model

`ccwmk` is a `make` superset for CCWeave. It parses the ordinary Makefile language plus a set of extensions that pull it toward CMake's feature set: typed variables, `target`/`dependency` declarations that aren't just file paths, package/dependency fetching, and multi-language build graphs. Where GNU Make treats everything as files and shell recipes, `ccwmk` treats the graph as first-class: translation units, programs, libraries, and fetched packages are all nodes with typed edges, not just filesystem timestamps.

The file it reads is called a `Weavefile` (not `Makefile`), so a project directory has a `Weavefile` the way it would have a `Makefile`.

Parsing itself is delegated to `third_party/mpc` — the Weavefile grammar (targets, variables, rules, directives) is expressed as an `mpc` grammar and parsed into an AST that `ccwmk` walks to build the dependency graph.

`ccwmk` uses `ucpp` for one specific job: preprocessing C/C++ source to extract `#include` edges, so it can build accurate translation-unit dependency subgraphs without re-implementing a preprocessor. This is analogous to how `ccwld` presumably treats object files — `ccwmk` treats a `.c`/`.h` pair as nodes whose edges come from `ucpp`'s include resolution, not from hand-written `depend` lines.

Despite the C/C++-flavored dependency scanning, `ccwmk` is not C/C++-specific. It is the build orchestrator for every language CCWeave supports (anything with a `frontmx` grammar and a backend under `ccweave`). Language-specific dependency scanning (like the `ucpp` include scan) is one plugin among several; other languages register their own scanner.

## 2. Files and their contract

toolchain/ccwmk/
  ccw-ccwmk.c
  ccw-ccwmk.h
  src/
    parse.c        # drives third_party/mpc over the Weavefile grammar
    grammar.c       # the Weavefile grammar (targets, vars, rules, directives)
    ast.c           # Weavefile AST
    graph.c         # the dependency graph: nodes, edges, topological order
    scan-c.c        # ucpp-based include scanning for C/C++ nodes
    fetch.c         # resolves and retrieves dependencies from remote/defined targets
    exec.c          # recipe execution, job scheduling, parallelism
    plugin.c        # plugin loading and the extension ABI
    lua.c           # Lua frontend glue, used by lccwmk
  lua/
    lccwmk.c
    lccwmk.h


| File | Contract |
|---|---|
| `ccw-ccwmk.h` | Public surface: `ccwmk_load()`, `ccwmk_build()`, `ccwmk_graph_t`, `ccwmk_target_t`, the plugin registration ABI. |
| `ccw-ccwmk.c` | Driver: loads a `Weavefile`, builds the graph, resolves fetches, schedules and runs recipes. |
| `src/parse.c` | Feeds `Weavefile` text to an `mpc` parser built from `grammar.c`'s rules; produces the `ast.c` tree. |
| `src/grammar.c` | The `mpc` grammar definition: target syntax, variable syntax, rule syntax, directive syntax (`fetch`, `package`, `plugin`, `include`). |
| `src/ast.c` | The Weavefile AST: targets, recipes, variable bindings, directives. |
| `src/graph.c` | The dependency graph: one node per target/translation-unit/program/fetched-package, edges from declared or scanned dependencies, topological build order, cycle detection. |
| `src/scan-c.c` | Wraps `ucpp` to extract `#include` edges from C/C++ sources and folds them into `graph.c` as translation-unit dependencies. |
| `src/fetch.c` | Resolves `fetch` directives against defined targets (local or remote), materializes the dependency, and adds it to the graph before the build proceeds. |
| `src/exec.c` | Executes recipes bottom-up over the topological order, with parallel job scheduling analogous to `make -j`. |
| `src/plugin.c` | Loads plugins (shared objects implementing the `ccw-ccwmk.h` extension ABI) — this is how non-C/C++ languages register their own dependency scanner in place of `scan-c.c`. |
| `src/lua.c` | Embeds Lua and exposes the `ccwmk` graph/target API to Lua scripts, backing the `lccwmk` library. |
| `lua/lccwmk.c` / `lua/lccwmk.h` | The `lccwmk` library proper: a Lua module that lets a `Weavefile` (or a companion `.lua` file) drive `ccwmk` from Lua, mirroring how `lccwld` drives `ccwld`. |

## 3. The Weavefile language

 targis the ordinary Make language (targets, prerequisites, recipes, variables, `include`) plus the following extensions.

### 3.1 Typed targets

```make
target program hello
  language: c
  sources: src/main.c src/util.c
  deps: libmath
  cflags: -O2 -Wall

target library libmath
  language: c
  sources: src/math/*.c
  kind: static
```

A `target` block replaces the bare `file: prereqs` line when you want CCWmk to know *what kind* of thing it's building. `program` and `library` targets get automatic translation-unit dependency scanning via `scan-c.c` (or the registered scanner for `language:`); plain file targets behave exactly like ordinary Make rules.

### 3.2 Fetched dependencies

```make
fetch zlib
  from: target zlib-project/Weavefile#library
  version: "1.3"

fetch fmtlib
  from: git "https://example.invalid/fmtlib.git"
  ref: "10.1.1"
```

`fetch` resolves a dependency from another defined target (a target in another `Weavefile`, referenced by path and target name) or from an external source. `fetch.c` materializes it and inserts it as a graph node before the topological build order is computed, so a `deps:` line can name a fetched target like any local one.

### 3.3 Plugins

```make
plugin rust-scan
  path: plugins/rust-scan.so

target program tool
  language: rust
  sources: src/*.rs
```

A `plugin` directive loads a shared object implementing the scanner ABI declared in `ccw-ccwmk.h`. When a target's `language:` matches a plugin's registered language, `graph.c` calls the plugin's scan function instead of `scan-c.c`. This is how `ccwmk` stays language-general: C/C++ gets `ucpp` built in, everything else is a plugin.

### 3.4 Ordinary Make compatibility

Everything else — `%` pattern rules, `$@`/`$<`/`$^`, `.PHONY`, recursive variable expansion, `ifeq`/`ifdef`, `include` — parses and behaves as in GNU Make. `grammar.c` extends the Make grammar rather than replacing it, so an existing Makefile with `s/Makefile/Weavefile/` and no CCWmk-specific blocks builds unchanged.

## 4 `library`, o

`graph.c` is the core data structure. A node is one of:

- a file (as in ordinary Make),
- a translation unit (a source file plus its scanned includes),
- a typed target (`program`, `library`, or a language-specific kind a plugin registers),
- a fetched dependency.

Edges come from three sources, merged: explicit `deps:`/prerequisite declarations, scanned includes (`scan-c.c` or a plugin), and `fetch` resolution. `graph.c` topologically sorts the merged graph, detects cycles (reported as a `Weavefile:line: dependency cycle: A -> B -> A` diagnostic, not a silent infinite loop), and hands the order to `exec.c`.

Because language scanning is pluggable, a single `Weavefile` can mix a C `program` target (scanned via `ucpp`) with a Rust `library` target (scanned via a plugin) in one graph, with `ccwmk` treating both uniformly once their edges are resolved.

## 5. Embedding and the plugin interface

`ccwmk` is embeddable: `ccw-ccwmk.h` exposes `ccwmk_load()` / `ccwmk_build()` as library calls, not just a CLI entry point, so another CCWeave tool can load a `Weavefile`, inspect the resulting graph, and drive or observe the build programmatically.

The plugin ABI (declared in `ccw-ccwmk.h`, implemented in `src/plugin.c`) is the same mechanism used for language-specific scanning:

```c
typedef struct {
    const char *language;                 /* e.g. "rust" */
    int (*scan)(const char *path, ccwmk_edge_sink_t *sink);
} ccwmk_scanner_t;

int ccwmk_plugin_init(ccwmk_registry_t *registry);
```

A plugin's `ccwmk_plugin_init` registers one or more `ccwmk_scanner_t` entries; `graph.c` looks up a target's `language:` against the registry and falls back to `scan-c.c` only for `c`/`c++`.

## 6. `lccwmk`: the Lua frontend

Mirroring `lccwld`/`ccwld`, `lccwmk` is a Lua library backed by `src/lua.c`, letting a build be driven from Lua instead of (or alongside) Weavefile syntax.

```lua
local ccwmk = require("lccwmk")

local wf = ccwmk.weavefile()

wf:target("program", "hello", {
  language = "c",
  sources  = { "src/main.c", "src/util.c" },
  deps     = { "libmath" },
  cflags   = { "-O2", "-Wall" },
})

wf:target("library", "libmath", {
  language = "c",
  sources  = { "src/math/*.c" },
  kind     = "static",
})

wf:fetch("zlib", {
  from    = "target",
  path    = "zlib-project/Weavefile",
  target  = "library",
  version = "1.3",
})

wf:build("hello")
```

- `ccwmk.weavefile()` returns a handle wrapping a `ccwmk_graph_t`; `:target()`, `:fetch()`, and `:plugin()` are Lua-side equivalents of the corresponding Weavefile directives, calling straight into `graph.c` / `fetch.c` / `plugin.c` through `src/lua.c`'s bindings.
- `:build(name)` runs `exec.c` on the subgraph reachable from `name`.
- A `Weavefile` can itself invoke Lua for cases ordinary Make syntax can't express cleanly:

```make
lua
  local ccwmk = require("lccwmk")
  for _, f in ipairs(glob("plugins/*.lua")) do
    dofile(f)
  end
end
```

  `parse.c` recognizes a `lua ... end` block as a directive and hands its body to `src/lua.c` for execution against the current graph, the same way `mk` blocks are optional escape hatches in some Make variants.

The split of responsibility is: `ccwmk` (C coy) rsing, the graph, scanning, fetching, and execution; `lccwmk` (Lua library) is a thin binding so a `Weavefile` — or a `.lua` build script entirely — can drive that same core.