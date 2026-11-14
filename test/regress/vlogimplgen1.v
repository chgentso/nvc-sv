// Implicit net declared by a port connection inside a loop generate
// without begin/end: the scope of the loop has no container
module sub(input a, output y);
  assign y = a;
endmodule

module vlogimplgen1;
  genvar i;
  for (i = 0; i < 2; i++)
    sub u(.a(und), .y());
  initial begin
    #1;
    $display("PASSED");
  end
endmodule
