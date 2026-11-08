module leaf #(parameter ID = 0) (input logic [3:0] i, output logic [3:0] o);
  assign o = i + ID;
  logic [3:0] inner = 4'h5;
  function int who(); return ID; endfunction
endmodule
module mid #(parameter BASE = 1) (input logic [3:0] i, output logic [3:0] o);
  logic [3:0] t;
  leaf #(BASE) a (.i(i), .o(t));
  leaf #(BASE + 1) b (.i(t), .o(o));
endmodule
module svlrm_c23a;
  // Self-checking test. Expected values: IEEE 1800-2017 23.2 (module instances, ports), 23.2.3 (port
  // connection by name and by position), 6.20.2 / 23.10 (parameter override by position, default
  // value), 23.6 (hierarchical names: instance variables, nets, function calls, parameters), 23.9
  // ($root and upwards name resolution through the top module name), 10.6.2 (force/release),
  // 21.2.1 (%m prints the hierarchical name of the scope), 20.6.2 ($bits).
  //   m1 (BASE=2): a (ID=2): t = in + 2 = 3; b (ID=3): o1 = t + 3 = 6
  //   m2 (BASE=1): a (ID=1): t = 2;           b (ID=2): o2 = 4
  //   force m1.t = f: o1 = f + 3 = 2 (4-bit); after release t returns to the driver value 3: o1 = 6
  logic [3:0] in = 4'h1;
  wire [3:0] o1, o2;
  mid #(2) m1 (in, o1);
  mid m2 (.i(in), .o(o2));
  int errs;
  function automatic void ck(bit ok, string m);
    if (!ok) begin errs++; $error("%s", m); end
  endfunction
  initial begin
    #1;
    ck(o1 === 4'h6 && o2 === 4'h4, "outputs of the two instances");
    ck(m1.t === 4'h3 && m2.t === 4'h2, "internal nets by hierarchical name");
    ck(m1.a.who() == 2 && m1.b.who() == 3, "hierarchical function calls");
    ck(m2.b.who() == 2, "default BASE");
    ck(m1.a.inner === 4'h5, "hierarchical variable read");
    m1.a.inner = 4'h9; ck(m1.a.inner === 4'h9, "hierarchical variable write");
    ck($sformatf("%m") == "svlrm_c23a", "%m in the top module");
    ck($bits(m1.t) == 4, "$bits of hierarchical object");
    force m1.t = 4'hf; #1 ck(o1 === 4'h2, "force on m1.t");
    release m1.t; #1 ck(o1 === 4'h6, "release restores the driven value");
    ck(svlrm_c23a.m1.a.ID == 2, "top-qualified parameter");
    ck(m1.BASE == 2, "instance parameter");
    ck($root.svlrm_c23a.in === 4'h1, "$root path");
    if (errs == 0) $display("PASSED");
  end
endmodule
