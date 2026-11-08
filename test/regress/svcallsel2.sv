// IEEE 1800-2017 A.8.4: primary has no select on a function call, so
// f()[i] and mk().x are extensions which nvc accepts with a warning
// rather than silently; a method call on a returned handle is legal.
module svcallsel2;
  typedef struct { int x; } s_t;
  class C;
    int v = 3;
    function int m(); return v; endfunction
  endclass
  function automatic s_t mk(int a); s_t s; s.x = a; return s; endfunction
  function automatic logic [7:0] f(); return 8'h5a; endfunction
  function automatic C gc(); C c = new; return c; endfunction
  initial begin
    if (f()[3] === 1'b1 && mk(3).x == 3 && gc().m() == 3)
      $display("PASSED");
    else
      $display("FAILED");
  end
endmodule
