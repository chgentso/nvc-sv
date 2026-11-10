// A diagnostic for text that comes from a macro used inside an included
// file is reported at the macro use in that file, not at the `include
// directive that brought the file in (IEEE 1800-2017 22.4 and 22.5.1).
`define BAD module

module pp22;

`include "pp22.vh"

  wire c = 1;
  wire wire wire;   // Error
endmodule // pp22
