class Box;
  typedef int map_t[string];
  map_t m;
  function new(map_t init);
    m = init;
  endfunction
endclass

class Pair;
  typedef struct packed {
    logic [7:0] a;
    logic [7:0] b;
  } p_t;
  p_t val;
  function new(p_t v);
    val = v;
  endfunction
endclass

module svctorpat1;
  initial begin
    automatic Box b = new('{"k": 42});
    automatic Pair p = new('{a: 8'h12, b: 8'h34});
    if (b.m["k"] !== 42) $fatal(1, "b.m=%0d", b.m["k"]);
    if (p.val.a !== 8'h12 || p.val.b !== 8'h34) $fatal(1, "p=%h", p.val);
    $display("PASSED");
    $finish;
  end
endmodule
