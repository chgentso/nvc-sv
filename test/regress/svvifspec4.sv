// A virtual interface passed through an output argument of a static method of a parameterized class.
// Expected values: IEEE 1800-2017 25.9, 8.25 (static members of a specialization), 13.5 (output
// arguments), 8.5 (class property default null).
package pk;
  class store #(type T = int);
    static T val;
    static bit set;
    static function void put(T v); val = v; set = 1; endfunction
    static function bit get(output T v); v = val; return set; endfunction
  endclass
  class user;
    virtual bfm h;
    function bit fetch();
      return store#(virtual bfm)::get(h);
    endfunction
  endclass
endpackage

interface bfm (input bit clk);
  int x;
endinterface

module svvifspec4;
  import pk::*;
  bit clk;
  bfm b(clk);
  int errors = 0;
  initial begin
    automatic user u = new;
    if (u.fetch() !== 1'b0) begin $error("before: fetch returned 1"); errors++; end
    store#(virtual bfm)::put(b);
    if (u.fetch() !== 1'b1) begin $error("after: fetch returned 0"); errors++; end
    if (u.h == null) begin $error("handle not copied"); errors++; end
    u.h.x = 5;
    if (b.x !== 5) begin $error("x=%0d", b.x); errors++; end
    if (errors == 0) $display("PASSED");
  end
endmodule
