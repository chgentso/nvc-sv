// bind of an interface into a hierarchical instance; the interface refers upward to a sibling instance and
// uses force/release on it (self-checking).
// Expected values: IEEE 1800-2017 23.11 (bind into d.u_core), 23.8 (upward name resolution: u_fsm is found in
// the scope core), 10.6.2 (force/release: while forced the variable keeps the forced value; after release a
// variable driven by a continuous assignment returns to the driven value), 9.4.1 (#delay), 15.5 (virtual interface).
// Reference model (clk rises at 5,15,...; falls at 10,20,...): reset until 22; cnt_q=0 at 5,15; 1 at 25,
// 2 at 35, 3 at 45; fast_mode=1 at 47; at the fall at 50 cnt_q==3, so cnt_d is forced to 3+10=13; cnt_q=13
// at 55; release at the fall at 60 (cnt_d = cnt_q+1 = 14); then +1 per rise: 65:14 ... 145:22; sampled at 147.
`timescale 1ns/1ns
interface fsm_if ();
  bit fast_mode;
  always @(negedge u_fsm.clk_i) begin
    if (fast_mode && u_fsm.cnt_q == 3) begin
      force u_fsm.cnt_d = u_fsm.cnt_q + 8'd10;
      @(negedge u_fsm.clk_i);
      release u_fsm.cnt_d;
    end
  end
endinterface

module fsm (input logic clk_i, input logic rst_ni);
  logic [7:0] cnt_q, cnt_d;
  assign cnt_d = cnt_q + 8'd1;
  always @(posedge clk_i or negedge rst_ni)
    if (!rst_ni) cnt_q <= 0; else cnt_q <= cnt_d;
endmodule

module core (input logic clk_i, input logic rst_ni);
  fsm u_fsm (.clk_i, .rst_ni);
endmodule

module dut (input logic clk_i, input logic rst_ni);
  core u_core (.clk_i, .rst_ni);
endmodule

module svbindinst1;
  logic clk = 0, rst_n = 0;
  always #5 clk = ~clk;
  dut d (.clk_i(clk), .rst_ni(rst_n));
  bind d.u_core fsm_if bound_if ();
  bit started = 0;
  int errs = 0;
  initial begin #1 started = 1; end
  virtual fsm_if vif;
  initial begin
    vif = d.u_core.bound_if;
    #22 rst_n = 1;
    #25 vif.fast_mode = 1;
    #100;
    if (!started) begin errs++; $error("start block did not run"); end
    if (d.u_core.u_fsm.cnt_q !== 8'd22) begin errs++; $error("cnt = %0d, expected 22", d.u_core.u_fsm.cnt_q); end
    if (errs == 0) $display("PASSED");
    $finish;
  end
endmodule
