// A localparam of a module computed from a parameter of an interface port
// is read by a hierarchical name from the parent, whatever order the
// modules are elaborated in
package svhierlp1_pkg;
  function automatic int fn(int v);
    return v + 1;
  endfunction
endpackage

interface svhierlp1_if0 #(parameter int W = 2);
  localparam int D = $clog2(W);
  localparam int C = D * 2;
endinterface

module svhierlp1_m0 (svhierlp1_if0 p);
  localparam int lp = svhierlp1_pkg::fn(p.C);
  localparam int lq = p.C + 1;
endmodule

interface svhierlp1_if1 #(parameter int W = 3);
  localparam int D = $clog2(W);
  localparam int C = D * 2;
endinterface

module svhierlp1_m1 (svhierlp1_if1 p);
  localparam int lp = svhierlp1_pkg::fn(p.C);
  localparam int lq = p.C + 1;
endmodule

interface svhierlp1_if2 #(parameter int W = 4);
  localparam int D = $clog2(W);
  localparam int C = D * 2;
endinterface

module svhierlp1_m2 (svhierlp1_if2 p);
  localparam int lp = svhierlp1_pkg::fn(p.C);
  localparam int lq = p.C + 1;
endmodule

interface svhierlp1_if3 #(parameter int W = 5);
  localparam int D = $clog2(W);
  localparam int C = D * 2;
endinterface

module svhierlp1_m3 (svhierlp1_if3 p);
  localparam int lp = svhierlp1_pkg::fn(p.C);
  localparam int lq = p.C + 1;
endmodule

interface svhierlp1_if4 #(parameter int W = 6);
  localparam int D = $clog2(W);
  localparam int C = D * 2;
endinterface

module svhierlp1_m4 (svhierlp1_if4 p);
  localparam int lp = svhierlp1_pkg::fn(p.C);
  localparam int lq = p.C + 1;
endmodule

interface svhierlp1_if5 #(parameter int W = 7);
  localparam int D = $clog2(W);
  localparam int C = D * 2;
endinterface

module svhierlp1_m5 (svhierlp1_if5 p);
  localparam int lp = svhierlp1_pkg::fn(p.C);
  localparam int lq = p.C + 1;
endmodule

module svhierlp1;
  svhierlp1_if0 #(.W(16)) i0 ();
  svhierlp1_m0 m0 (.p(i0));
  svhierlp1_if1 #(.W(32)) i1 ();
  svhierlp1_m1 m1 (.p(i1));
  svhierlp1_if2 #(.W(48)) i2 ();
  svhierlp1_m2 m2 (.p(i2));
  svhierlp1_if3 #(.W(64)) i3 ();
  svhierlp1_m3 m3 (.p(i3));
  svhierlp1_if4 #(.W(80)) i4 ();
  svhierlp1_m4 m4 (.p(i4));
  svhierlp1_if5 #(.W(96)) i5 ();
  svhierlp1_m5 m5 (.p(i5));
  initial begin
    if (m0.lp != 9 || m0.lq != 9) $fatal(1, "m0 %0d %0d", m0.lp, m0.lq);
    if (m1.lp != 11 || m1.lq != 11) $fatal(1, "m1 %0d %0d", m1.lp, m1.lq);
    if (m2.lp != 13 || m2.lq != 13) $fatal(1, "m2 %0d %0d", m2.lp, m2.lq);
    if (m3.lp != 13 || m3.lq != 13) $fatal(1, "m3 %0d %0d", m3.lp, m3.lq);
    if (m4.lp != 15 || m4.lq != 15) $fatal(1, "m4 %0d %0d", m4.lp, m4.lq);
    if (m5.lp != 15 || m5.lq != 15) $fatal(1, "m5 %0d %0d", m5.lp, m5.lq);
    $display("PASSED");
  end
endmodule
