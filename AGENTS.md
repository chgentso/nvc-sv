NVC is a mixed-language VHDL and SystemVerilog compiler and simulator.

The `build/` directory contains only compiled output and no source code.

To build the project run `make -C build`.

After changing node definitions, for example in `src/vlog/vlog-node.c`,
run `make -C build bootstrap` so serialized library formats are regenerated.

To run the unit tests use `make -C build check QUICK=1`.

Regression tests are listed in `test/regress/testlist.txt`.  New tests
should be appended at the end.  Run the regression runner from the build
directory to execute a single test:

    cd build
    bin/run_regr <test-name>

Equivalently, from the repository root:

    make -C build
    (cd build && bin/run_regr <test-name>)

To run the full test suite use `make -C build check`.  This can take a
long time so use it sparingly and prefer targeted tests.

Prefer writing tests that are self checking, either with asserts (VHDL)
or by printing PASSED/FAILED (Verilog).  If the output of the simulator
must be checked, place the expected output in `test/regress/gold` and
add the "gold" option to the entry in `testlist.txt`.  Avoid writing
shell script tests if possible.

There's no need to update `test/dist.mk`, this is generated
automatically before release.

Avoid running multiple tests or builds in parallel.

New regression tests should be appended to the end of
`test/regress/testlist.txt`.

Set the `NVC_LOWER_VERBOSE` environment variable to print the generated
IR.  E.g. `NVC_LOWER_VERBOSE=1` to print all units or
`NVC_LOWER_VERBOSE=foo` to print only `foo`.  Other `NVC_*_VERBOSE`
environment variables are available.

Keep edits minimal where possible.

---

# nvc-sv fork

Everything above is upstream nvc's own guidance and still applies. This section is specific to the nvc-sv fork
(branch `nvc-sv`; `master` mirrors upstream). `FORK.md` lists what the fork adds, `TODO.md` the known limits,
`NEWS.md` the changes per release, `MERGING.md` how upstream is merged.

## Differences that matter when you build and test

- The program is `bin/nvc-sv` (a link `bin/nvc` is also made in the build directory, for tools that look for
  `nvc`). Libraries install to `lib/nvc-sv`; `NVC_SV_<NAME>` is read before `NVC_<NAME>`.
- Configure from a Git clone with `./autogen.sh`, then in a separate build directory:
  `../configure --enable-verilog --enable-tcl` (SystemVerilog needs `--enable-verilog`; LLVM is found
  automatically), `make -j4`. Debug and sanitizer build: add `--enable-debug --enable-asan` and run with
  `ASAN_OPTIONS=detect_leaks=0`.
- Test commands from the build directory: `bin/unit_test` (C unit tests, `test/test_*.c`) and `bin/run_regr`
  (all regression tests) or `bin/run_regr NAME...` (selected). `make check` runs both. The regression list is
  `test/regress/testlist.txt` (about 2900 entries); a full `run_regr` takes a while, so run the tests for your
  area while iterating and the whole list before handing over a change. Run it in a release build and in the
  debug and ASan build: assertions that only exist in debug builds find real bugs.
- Verilog and SystemVerilog regression tests are `test/regress/*.sv` with the `verilog` flag (add `gold` when
  the output is compared with `test/regress/gold/NAME.txt`); mixed-language tests use `mixed`. Unit tests for
  the Verilog front end are in `test/test_vlog.c` with inputs under `test/vlog/`.
- Changing node definitions or the library format: run `make bootstrap` in the build directory, and delete the
  built `lib/*` folders in the build tree before the next `make` (an incremental build does not recompile
  them). After a clean configure and make, check that the standard libraries (including IEEE VITAL) still build:
  a new semantic check can reject nvc's own library sources, which only a fresh build recompiles.
- A fresh configure and make is part of the checks for any change to semantic checking or the libraries.

## Conventions of the fork

- Commit subjects use nvc's area prefixes in lower case: `vlog:`, `elab:`, `sem:`, `lower:`, `rt:`, `vpi:`,
  `vhpi:`, `lib:`, `lint:`, `cover:`, `NEWS:`. One commit per root cause, with a regression test in the same
  commit, and a body that says what was wrong and why the fix is right. Release commits are `Version X.Y.Z`.
- Fix the root cause the way nvc is designed. A construct that is erroneous gets a proper diagnostic; a large
  missing feature gets a clean "sorry, ... is not supported" message, never a crash.
- Copyright headers of files under `src/` name the original author, as upstream requires; new files copy an
  existing header with the current year. Contributors are credited in `THANKS.md`, not in headers.
- Tests are your own and minimal. Do not copy test cases from suites with licences that forbid it; reduce cases
  from open suites to the smallest self-checking test.
- `NEWS.md` gets a line per user-visible change, under the section of the next release (`## nvc-sv X.Y.Z`).
  `FORK.md` and the manual (`nvc-sv.1`, `www/`) change in the same commit as the feature they describe.
- The toolchain version is the `NVC_SV_VERSION` define in `configure.ac`; the IDE and the packaging repositories
  must agree with it (`nvc-sv --version` is what they check). Do not change it outside a release.
- The nvc version on line 1 of `configure.ac` and the package names follow the rules in `MERGING.md`.
- The published branch `nvc-sv` is append-only: add commits on top, never reorder or rewrite pushed commits,
  fix mistakes with a new commit. Stage explicit paths (no `git add -A`, no `git commit -a`). Do not push or
  open merge requests unless the maintainer asks.
- This repository is public: no machine-specific paths, host names or credentials, no names of commercial
  simulators in code, tests, comments or commit messages.
