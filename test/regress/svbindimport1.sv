package svbindimport1_pkg;
  parameter int BindWidth = 12;
  parameter int BindDepth = 4;
endpackage

interface svbindimport1_if #(
  parameter int Width = 1,
  parameter int Depth = 1
) (
  input logic [Width-1:0] data
);
  initial begin
    #1;
    if (Width !== 12 || Depth !== 4 || data !== 12'hABC)
      $fatal(1, "FAIL: Width=%0d Depth=%0d data=%h", Width, Depth, data);
    $display("PASSED");
  end
endinterface

module svbindimport1_target;
  logic [11:0] sig = 12'hABC;
endmodule

module svbindimport1_host
  import svbindimport1_pkg::*;
();
  bind svbindimport1_target svbindimport1_if #(
    .Width(BindWidth),
    .Depth(BindDepth)
  ) u_bind (
    .data(sig)
  );
endmodule

module svbindimport1;
  svbindimport1_target u_target();
  svbindimport1_host u_host();
endmodule
