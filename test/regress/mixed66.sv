module mixed66_leaf (
  input  logic       clk,
  output logic [7:0] val
);
  logic [7:0] cnt = 8'h10;
  always_ff @(posedge clk)
    cnt <= cnt + 8'h01;
  assign val = cnt;
endmodule

module mixed66;
  logic clk = 0, do_frc = 0, do_rel = 0;
  wire [7:0] obs;
  always #5 clk = ~clk;

  mixed66_mid u_mid (.clk(clk), .do_frc(do_frc), .do_rel(do_rel), .obs(obs));

  initial begin
    @(posedge clk); #1;
    if (obs !== 8'h10) $fatal(1, "init obs=%h exp=10", obs);

    @(posedge clk); #1;
    if (obs !== 8'h11) $fatal(1, "step obs=%h exp=11", obs);

    do_frc = 1;
    @(posedge clk); #1;
    do_frc = 0;
    if (u_mid.u_leaf.val !== 8'hAA) $fatal(1, "force val=%h exp=aa", u_mid.u_leaf.val);

    @(posedge clk); #1;
    if (obs !== 8'hAA) $fatal(1, "hold obs=%h exp=aa", obs);

    do_rel = 1;
    @(posedge clk); #1;
    do_rel = 0;
    @(posedge clk); #1;
    if (u_mid.u_leaf.val === 8'hAA) $fatal(1, "release did not resume: %h", u_mid.u_leaf.val);

    $display("PASSED");
    $finish;
  end
endmodule
