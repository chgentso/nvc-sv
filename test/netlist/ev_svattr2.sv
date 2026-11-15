// A module with an attribute instance, instantiated from another file
(* XPM_MODULE = "TRUE", KEEP_HIERARCHY = "SOFT" *)
module ev_svattr_leaf #(parameter integer W = 2) (input wire [W-1:0] a,
                                                   output wire [W-1:0] b);
  assign b = a;
endmodule

(* XPM_MODULE = "TRUE" *)
module ev_svattr_mid #(parameter integer W = 2) (input wire [W-1:0] a,
                                                  output wire [W-1:0] b);
  ev_svattr_leaf #(.W(W)) u (.a(a), .b(b));
endmodule
