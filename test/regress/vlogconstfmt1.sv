// $sformatf of constant arguments in a localparam, including %m
module sub;
  localparam string S = $sformatf("%m");
  localparam string T = $sformatf("%0d-%s-%h-%b-%5d|%x", 12, "ab", 8'hab,
                                  4'b101, 7, 16'hff);
  initial $display("%s %s", S, T);
endmodule

module vlogconstfmt1;
  sub u1();
  sub u2();
endmodule
