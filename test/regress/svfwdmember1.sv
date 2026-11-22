module svfwdmember1;
  typedef struct packed {
    logic a;
    logic [3:0] vec;
  } s_t;

  logic out_a;
  logic out_bit;
  assign out_a = fwd_s.a;
  assign out_bit = fwd_s.vec[2];

  logic clk = 0;
  always #5 clk = ~clk;

  assert property (@(posedge clk) fwd_s.a |-> fwd_s.vec[2]);

  s_t fwd_s;

  initial begin
    fwd_s = '{a: 1'b1, vec: 4'b0100};
    #12;
    if (out_a !== 1'b1 || out_bit !== 1'b1)
      $fatal(1, "out_a=%b out_bit=%b", out_a, out_bit);
    $display("PASSED");
    $finish;
  end
endmodule
