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

module ports3d (o, p);
  parameter N = 8;
  localparam M = N + 2;
  output [7:0] o;
  wire [N-1:0] o;             // OK
  output [M-1:0] p;
  wire [9:0] p;               // OK
endmodule // ports3d

module ports3e (o);
  parameter N = 8;
  output [N:0] o;
  wire [7:0] o;               // Error
endmodule // ports3e
