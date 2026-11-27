// An oscillating Verilog continuous-assignment combinational loop must
// hit the delta cycle limit cleanly instead of hanging in reschedq, under
// both default RTL islands and --rtl-islands=off.

module vlog_comb_loop1;
  logic en = 1'b0;
  wire [8:0] w;

  assign w[0] = en & ~w[8];
  assign w[1] = w[0];
  assign w[2] = w[1];
  assign w[3] = w[2];
  assign w[4] = w[3];
  assign w[5] = w[4];
  assign w[6] = w[5];
  assign w[7] = w[6];
  assign w[8] = w[7];

  initial begin
    #1 en = 1'b1;
    #1 $display("not reached");
    $finish;
  end
endmodule
