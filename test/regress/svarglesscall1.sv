// Self-checking. Expected values: IEEE 1800-2017 13.3/13.4 (a function call with no arguments may omit the
// parentheses), 8.10 (static methods), 8.23 (class scope resolution ::), 26.3 (package scope), 13.4.1.
`define CHK(tag, got, exp) if ((got) !== (exp)) begin errs++; $error("%s: got %0d expected %0d", tag, got, exp); end
class c;
  static function int get_type(); return 42; endfunction
  static function int plain; return 7; endfunction
  function int self; int self_local = 3; return self_local; endfunction
endclass
package p;
  function int pf; return 5; endfunction
endpackage
module svarglesscall1;
  int errs = 0;
  function int pf2; pf2 = 11; endfunction
  function int add2(int a, int b); return a + b; endfunction
  int x;
  initial begin
    c o;
    o = new();
    x = c::get_type;
    `CHK("c::get_type", x, 42)
    `CHK("add2(c::get_type, c::plain)", add2(c::get_type, c::plain), 49)
    `CHK("add2(p::pf, 1)", add2(p::pf, 1), 6)
    `CHK("pf2", pf2, 11)
    `CHK("o.self()", o.self(), 3)
    if (errs == 0) $display("PASSED");
    $finish;
  end
endmodule
