typedef logic [15:0] inner_t;

typedef class Inner;

class Outer #(parameter P = 13);
  function int get_p();
    return c1.get_p();
  endfunction
  function new();
    c1 = new;
  endfunction
  Inner #(PMINUS1 + 1) c1;
  localparam PMINUS1 = P - 1;
endclass

class Inner #(parameter PBASE = 12);
  static function int get_p();
    return PBASE;
  endfunction
endclass

class Shadow #(parameter int A = 8);
  typedef logic [A-1:0] inner_t;
  localparam int width = $bits(inner_t);
endclass

module sv_class_fwd_lparam1;
  typedef Shadow#(8) s8_t;
  initial begin
    Outer #(20) o;
    o = new;
    if (o.get_p() !== 20) $fatal(1, "expected 20, got %0d", o.get_p());
    if (s8_t::width !== 8) $fatal(1, "expected width 8, got %0d", s8_t::width);
    $display("PASSED");
  end
endmodule
