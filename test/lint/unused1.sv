module sub(input a, output y);
  assign y = a;
endmodule

module unused1;
  reg clk = 1'b0;
  reg d = 1'b0;
  wire q;
  always @(q) $display("%b", q);
  reg r_unused;                // never read
  reg r_undriven;              // read, never assigned
  reg r_ok;
  reg r_init = 1'b1;           // initialised, so driven
  wire w_unused;
  wire w_undriven;
  wire w_ok;
  wire w_gate;
  wire w_sub;                  // driven by an instance, then read
  wire w_off;                  // nvc lint_off unused
  reg [3:0] r_arg;
  integer i;
  assign q = r_ok & r_undriven & r_init & w_undriven & w_gate & w_sub;
  assign w_ok = d;
  assign w_unused = w_ok;
  and g1(w_gate, d, w_ok);
  sub u1(.a(d), .y(w_sub));
  always @(posedge clk) r_ok <= w_ok;
  always @(posedge clk) r_unused <= d;
  assign w_off = d;
  // Written by a task and read
  task get(output [3:0] o); o = 4'd1; endtask
  initial begin
    get(r_arg);
    for (i = 0; i < 2; i = i + 1) $display("%d", r_arg[0]);
  end
endmodule
