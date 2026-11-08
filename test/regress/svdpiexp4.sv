// Exported DPI functions declared in a package, called from foreign code
// during static variable initialisation and from procedural code.
// IEEE 1800-2017 35.5.4 / 35.6 (export declarations in a package scope;
// the exported function is called in the package's context), 35.5.3
// (context imports may call exports), 6.8 / 10.5 (static variable
// initialisers run before any initial or always procedure).  The relative
// order of static initialisers is not specified, so the functions called
// during initialisation do not depend on other initialised variables.
// C side: test/dpi/dpi_export2.c (dpi_pk2_scale, dpi_pk2_base).
`define CHK(c, m) if (!(c)) begin err++; $error(m); end
package pk2;
  int base = 100;

  export "DPI-C" function pk2_scale;
  export "DPI-C" function pk2_base;

  function int pk2_scale(int a);
    return 3 * a;
  endfunction

  function int pk2_base();
    return base;
  endfunction

  import "DPI-C" context function int dpi_pk2_scale(int a);
  import "DPI-C" context function int dpi_pk2_base();

  int early = dpi_pk2_scale(7);   // during package initialisation
endpackage

module svdpiexp4;
  import pk2::*;

  int m_early = dpi_pk2_scale(5);   // during module initialisation
  int err = 0;

  initial begin
    `CHK(early == 21, "export called from a package initialiser")
    `CHK(m_early == 15, "export called from a module initialiser")
    `CHK(dpi_pk2_scale(-4) == -12, "export called from an initial procedure")
    `CHK(dpi_pk2_base() == 100, "package variable initialised before use")
    base = 7;
    `CHK(dpi_pk2_base() == 7, "export sees the current package variable")
    `CHK(pk2::dpi_pk2_base() == 7, "import called through the package name")
    if (err == 0) $display("PASSED");
  end
endmodule
