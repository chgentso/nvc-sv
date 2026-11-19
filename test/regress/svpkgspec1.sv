// A parameterised class declared in a package used as a scope prefix:
// pkg::cls#(params)::member (IEEE 1800-2017 8.25, 26.3).
package p;
  class m #(int W = 4);
    typedef logic [W-1:0] t;
    static function int get(); return W; endfunction
  endclass
endpackage

module svpkgspec1;
  int x, y;
  p::m#(7)::t v;
  initial begin
    x = p::m#()::get();
    y = p::m#(9)::get();
    v = '1;
    $display("%0d %0d %0d", x, y, $bits(v));
  end
endmodule
