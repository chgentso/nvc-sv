# What nvc-sv adds over nvc

nvc-sv follows the development line of [nvc](https://github.com/nickg/nvc)
after release 1.23.0.  All upstream nvc features are present.  This file
lists what the fork adds.  The manual and [NEWS.md](NEWS.md) have the
details of each option.  Known limits are in [TODO.md](TODO.md).

## SystemVerilog and UVM

- A SystemVerilog front end: parsing, semantic checks, elaboration and
  simulation of modules, interfaces, programs, packages, classes,
  generate constructs, `bind`, `defparam`, hierarchical references and
  `final` procedures.
- The body of a `for` generate loop is compiled once for all of its
  iterations when the genvar is only used where its value can be computed
  as each iteration is created, and the generate blocks of a module are
  compiled once for all instances of the module with the same parameters.
  Each iteration keeps its own scope.  `NVC_GEN_SHARE=off` compiles every
  iteration separately and `NVC_GEN_SHARE_VERBOSE=1` reports the loops
  which are not shared and why.
- Classes with inheritance, virtual methods, parameterised classes,
  `rand` properties, `super`, `this` and `$cast`.
- Interfaces with modports, virtual interfaces, arrays of interface
  instances and clocking blocks.
- Queues, dynamic and associative arrays, strings, mailboxes, events,
  packed and unpacked structures and unions, enumerations with their
  methods, `type` parameters and `nettype`.
- `fork` with `join`, `join_any` and `join_none`, `wait fork`, `disable`,
  `ref` arguments, and `always_comb`, `always_ff` and `always_latch`.
- `$error` counts as an error severity failure and `$fatal` ends the run
  with a non-zero status.  `--exit-severity` changes the threshold.
- The UVM 1.2 library compiles and runs: factory, phases, sequences,
  configuration database, TLM, objections and the command line
  processor.  The packages carry a precompiled UVM library on the library
  path, so `import uvm_pkg::*;` needs no recompilation.  The `uvm_hdl_*`
  and `uvm_re_*` DPI routines are built in.
- Packages, interfaces and virtual interface types from other libraries
  are found on the `-L` path.
- `covergroup`, `coverpoint` and `cross` are parsed and checked.
- `--relaxed` accepts some constructs that other tools accept.  An error
  that it would downgrade says so, and the JSON messages carry
  `"relaxable":true`.
- `--black-box-missing[=LIB,...]` elaborates an instance of a unit that
  cannot be found as an empty black box.  The design cannot be run, but
  `--netlist` draws the boxes.

## Mixed VHDL and Verilog

- VHDL entities can be instantiated from Verilog modules, with the usual
  port modes and types and generics set by parameter overrides.
- Verilog modules can be instantiated from VHDL as `entity work.name` or
  bound to components.
- Types of a VHDL package can be imported into SystemVerilog.  VHDL
  record ports can be connected from Verilog.  Hierarchical references
  from Verilog can read VHDL signals.
- Hierarchical names at the boundary have one level per instance:
  `'path_name`, `%m`, waveform scopes, coverage scopes and `--netlist`
  modules agree.

## Constrained random

- `randomize()` and `randomize() with`, `rand` and `randc`, `rand_mode`,
  `constraint_mode`, `soft` constraints, `solve ... before`,
  `std::randomize`, `inside`, `dist`, implications and `foreach`.
- Variables wider than 64 bits, arrays of packed structures with random
  indexes and function calls in constraints.
- `NVC_CRV_SKIP` ignores constraints by index, to debug a failing
  randomisation.

## DPI, VPI and VHPI

- DPI-C: `import "DPI-C"` functions and tasks with the common scalar,
  string, `chandle` and packed vector arguments.  Shared libraries are
  given with the repeatable `--sv-lib` option.  `export "DPI-C"` calls
  back into the design with the scope functions of `svdpi.h`.
- VPI: `cbValueChange` on values held in memory, the class model
  (`vpiClassVar`, `vpiClassObj`, ...), dynamic arrays, queues, associative
  arrays and enumeration typespecs.  Extensions are declared in
  `vpi_ext_nvc.h`: `vpi_nvc_connections` (what drives a signal),
  `vpi_nvc_set_diag_cb` (every assertion or report message),
  `vpi_nvc_object_members`, `vpi_nvc_dynamic_type`,
  `vpi_nvc_object_valid`, `vpi_nvc_object_at` and `vpi_nvc_pin`.
- `--load` can be repeated to load several VHPI or VPI plugins.

## Assertions

- SystemVerilog concurrent and immediate assertions: sequences,
  properties, `assert`, `assume`, `cover`, `expect`, `disable iff`,
  default clocking, local variables, the sampled value functions,
  assertion modules attached with `bind`, and assertion control tasks.
  Not every form is accepted yet.
- PSL in VHDL-2008 and VHDL-2019: verification units bound to an entity,
  architecture or instance, parameterised sequences and properties,
  `forall` and `for` replication, SERE and LTL operators, and the
  built-in functions `prev`, `next`, `stable`, `rose`, `fell` and
  `nondet`.
- The VHDL-2019 `STD.ENV` PSL subprograms (`PslIsCovered`,
  `PslAssertFailed`, ...).

## VHDL

- Support for VHDL-2019 features such as composite types of protected and
  file types, protected types with generics, and anonymous types in
  interface declarations.
- Many parser, semantic and library conformance fixes found with the
  VHDL conformance suites.
- `-e lib.unit` accepts a library qualified name.

## Lint

- `--lint[=RULES]` reports legal but suspicious VHDL and Verilog code as
  warnings tagged with the rule, such as `[lint:width]`.  Nothing is
  checked without the option.
- Rules: `unused`, `undriven`, `latch`, `width` (Verilog), `sensitivity`
  (VHDL), and the Verilog rules `implicit-net`, `blocking`,
  `case-default`, `unconnected` and `multidriven`.  `--lint=list` prints
  them.  `--lint=all,-width` selects rules.
- A comment `// nvc lint_off RULE` (Verilog) or `-- nvc lint_off RULE`
  (VHDL) on the line or the line before suppresses a rule.
- Some rules offer a fix as text edits.  Lint carries on after a syntax
  error.
- `--messages=json` prints each message as a line of JSON with level,
  text, rule, range, related locations and fixes.  The language server
  reads it.

## Coverage

- `--cover` works for Verilog and SystemVerilog and for designs that mix
  them with VHDL.  Each module instance and generate block is a scope at
  its path, with statement and branch items.
- The XML export marks module scopes with `kind="module"`.
- Not covered yet: code in classes and packages, and toggle coverage of
  Verilog signals.

## Netlist, scan and tool interfaces

- `--netlist` writes a block schematic of an elaborated design as Yosys
  JSON, for netlistsvg and compatible viewers.  `--top-only` and
  `--depth=N` limit the levels.  Large arrays are written as `memories`.
  The output is the same from one build to the next.
- `--netlist --evidence` adds what a clock domain analysis needs: process
  class, clocks and edges, asynchronous controls, per-output dependencies
  and signal attributes (`async_reg`, `keep`, `cdc_*`).
- `--scan` prints, as one line of JSON per source file, the design units
  the file provides and requires, without needing any library.
  `--symbols` adds declarations and references.  Both are meant for
  editors and language servers.

## Debugging and waveforms

- Runtime hooks for debuggers: variables of subprogram frames, call
  stack, breakpoint sites, process-limited stepping and signal loads.
- `--debug-stmts` instruments statements for source line breakpoints and
  stepping.
- `--surfer` starts the Surfer waveform viewer connected to the
  simulation, using the nvc-surfer plugin.
- `--wave-flush` keeps the FST file written by `--wave` readable while
  the simulation runs.

## Windows

nvc-sv builds and runs on Windows x64 with MSYS2.

## Names, environment and folders

- The program is `nvc-sv`.  The files are in `lib/nvc-sv`,
  `libexec/nvc-sv`, `share/nvc-sv` and `include/nvc-sv`, with `nvc-sv(1)`
  and `nvc-sv.pc`, so nvc-sv and nvc can share a prefix.  The VHDL
  library `nvc`, the `nvc lint_off` comment, the plugin names and the
  `NVC_` variables keep their names.
- `NVC_SV_<NAME>` is read first and `NVC_<NAME>` as a fallback.
- Libraries installed with `nvc-sv --install` are in `~/.nvc-sv/lib`.
  `~/.nvc/lib` is not read.
- Started through a link called `nvc`, the program prints the upstream
  `--version` format, so VUnit and cocotb keep working.
- An installed tree can be moved: the libraries are found relative to the
  program, also through symlinks.

| What | Location |
|---|---|
| State of the extension and runner | `~/.local/state/nvc-sv/` (or `$XDG_STATE_HOME`) on Linux, `%LOCALAPPDATA%\nvc-sv` on Windows, one folder per project; `DIR/.nvc-sv` with `--state-in-project` |
| Libraries from `nvc-sv --install` | `~/.nvc-sv/lib` |
| Standard libraries and UVM in a package | `<package>/lib/nvc-sv` |
| Install scripts of `nvc-sv --install` | `<package>/libexec/nvc-sv` |
| Examples, UVM sources, recording packages | `<package>/share/nvc-sv` |

## Known limits

[TODO.md](TODO.md) has the full list.  The main ones:

- An `@(posedge v)` waiter can miss a 0, 1, 0 pulse made inside one
  activation of an `always_comb` or `always_latch` process.
- Variable capture by `fork ... join_none` is a heuristic, not the
  lifetime model of IEEE 1800-2017.
- `rand_mode()` works only on the first 64 `rand` properties of a class.
- A `bind` directive does not see names that the target module gets
  through a wildcard package import.
- Some constraints on variables wider than 64 bits can fail.
- SystemVerilog support is large but still evolving.  Report what you
  find with a small example.
