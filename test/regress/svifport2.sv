// Expected values: IEEE 1800-2017 25.3/25.5 (interface ports, modports; non-ANSI port declarations of interface
// type), 23.2.2.? (non-ANSI module headers), 9.4.2 (posedge), 4.9/10.4.2 (nonblocking updates are seen only after
// the clock edge's active region, so a consumer sampling at an edge sees the previous value), 10.3 (continuous
// assignment y = x + 1). Posedges of clk at 5, 15, 25, 35; the producer sets valid at 5, so consumers log from 15.
`timescale 1ns/1ns
interface svifport2_if (input logic clk);
  logic [7:0] data;
  logic valid;
  modport mst (output data, output valid, input clk);
  modport slv (input data, input valid, input clk);
endinterface

module svifport2_prod(a);
  svifport2_if.mst a;
  always @(posedge a.clk) begin a.data <= a.data + 1; a.valid <= 1; end
  initial begin a.data = 0; a.valid = 0; end
endmodule

module svifport2_cons(x, b, y, z);
  input [3:0] x;
  svifport2_if.slv b;
  output [3:0] y;
  input z;
  string log[$];
  string scope;
  assign y = x + 1;
  always @(posedge b.clk) if (b.valid) begin
    scope = $sformatf("%m");
    log.push_back($sformatf("%0d %0d %0d %b", $time, b.data, y, z));
  end
endmodule

module svifport2_cons2(a, n);
  svifport2_if a;
  input n;
  string log[$];
  always @(posedge a.clk) if (a.valid) log.push_back($sformatf("%0d %0d %b", $time, a.data, n));
endmodule

module svifport2;
  logic clk = 0;
  always #5 clk = ~clk;
  svifport2_if i(clk);
  wire [3:0] w;
  svifport2_prod p(i);
  svifport2_cons c(4'd3, i, w, 1'b1);
  svifport2_cons c3(.z(1'b0), .y(), .b(i), .x(w));
  svifport2_cons2 c2(.n(1'b1), .a(i));
  int err = 0;
  task automatic chk(string what, string got[$], string exp[$]);
    if (got.size() != exp.size()) begin err++; $error("%s: %0d entries, expected %0d", what, got.size(), exp.size()); end
    else foreach (got[k]) if (got[k] != exp[k]) begin err++; $error("%s[%0d]: '%s' expected '%s'", what, k, got[k], exp[k]); end
  endtask
  initial begin
    #40;
    chk("c", c.log, '{"15 1 4 1", "25 2 4 1", "35 3 4 1"});
    chk("c3", c3.log, '{"15 1 5 0", "25 2 5 0", "35 3 5 0"});
    chk("c2", c2.log, '{"15 1 1", "25 2 1", "35 3 1"});
    if (c.scope != "svifport2.c" || c2.log.size() == 0) begin err++; $error("%%m in consumer: '%s'", c.scope); end
    if (err == 0) $display("PASSED");
    $finish;
  end
endmodule
