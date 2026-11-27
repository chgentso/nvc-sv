module bad1(.a(x[1:0]), .b({y, z}), .c(w));
  input [3:0] x;
  input y;
  output z;
  inout [1:0] w;
endmodule

module bad2(.q(nope));
endmodule
