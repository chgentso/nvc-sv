// Expected values: IEEE 1800-2017 8.25 (parameterized classes), 8.26 / 8.15 (a derived class
// sees the members, including parameters and local parameters, of its base class
// specialization), 6.20 (parameters).
package pk;
  class base #(type T = int, int N = 3);
    parameter int Threshold = 80;
    localparam int Local = 7;
    T val;
    function int lim(); return Threshold * 2 + N; endfunction
  endclass
endpackage
class derived extends pk::base #(.T(bit [3:0]), .N(5));
  function int get();
    return Threshold + Local + lim() + N;
  endfunction
endclass
class derived2 extends derived;
  function int get2();
    return Threshold * 2 + Local;
  endfunction
endclass
class plain;
  parameter int P = 5;
endclass
class sub extends plain;
  function int g(); return P * 3; endfunction
endclass
module svclassbaseparam1;
  int errors = 0;
  initial begin
    automatic derived d = new;
    automatic derived2 d2 = new;
    automatic sub s = new;
    // get(): 80 + 7 + (80*2 + 5) + 5 = 257
    if (d.get() !== 257) begin $error("d.get() = %0d, expected 257", d.get()); errors++; end
    if (s.g() !== 15) begin $error("s.g() = %0d, expected 15", s.g()); errors++; end
    // get2(): 80*2 + 7 = 167
    if (d2.get2() !== 167) begin $error("d2.get2() = %0d, expected 167", d2.get2()); errors++; end
    // T = bit [3:0]: the member val keeps only 4 bits
    d.val = 5'h1f;
    if (d.val !== 4'hf) begin $error("d.val = %h, expected f", d.val); errors++; end
    if (errors == 0) $display("PASSED");
  end
endmodule
