// force and release of a hierarchically named variable from an always block, in a generate scope
// Expected values: IEEE 1800-2017 10.6.2 (force overrides a nonblocking update that matures later in the same time
// step; release leaves the value in place until the next assignment), 10.4.2 (nonblocking assignment), 27.4
// (generate blocks, hierarchical names), 21.2.1.2 (%b of an all-x value). The relative order of the two always
// blocks at the same edge does not matter: both run in the active region before the nonblocking update.
`timescale 1ns/1ns
module svforce2;
  parameter EN = 1;
  reg clk = 0;
  reg [3:0] d = 0;
  int errs = 0;
  if (EN) begin : gen_rd
    reg [3:0] doutb_reg = 4'h5;
    always @(posedge clk) doutb_reg <= d;
  end
  if (EN) begin : gen_coll
    always @(posedge clk) begin
      if (d == 4'h3) force gen_rd.doutb_reg = 4'hx;
      else release gen_rd.doutb_reg;
    end
  end
  initial begin
    #1 clk = 1; #1 clk = 0; d = 3; #1 clk = 1; #1;
    if (gen_rd.doutb_reg !== 4'bxxxx) begin $error("forced value: got %b expected xxxx", gen_rd.doutb_reg); errs++; end
    $display("%b", gen_rd.doutb_reg);
    #1 clk = 0; d = 4; #1 clk = 1; #1;
    if (gen_rd.doutb_reg !== 4'b0100) begin $error("after release: got %b expected 0100", gen_rd.doutb_reg); errs++; end
    $display("%b", gen_rd.doutb_reg);
    if (errs == 0) $display("PASSED");
  end
endmodule
