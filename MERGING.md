# Merging upstream nvc

The fork keeps its changes to upstream files small so that merges stay
mostly mechanical.  Merge `upstream/master` into a scratch branch, then
expect conflicts only in these places.

- `configure.ac`, `AC_INIT`: line 1 is the nvc version (take upstream's),
  line 2 is the bug report address and line 4 is the tarname `nvc-sv` (keep
  ours).  `PACKAGE` becomes `nvc-sv` through the tarname; `PACKAGE_NAME` and
  `PACKAGE_VERSION` stay those of nvc on purpose, because the library
  format marker, the VHPI/VPI tool names and the FST writer string use
  them.  The three `AX_DEFINE_DIR` lines for `LIBDIR`, `LIBEXECDIR` and
  `DATADIR` end in `nvc-sv`.
- `configure.ac`, the `AC_DEFINE([NVC_SV_VERSION], ...)` line just after
  `AC_PREREQ`: the toolchain version, 0.16.0 now.  This is the one literal
  to change for a release; the IDE and the packaging read the same number
  and must agree (`nvc-sv --version` is what they check).
- `src/Makemodule.am`: the program is `bin/nvc-sv`, so the variables are
  `bin_nvc_sv_*`.  When upstream changes the link flags of `bin_nvc_*`
  apply the same change to `bin_nvc_sv_*`.  The `else` branch of
  `IMPLIB_REQUIRED` builds the `bin/nvc` link for the build tree.
  `lib/Makemodule.am` names the program in `nvc =` and `DRIVER`.
- `Makefile.am`, `contrib/Makemodule.am`: `nvc-sv.1` and `contrib/nvc-sv.bash`
  are generated from `nvc.1` and `contrib/nvc.bash` by `sed`; the sources
  keep their upstream names.  `contrib/nvc.pc.in` is configured into
  `contrib/nvc-sv.pc`.  Headers are installed with `pkginclude_HEADERS`.
- `src/util.c`, `src/util.h`: `nvc_getenv` (the `NVC_SV_` fallback, hooked up
  by a `#define getenv`) and `get_relocated_dir` (paths relative to the
  program).  New upstream `getenv("NVC_...")` calls and new uses of
  `PACKAGE` need no change.
- `test/run_regr.c` starts `bin/nvc-sv`; shell tests that call `nvc` run
  through the `bin/nvc` link.  New upstream shell tests need no change.

After a merge build in a fresh directory, run `make check`, and check that
`make install DESTDIR=...` installs nothing outside `nvc-sv` names.
