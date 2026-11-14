module linterr11_sub(input c);
endmodule

module linterr11;
  always @(posedge
  reg       c;
  integer   i;
  linterr11_sub u (.c(c));
  initial begin
    i = 5;
    c = i;
  end
endmodule
