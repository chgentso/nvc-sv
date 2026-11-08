// Illegal uses of DPI open array formals.
// IEEE 1800-2017 35.5.6.1 (unsized dimensions are allowed only in formal
// arguments of imported subroutines), 7.7 (a dynamic array cannot be passed
// to an unsized output formal of an import; the element types of array
// arguments must be equivalent), 7.6.
module dpiopen1;
  import "DPI-C" function void imp_in(input int x[]);
  import "DPI-C" function void imp_out(output int x[]);
  import "DPI-C" function void imp_inout(inout int x[]);
  import "DPI-C" function void imp_2d(input int x[][]);

  export "DPI-C" function exp_open;              // Error
  function void exp_open(input int x[]);
  endfunction

  function void plain(input bit [] v);           // Error
  endfunction

  int dyn[];
  int fixed4[4];
  shortint s4[4];
  int k;

  initial begin
    imp_in(dyn);                                 // OK
    imp_inout(dyn);                              // OK
    imp_out(fixed4);                             // OK
    imp_out(dyn);                                // Error
    imp_in(s4);                                  // Error
    imp_in(k);                                   // Error
    imp_2d(fixed4);                              // Error
  end
endmodule
