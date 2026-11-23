// A continuous assignment to a variable changes it from x at time 0 just
// like an assignment to a net (IEEE 1800-2017 10.3.2) so a process waiting
// for the variable or an asynchronous reset sees the event
module prim_flop (input clk_i, input rst_ni, output logic q_o);
  always_ff @(posedge clk_i or negedge rst_ni)
    if (!rst_ni) q_o <= 1'b0;
    else         q_o <= 1'b1;
endmodule

module vlog_assign_init1;
  logic supp = 1'b0;
  logic other;
  logic rst_n_var, one_var;
  wire  rst_n_net;
  logic clk, q_var, q_net;
  assign rst_n_var = other && supp;
  assign rst_n_net = other && supp;
  assign one_var = 1'b1;
  prim_flop u_ffv (.clk_i(clk), .rst_ni(rst_n_var), .q_o(q_var));
  prim_flop u_ffn (.clk_i(clk), .rst_ni(rst_n_net), .q_o(q_net));
  always @(one_var) $display("%0t: one_var=%b", $time, one_var);
  initial begin
    #1 $display("%b %b %b %b", rst_n_var, q_var, rst_n_net, q_net);
    $finish;
  end
endmodule
