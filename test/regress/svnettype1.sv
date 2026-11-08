// User-defined nettypes with and without a resolution function, declared in a package and in a module
// Expected values: IEEE 1800-2017 6.6.7 (nettype declarations; the resolution function takes a dynamic array of
// the driver values), 13.4 (functions), 7.12.3 (foreach); the sum of 1.5 and 2.0 is exactly 3.5
package nt_pkg;
  function automatic real pkg_sum (input real driver[]);
    pkg_sum = 0.0;
    foreach (driver[i])
      pkg_sum += driver[i];
  endfunction
  nettype real pkg_net with pkg_sum;
endpackage

module svnettype1;
  function automatic real real_sum (input real driver[]);
    real_sum = 0.0;
    foreach (driver[i])
      real_sum += driver[i];
  endfunction

  nettype real real_net with real_sum;
  nettype real plain_net;

  initial begin
    real r;
    automatic real d[] = '{1.5, 2.0};
    automatic int fails = 0;
    r = real_sum(d);
    if (r != 3.5) begin $error("real_sum = %f", r); fails++; end
    r = nt_pkg::pkg_sum(d);
    if (r != 3.5) begin $error("pkg_sum = %f", r); fails++; end
    r = real_sum('{});
    if (r != 0.0) begin $error("empty sum = %f", r); fails++; end
    if (fails == 0) $display("PASSED");
  end
endmodule
