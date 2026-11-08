// A nettype whose resolution function is declared in a package, and a call of
// a package function with an assignment pattern argument
// Expected values: IEEE 1800-2017 6.6.7 (the net takes the value returned by the resolution function called with
// the value of every driver), 10.9.1 (an assignment pattern takes the type of the formal argument), 26.3 (calls
// of package functions); drivers 1.0 + 2.0 + 3.0 + 0.5 sum to 6.5
package svnettype8_pkg;
  function automatic real res_sum (input real d[]);
    res_sum = 0.0;
    foreach (d[i]) res_sum += d[i];
  endfunction
  function automatic int count (input int d[]);
    return d.size();
  endfunction
  nettype real rsum with res_sum;
  nettype rsum rsum_alias;
endpackage

module svnettype8;
  import svnettype8_pkg::*;
  real k = 0.5;
  rsum_alias n = k;
  int a = 4, c;
  assign n = 1.0;
  assign n = 2.0;
  assign n = 3.0;
  initial begin
    c = svnettype8_pkg::count('{a, 2, 3});
    #1;
    if (n != 6.5 || c != 3) $error("n = %f c = %0d", n, c);
    else $display("PASSED");
  end
endmodule
