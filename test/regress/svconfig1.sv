// IEEE 1800-2017 33.4: a configuration names the top-level cell with a
// design statement and selects the implementation of instances: a default
// liblist (33.4.1.3), an instance clause with a use clause binding a
// different cell (33.4.1.4, 33.4.1.6), a cell clause rebinding every
// instance of a cell (33.4.1.5), and a use clause overriding a parameter of
// one instance (33.4.3).  The configuration is elaborated as the top.
module svconfig1_leaf #(parameter int W = 4) (output int id, output int w);
  assign id = 1;
  assign w = W;
endmodule

module svconfig1_alt #(parameter int W = 4) (output int id, output int w);
  assign id = 2;
  assign w = W * 10;
endmodule

module svconfig1_other #(parameter int W = 4) (output int id, output int w);
  assign id = 3;
  assign w = W;
endmodule

module svconfig1_top;
  int id1, id2, id3, id4, id5;
  int w1, w2, w3, w4, w5;

  svconfig1_leaf u1(id1, w1);           // Unchanged
  svconfig1_leaf u2(id2, w2);           // Instance clause: alt
  svconfig1_leaf u3(id3, w3);           // Instance clause: W = 16
  svconfig1_other u4(id4, w4);          // Cell clause: alt
  svconfig1_leaf #(.W(5)) u5(id5, w5);  // Instance clause: alt, W from here

  initial begin
    #1;
    if (id1 != 1 || w1 != 4) $fatal(1, "u1: %0d %0d", id1, w1);
    if (id2 != 2 || w2 != 40) $fatal(1, "u2: %0d %0d", id2, w2);
    if (id3 != 1 || w3 != 16) $fatal(1, "u3: %0d %0d", id3, w3);
    if (id4 != 2 || w4 != 40) $fatal(1, "u4: %0d %0d", id4, w4);
    if (id5 != 2 || w5 != 50) $fatal(1, "u5: %0d %0d", id5, w5);
    $display("PASSED");
  end
endmodule

config svconfig1;
  localparam WIDE = 16;
  design work.svconfig1_top;
  default liblist work;
  instance svconfig1_top.u2 use svconfig1_alt;
  instance svconfig1_top.u3 use #(.W(WIDE));
  instance svconfig1_top.u5 use work.svconfig1_alt;
  cell svconfig1_other use svconfig1_alt;
endconfig : svconfig1
