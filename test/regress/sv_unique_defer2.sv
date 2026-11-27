// Deferred unique/priority violation reports (IEEE 1800-2017 12.4.2.1)
// from a function called by an always_comb block, from an always_ff
// block, and from an always_comb block that runs again in the same time
// step after another block has queued a report

`timescale 1ns/1ns

module sv_unique_defer2_sub (
  input  logic       clk,
  input  logic [1:0] s,
  input  logic [1:0] t,
  output logic [1:0] y,
  output logic [1:0] z,
  output logic [1:0] q
);
  function automatic logic [1:0] pick(logic [1:0] v);
    logic [1:0] r;
    r = 2'd0;
    priority case (1'b1)
      v[0]: r = 2'd1;
      v[1]: r = 2'd2;
    endcase
    return r;
  endfunction

  always_comb y = pick(s);

  always_comb begin
    z = 2'd0;
    unique if (t[0])
      z = 2'd1;
    else if (t[1])
      z = 2'd2;
  end

  always_ff @(posedge clk) begin
    unique case (s)
      2'd1: q <= 2'd1;
      2'd2: q <= 2'd2;
    endcase
  end
endmodule

module sv_unique_defer2;
  logic clk = 1'b0;
  logic [1:0] s = 2'd1, t = 2'd1;
  logic [1:0] y, z, q;

  sv_unique_defer2_sub u (.clk(clk), .s(s), .t(t), .y(y), .z(z), .q(q));

  initial begin
    #10;
    s = 2'd0;          // pick: no match, stays: reported at 10ns
    t = 2'd3;          // two match, then fixed in the same time step
    #0 t = 2'd2;
    #10;
    $display("y=%0d z=%0d", y, z);
    s = 2'd0;          // pick: no match, then fixed in the same time step
    #0 s = 2'd3;       // no case item of the always_ff matches at the edge
    clk = 1'b1;
    #10;
    $display("q=%0d", q);
    s = 2'd2;
    t = 2'd0;          // z: no match in a later time step
    #10;
    $display("y=%0d z=%0d", y, z);
    $finish;
  end
endmodule
