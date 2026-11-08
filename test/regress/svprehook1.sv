// pre_randomize/post_randomize hooks; super.pre/post_randomize() reach the built-in empty methods.
// Expected values: IEEE 1800-2017 18.6.1 (pre_randomize / post_randomize), 18.6.3 (randomize returns 1)
string log = "";
class A;
  rand int x;
  function void pre_randomize(); log = {log, "A pre;"}; endfunction
endclass
class B;
  rand int x;
endclass
class C extends B;
  function void pre_randomize();
    super.pre_randomize();
    log = {log, "C pre;"};
  endfunction
  function void post_randomize();
    super.post_randomize();
    log = {log, "C post;"};
  endfunction
endclass
module svprehook1;
  C c;
  int r;
  initial begin
    c = new;
    r = c.randomize();
    if (r !== 1) $error("randomize returned %0d", r);
    else if (log != "C pre;C post;") $error("hook log '%s'", log);
    else $display("PASSED");
  end
endmodule
