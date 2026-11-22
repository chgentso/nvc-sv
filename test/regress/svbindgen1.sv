interface svbindgen1_if (
  input logic [7:0] val
);
  logic [7:0] mirrored;
  assign mirrored = val;
endinterface

module svbindgen1_bank #(parameter int ID = 0) (
  output logic [7:0] out
);
  logic [7:0] val;
  assign val = 8'(ID + 8'h10);
  assign out = val;
endmodule

module svbindgen1_dut (
  output logic [7:0] out0,
  output logic [7:0] out1
);
  logic [7:0] outs [2];
  for (genvar b = 0; b < 2; b++) begin : gen_banks
    svbindgen1_bank #(.ID(b)) u_bank (.out(outs[b]));
  end
  assign out0 = outs[0];
  assign out1 = outs[1];
endmodule

module svbindgen1;
  logic [7:0] o0, o1;
  svbindgen1_dut dut (.out0(o0), .out1(o1));

  for (genvar i = 0; i < 2; i++) begin : gen_bind
    bind svbindgen1.dut.gen_banks[i].u_bank svbindgen1_if u_if (.val);
  end

  initial begin
    #1;
    if (dut.gen_banks[0].u_bank.u_if.mirrored !== 8'h10)
      $fatal(1, "bank 0: %h", dut.gen_banks[0].u_bank.u_if.mirrored);
    if (dut.gen_banks[1].u_bank.u_if.mirrored !== 8'h11)
      $fatal(1, "bank 1: %h", dut.gen_banks[1].u_bank.u_if.mirrored);
    $display("PASSED");
    $finish;
  end
endmodule
