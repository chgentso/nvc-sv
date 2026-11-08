`timescale 1ns/1ns
// User-defined nettypes of unpacked struct type, with and without a resolution function, several continuous
// drivers per net, re-resolution when a driver changes
// Expected values: IEEE 1800-2017 6.6.7 (a nettype may have an unpacked struct data type whose members are integral
// or real; the value of the net is the return value of the resolution function called with a dynamic array
// holding the value of every driver; it is called again whenever a driver changes; a nettype without a resolution
// function allows a single driver), 7.2 (structures), 10.9.2 (structure assignment patterns), 10.3 (continuous
// assignments are drivers).
// vgnet models Thevenin sources: g = sum of the conductances, v = sum(v*g)/g; the operands are chosen so that every
// result is exact in binary floating point.  cnet ORs the values and counts the drivers.
module svnettype3;
  typedef struct { real v; real g; } vg_t;
  typedef struct { logic [7:0] val; int n; } cnt_t;

  function automatic vg_t res_vg (input vg_t d[]);
    real gs = 0.0, is = 0.0;
    foreach (d[i]) begin
      gs += d[i].g;
      is += d[i].v * d[i].g;
    end
    res_vg.g = gs;
    res_vg.v = (gs == 0.0) ? 0.0 : is / gs;
  endfunction

  function automatic cnt_t res_cnt (input cnt_t d[]);
    res_cnt.val = 8'h00;
    res_cnt.n = d.size();
    foreach (d[i]) res_cnt.val |= d[i].val;
  endfunction

  nettype vg_t vgnet with res_vg;
  nettype cnt_t cnet with res_cnt;
  nettype vg_t single_vg;

  vgnet n;
  cnet c;
  single_vg s;

  real v1 = 2.0, v2 = 8.0;
  logic [7:0] a = 8'h01, b = 8'h10, e = 8'h80;
  int fails = 0;

  assign n = '{v1, 1.0};
  assign n = '{v2, 3.0};
  assign c = '{a, 1};
  assign c = '{b, 1};
  assign c = '{val: e, n: 1};
  assign s = '{v: v1 * 2.0, g: 0.5};

  task automatic check(string what, bit ok);
    if (!ok) begin
      $error("%s: n = '{%f, %f} c = '{%h, %0d} s = '{%f, %f}", what, n.v, n.g, c.val, c.n, s.v, s.g);
      fails++;
    end
  endtask

  initial begin
    #1;
    // g = 1 + 3, v = (2*1 + 8*3) / 4
    check("t=1", n.g == 4.0 && n.v == 6.5 && c.val === 8'h91 && c.n == 3 && s.v == 4.0 && s.g == 0.5);
    v1 = 10.0; a = 8'h02; #1;
    // v = (10 + 24) / 4
    check("t=2", n.g == 4.0 && n.v == 8.5 && c.val === 8'h92 && c.n == 3 && s.v == 20.0);
    v2 = -2.0; b = 8'h00; e = 8'h00; #1;
    // v = (10 - 6) / 4
    check("t=3", n.v == 1.0 && c.val === 8'h02 && c.n == 3);
    if (fails == 0) $display("PASSED");
  end
endmodule
