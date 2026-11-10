// Self-checking. Expected values: IEEE 1800-2017 9.2.2.2.1 (always_comb is
// sensitive to the expressions read in functions it calls: a function may
// create an object, 8.7, and a dynamic array, 7.5.1), 13.4 (function value).
class box_c;
  int v;
  function new(int x); v = x; endfunction
endclass

function automatic int make(int x);
  box_c b;
  int a[];
  b = new(x);
  a = new[3];
  a[1] = b.v;
  return a[1] + 1;
endfunction

module svcombnew1;
  int q = 3, r;
  always_comb r = make(q);
  initial begin
    #1 if (r != 4) $error("r = %0d expected 4", r);
    q = 9;
    #1 if (r != 10) $error("r = %0d expected 10", r);
    else $display("PASSED");
  end
endmodule
