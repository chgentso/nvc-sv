// Comparing a class handle with null, and reading a property through a null
// handle.  Test is expected to terminate with a run-time error (the `fail`
// flag of the regression list): every check before the null access is made by
// an explicit comparison that ends the run normally (exit status 0, which the
// `fail` flag reports as a regression failure) if it is wrong.
// Expected values: IEEE 1800-2017 clause 8 (8.4: handles are initialised to null, null
// comparison with ==/!=), clause 8 (accessing a property through a null handle is
// an error).
class c;
  int x;
endclass

module svnull1;
  c a;
  initial begin
    if (a != null || !(a == null)) begin $display("FAILED: initial handle is not null"); $finish; end
    fork
      wait (a != null);
      #1 a = new;
    join
    if (a == null || null == a || !(a != null) || !(null != a)) begin
      $display("FAILED: comparison with null after new"); $finish;
    end
    a = null;
    if (a != null || null != a) begin $display("FAILED: null assignment"); $finish; end
    $display("%0d", a.x);   // run-time error: null handle
    $display("FAILED: access through null handle did not stop the simulation");
    $finish;
  end
endmodule
