module lintvlogfix1;
  reg d = 1'b0;
  wire w_dead;
  reg [3:0] r_dead;      // never used
  wire w_a, w_b;
  reg r_written;
  reg r_init = 1'b0;
  wire w_ok = d;
  wire w_ok2 = d;
  always @(posedge w_ok) r_written <= d;
  initial $display("%b %b", w_ok, w_ok2);
endmodule
