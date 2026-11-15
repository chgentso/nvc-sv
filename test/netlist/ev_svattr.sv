// Attribute instances on modules, ports, nets and variables
(* top_attr = 3 *)
module ev_svattr (clk, d, q);
  (* port_attr = "p" *) input clk;
  input d;
  (* ASYNC_REG = "TRUE", SHREG_EXTRACT = "no" *) output reg q;
  (* keep *) wire w1, w2;
  (* a1 *) (* a2 = "x" *) reg [1:0] r;
  wire w3 = d;
  (* mark_debug = "true" *) wire w4 = d;
  assign w1 = d;
  always @(posedge clk) q <= w1;
endmodule

module ev_svattr_top (input wire clk, (* ansi_attr = "yes" *) input wire d,
                      output reg q);
  ev_svattr u (.clk(clk), .d(d), .q(q));
endmodule
