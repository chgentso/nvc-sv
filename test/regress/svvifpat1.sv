// Assignment patterns, insert and copies of arrays of virtual interfaces
// Expected values: IEEE 1800-2017 7.10 (queue assignment pattern, insert, copy), 7.5 (dynamic
// array pattern), 25.9 (virtual interfaces), 13.5.1 (named arguments).
`timescale 1ns/1ns
interface svvifpat1_if #(parameter W = 8) (input logic clk);
  logic [W-1:0] d;
endinterface

module svvifpat1;
  int errors = 0;
  logic clk, c = 1;
  svvifpat1_if i8(clk);
  svvifpat1_if #(16) i16(clk);
  virtual svvifpat1_if #(8) q8[$], r8[$];
  virtual svvifpat1_if #(16) q16[$];
  virtual svvifpat1_if #(8) dyn8[];
  task automatic t(virtual svvifpat1_if #(8) x, int n); x.d = n; endtask
  initial begin
    q8 = '{i8, i8};
    q8.insert(1, i8);
    r8 = q8;
    dyn8 = '{i8};
    q16.push_back(i16);
    t(.n(7), .x(i8));
    if (q8.size() != 3 || r8.size() != 3 || dyn8.size() != 1 || i8.d !== 8'd7) begin
      $error("%0d %0d %0d %0d", q8.size(), r8.size(), dyn8.size(), i8.d); errors++;
    end
    begin automatic virtual svvifpat1_if #(8) ref8 = i8; foreach (q8[k]) if (q8[k] != ref8 || r8[k] != ref8) begin $error("handle %0d differs", k); errors++; end end
    if (dyn8[0].d !== 8'd7 || r8[2].d !== 8'd7) begin $error("read via handle"); errors++; end
    q16[0].d = 16'h1234;
    #1;
    if (i16.d !== 16'h1234) begin $error("i16.d=%h", i16.d); errors++; end
    if (errors == 0) $display("PASSED");
  end
endmodule
