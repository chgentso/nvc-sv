interface ifc;
  logic [7:0] a, b;
  logic x;
  function int f(); return 1; endfunction
  function int g(); return 2; endfunction
  modport mi (input a, output b, import f);
  modport bad (output .e(a & b));
endinterface

module cons(ifc.mi p);
  initial begin
    p.a = 1;
    $display("%d", p.x);
    $display("%d", p.g());
  end
endmodule

module cons2;
  virtual ifc.mi v;
  initial begin
    v.a = 1;
    v.x = 1;
  end
endmodule
