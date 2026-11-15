module ev_svattr3;
  wire [3:0] a, b;
  ev_svattr_mid #(.W(4)) u (.a(a), .b(b));
endmodule
