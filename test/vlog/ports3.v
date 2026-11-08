module ports3a (o);
  function integer clogb2;
    input [31:0] value;
    begin
      for (clogb2 = 0; value > 0; clogb2 = clogb2 + 1)
        value = value >> 1;
    end
  endfunction
  output [clogb2(255):2] o;
  wire [clogb2(255):2] o;     // OK
endmodule // ports3a

module ports3b (o);
  function integer clogb2;
    input [31:0] value;
    begin
      for (clogb2 = 0; value > 0; clogb2 = clogb2 + 1)
        value = value >> 1;
    end
  endfunction
  output [clogb2(255):2] o;
  wire [clogb2(15):2] o;      // Error
endmodule // ports3b

module ports3c (o);
  function integer clogb2;
    input [31:0] value;
    begin
      for (clogb2 = 0; value > 0; clogb2 = clogb2 + 1)
        value = value >> 1;
    end
  endfunction
  output [clogb2(255):2] o;
  wire [8:2] o;               // OK
endmodule // ports3c
