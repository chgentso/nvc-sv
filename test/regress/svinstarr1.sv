// Arrays of module and interface instances
// Expected values: IEEE 1800-2017 23.3.3.5 (port connections to arrays of instances: a connection
// as wide as the whole array is split, the leftmost instance of the array taking the leftmost bits;
// a connection as wide as the port is replicated to every instance), 25.3 / 25.9 (interface arrays,
// virtual interfaces), 9.4.1 (delay control)
`timescale 1ns/1ns

interface svinstarr1_if (input logic clk);
  logic [7:0] d;
  modport mp (input d, input clk);
endinterface

module svinstarr1_cons(svinstarr1_if.mp p, input [3:0] idx);
  always @(posedge p.clk) begin
    #(idx);
    // c[k] (idx 0..2) sees i[k]; cj[0..2] (idx 4..6) sees j[0..2]
    if ($time !== 64'd50 + idx) begin svinstarr1.errs++; $error("cons idx=%0d at %0t", idx, $time); end
    if (idx < 4) begin
      if (p.d !== (idx == 0 ? 8'd100 : idx == 1 ? 8'd77 : 8'd102)) begin
        svinstarr1.errs++; $error("c idx=%0d d=%0d", idx, p.d); end
    end else begin
      if (p.d !== 8'd16 + idx) begin svinstarr1.errs++; $error("cj idx=%0d d=%0d", idx, p.d); end
    end
    svinstarr1.cnt++;
  end
endmodule

module svinstarr1_leaf #(parameter K = 1) (input [3:0] a, output [3:0] y);
  assign y = a + K;
endmodule

class svinstarr1_drv;
  virtual svinstarr1_if v;
  task run(int x); v.d = x; endtask
endclass

module svinstarr1;
  parameter N = 3;
  int errs = 0, cnt = 0;
  logic clk = 0;
  always #50 clk = ~clk;
  svinstarr1_if i[N-1:0] (clk);
  svinstarr1_if j[0:2] (clk);
  svinstarr1_cons c[N-1:0] (i, {4'd2, 4'd1, 4'd0});
  svinstarr1_cons cj[0:2] (.p(j), .idx({4'd4, 4'd5, 4'd6}));
  wire [11:0] y12, y12b;
  wire [7:0] y12c;
  svinstarr1_leaf #(.K(2)) l[2:0] (.a(12'h321), .y(y12));
  svinstarr1_leaf lb[0:2] (.a(12'h321), .y(y12b));
  svinstarr1_leaf lc[1:0] (.a(4'h7), .y(y12c));
  svinstarr1_drv dd0 = new, dd2 = new;
  virtual svinstarr1_if vv;
  for (genvar g = 0; g < N; g++) begin : gg
    initial i[g].d = 10 + g;
  end
  initial begin
    j[0].d = 20; j[1].d = 21; j[2].d = 22;
    #1;
    if (y12 !== 12'h543) begin errs++; $error("y12=%h", y12); end
    if (y12b !== 12'h432) begin errs++; $error("y12b=%h", y12b); end
    if (y12c !== 8'h88) begin errs++; $error("y12c=%h", y12c); end
    vv = i[1];
    vv.d = 77;
    #1;
    if (i[0].d !== 10 || i[1].d !== 77 || i[2].d !== 12) begin errs++; $error("i d: %0d %0d %0d", i[0].d, i[1].d, i[2].d); end
    if (svinstarr1.j[2].d !== 22) begin errs++; $error("j[2].d=%0d", svinstarr1.j[2].d); end
    dd0.v = i[0]; dd2.v = i[2]; dd0.run(100); dd2.run(102);
    #1;
    if (i[0].d !== 100 || i[1].d !== 77 || i[2].d !== 102) begin errs++; $error("i d: %0d %0d %0d", i[0].d, i[1].d, i[2].d); end
  end
  initial begin
    #120;
    if (cnt !== 6) begin errs++; $error("cnt=%0d", cnt); end
    if (errs == 0) $display("PASSED");
    $finish;
  end
endmodule
