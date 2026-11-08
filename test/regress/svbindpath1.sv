// bind by hierarchical instance path and by module:instance list (self-checking).
// Expected values: IEEE 1800-2017 23.11 (bind_target_instance / bind_target_scope : instance list; the bound
// instance is created only in the named target and sees its signals), 21.2.1.6 (%m), 9.4.2, 10.4.2 (NBA).
// Reference model: clk rises at 5 and 15; d (4 bits) is incremented by NBA at the falls at 10 and 20, so
// d = 0 at t=5, 1 at t=15.  In leaf, q <= d at each rise, so the monitor (active region) sees the previous
// value: q = x at 5 and q = 0 at 15 (m1.a), and the inverted data for m1.b (d = ~d_parent: 15, 14; q = x, 15).
// m2 gets d+1 (1, 2) and its bound monitor connects q to d.
`timescale 1ns/1ns
module leaf(input logic clk, input logic [3:0] d);
  logic [3:0] q;
  always @(posedge clk) q <= d;
endmodule

module mid(input logic clk, input logic [3:0] d);
  leaf a(clk, d);
  leaf b(clk, ~d);
endmodule

module mon(input logic clk, input logic [3:0] d, input logic [3:0] q);
  logic [3:0] dh [0:1];
  logic [3:0] qh [0:1];
  int n = 0;
  string path;
  initial path = $sformatf("%m");
  always @(posedge clk) begin
    if (n < 2) begin dh[n] = d; qh[n] = q; end
    n++;
  end
endmodule

module svbindpath1;
  logic clk = 0;
  logic [3:0] d = 0;
  int errs = 0;
  mid m1(clk, d);
  mid m2(clk, d + 1);
  always #5 clk = ~clk;
  always @(negedge clk) d <= d + 1;
  initial begin
    #22;
    if (m1.a.mon_a.n != 2 || m1.a.mon_a.dh[0] !== 4'd0 || m1.a.mon_a.dh[1] !== 4'd1 ||
        m1.a.mon_a.qh[0] !== 4'bxxxx || m1.a.mon_a.qh[1] !== 4'd0) begin errs++; $error("mon_a"); end
    if (m1.b.mon_b.n != 2 || m1.b.mon_b.dh[0] !== 4'd15 || m1.b.mon_b.dh[1] !== 4'd14 ||
        m1.b.mon_b.qh[0] !== 4'bxxxx || m1.b.mon_b.qh[1] !== 4'd15) begin errs++; $error("mon_b"); end
    if (m2.mon_m2.n != 2 || m2.mon_m2.dh[0] !== 4'd1 || m2.mon_m2.dh[1] !== 4'd2 ||
        m2.mon_m2.qh[0] !== 4'd1 || m2.mon_m2.qh[1] !== 4'd2) begin errs++; $error("mon_m2"); end
    if (m1.a.mon_a.path != "svbindpath1.m1.a.mon_a") begin errs++; $error("path a: %s", m1.a.mon_a.path); end
    if (m1.b.mon_b.path != "svbindpath1.m1.b.mon_b") begin errs++; $error("path b: %s", m1.b.mon_b.path); end
    if (m2.mon_m2.path != "svbindpath1.m2.mon_m2") begin errs++; $error("path m2: %s", m2.mon_m2.path); end
    if (errs == 0) $display("PASSED");
    $finish;
  end
endmodule

bind svbindpath1.m1.a mon mon_a (.clk(clk), .d(d), .q(q));
bind leaf : svbindpath1.m1.b mon mon_b (.clk(clk), .d(d), .q(q));
bind svbindpath1.m2 mon mon_m2 (.clk(clk), .d(d), .q(d));
