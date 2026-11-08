module svlrm_c6e;
  // Self-checking test. Expected values: IEEE 1800-2017 6.5-6.7 (variables and nets, net types:
  // wire, tri, wand, wor, tri0, tri1, supply0, supply1, trireg), 6.6.1 (wire/tri), 6.6.2 (wand/wor),
  // 6.6.4 (trireg: an undriven trireg holds x), 6.6.3 (supply nets), 6.7 (net declaration assignment),
  // 7.3.1 and 7.2.1 (packed union and packed struct layout), 6.9 / 11.4.14 (part-select).
  typedef struct packed { logic [3:0] hi; logic [3:0] lo; } byte_s;
  typedef union packed { byte_s s; logic [7:0] b; } bu_t;
  wire [3:0] w1 = 4'ha;
  wire w2;
  assign w2 = 1'b1;
  tri [1:0] t1;
  assign t1 = 2'bz1;
  wand wa; assign wa = 1'b1; assign wa = 1'b0;
  wor wo; assign wo = 1'b1; assign wo = 1'b0;
  tri0 t0; tri1 t_1;
  supply0 gnd; supply1 vdd;
  trireg tr;
  wire (strong0, weak1) sw = 1'b1;
  wire [7:0] vec = 8'h5a;
  bu_t u;
  int errs;
  function automatic void ck(bit ok, string m);
    if (!ok) begin errs++; $error("%s", m); end
  endfunction
  initial begin
    #1;
    ck(w1 === 4'b1010, "w1");
    ck(w2 === 1'b1, "w2");
    ck(t1 === 2'bz1, "t1 keeps z on the undriven bit");
    ck(wa === 1'b0, "wand of 1 and 0");
    ck(wo === 1'b1, "wor of 1 and 0");
    ck(t0 === 1'b0, "tri0 undriven is 0");
    ck(t_1 === 1'b1, "tri1 undriven is 1");
    ck(gnd === 1'b0 && vdd === 1'b1, "supply nets");
    ck(sw === 1'b1, "net with drive strength");
    ck(vec[3:0] === 4'ha, "part-select of net");
    u.b = 8'hc3; ck(u.s.hi === 4'hc && u.s.lo === 4'h3, "packed union read through struct");
    u.s.hi = 4'h1; ck(u.b === 8'h13, "packed union written through struct");
    ck($bits(u) == 8, "$bits of packed union");
    ck(tr === 1'bx, "undriven trireg holds x (6.6.4)");
    if (errs == 0) $display("PASSED");
  end
endmodule
