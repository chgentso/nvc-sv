module vlog_class_inner1;
  class Base0;
    class InnerOnly;
      int v;
      function new();
        v = 11;
      endfunction
    endclass

    class InnerOver;
      int v;
      function new();
        v = 12;
      endfunction
    endclass

    InnerOnly o = new;
    InnerOver ov = new;
  endclass

  class Ext extends Base0;
    class InnerOver;
      int v;
      function new();
        v = 22;
      endfunction
    endclass

    InnerOnly o2 = new;
    InnerOver ov2 = new;
  endclass

  initial begin
    Ext e = new;
    if (e.o.v != 11) $fatal(1, "base inner mismatch: %0d", e.o.v);
    if (e.o2.v != 11) $fatal(1, "inherited inner mismatch: %0d", e.o2.v);
    if (e.ov.v != 12) $fatal(1, "base shadowed inner mismatch: %0d", e.ov.v);
    if (e.ov2.v != 22) $fatal(1, "derived shadowing inner mismatch: %0d", e.ov2.v);
    $display("PASSED");
    $finish;
  end
endmodule
