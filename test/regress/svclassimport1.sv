// Expected values: IEEE 1800-2017 26.3 (package import: wildcard and explicit imports are
// allowed in any scope that can hold declarations, including class methods, tasks and
// begin-end blocks), 6.19 (enum values RED = 0, GREEN = 1).
package pk;
  function automatic int twice(int x); return 2*x; endfunction
  typedef enum { RED, GREEN } col_t;
endpackage
class c;
  int t_result;
  function int f(int a);
    import pk::*;
    int out;
    col_t k = GREEN;
    out = twice(a) - a + 1 + int'(k);
    return out;
  endfunction
  task t();
    import pk::twice;
    t_result = twice(5);
  endtask
endclass
module svclassimport1;
  int errors = 0;
  initial begin
    automatic c o = new;
    // f(2) = 4 - 2 + 1 + 1 = 4
    if (o.f(2) !== 4) begin $error("f(2) = %0d, expected 4", o.f(2)); errors++; end
    o.t();
    if (o.t_result !== 10) begin $error("twice(5) = %0d, expected 10", o.t_result); errors++; end
    begin
      import pk::*;
      if (twice(4) !== 8) begin $error("twice(4) = %0d, expected 8", twice(4)); errors++; end
      if (int'(GREEN) !== 1) begin $error("GREEN = %0d, expected 1", int'(GREEN)); errors++; end
    end
    if (errors == 0) $display("PASSED");
  end
endmodule
