// In the constraint block of randomize() a function name is looked up in the class of the object first
// and local:: names the scope of the caller (IEEE 1800-2017 18.7.1), also for a function call
class A;
  rand int x;
  function int get; return 100; endfunction
endclass
class B;
  int base = 7;
  A a;
  function int get; return base; endfunction
  function int op(int v); return v + 1; endfunction
  function int test;
    int fails = 0;
    a = new;
    if (!a.randomize() with { x == get(); }) fails++;
    if (a.x != 100) fails++;
    if (!a.randomize() with { x == local::get(); }) fails++;
    if (a.x != 7) fails++;
    if (!a.randomize() with { x == local::op(local::base) + get(); }) fails++;
    if (a.x != 108) fails++;
    return fails;
  endfunction
endclass
module svcrvlocalcall1;
  B b;
  initial begin
    b = new;
    if (b.test() == 0) $display("PASSED"); else $error("failed");
  end
endmodule
