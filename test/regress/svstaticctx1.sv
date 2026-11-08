// Expected values: IEEE 1800-2017 8.9 (static class members shared by all objects, static methods),
// 8.10 (static methods access only static members), 6.21 (module-scope variable visible in class method),
// 7.10.2 (push_back), 7.10.1 (queue size)
module svstaticctx1;
  bit [3:0] v;
  class K;
    static K all[$];
    static int n2 = 3;
    int n = 7;
    function new(); all.push_back(this); endfunction
    function void s(); n = v; endfunction
    static function int count(); return all.size() + n2; endfunction
    static function int first(); return all[0].n; endfunction
  endclass
  K k = new;
  int errs = 0;
  initial begin
    v = 9;
    k.s();
    // one object constructed: size 1 + n2 3 = 4; all[0] is k whose n became 9
    if (K::count() != 4) begin $error("count=%0d expected 4", K::count()); errs++; end
    if (K::first() != 9) begin $error("first=%0d expected 9", K::first()); errs++; end
    if (errs == 0) $display("PASSED");
  end
endmodule
