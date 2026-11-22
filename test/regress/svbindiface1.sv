interface svbindiface1_if (
  input logic sig
);
  logic mirrored;
  assign mirrored = sig;
endinterface

module svbindiface1_mon (
  svbindiface1_if mon_if
);
  always @(mon_if.mirrored) begin
    if (mon_if.mirrored === 1'b1)
      $display("PASSED");
  end
endmodule

module svbindiface1_dut (
  input logic sig
);
endmodule

module svbindiface1;
  logic sig = 1'b0;

  svbindiface1_dut dut (.sig(sig));

  bind dut svbindiface1_if u_if (.sig(sig));
  bind dut svbindiface1_mon u_mon (.mon_if(u_if));

  initial begin
    #1 sig = 1'b1;
    #1 $finish;
  end
endmodule
