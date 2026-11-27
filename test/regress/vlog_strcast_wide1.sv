module vlog_strcast_wide1;
  string s;
  logic [79:0] v = "ABCDEFGHIJ";
  logic [71:0] w = 72'h00_41_42_43_44_45_46_47_48;
  initial begin
    $display("[%s]", string'("SUSPENDED"));
    s = string'(v);
    $display("[%s] %0d", s, s.len());
    s = string'(w);   // Leading NUL dropped
    $display("[%s] %0d", s, s.len());
  end
endmodule
