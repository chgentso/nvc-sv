// Test class parameter specialization keys with casts and typed value truncation

class CByte #(parameter byte P = byte'(8));
  function byte get();
    return P;
  endfunction
endclass

class CSigned #(parameter int P = int'(-5));
  function int get();
    return P;
  endfunction
endclass

class CWidth #(parameter int N = 1, parameter logic [N-1:0] P = '0);
  static int shared;
  static function int val();
    return int'(P);
  endfunction
endclass

class CType #(parameter type T = byte, parameter T P = 0, parameter T Q = 0);
  static int shared;
  static function int val();
    return int'(Q);
  endfunction
endclass

module svclassparam3;
  localparam int TWO = 2;
  logic [3:0] tv;

  CByte #() b1;
  CByte #(8) b2;

  CSigned #() s1;
  CSigned #(-5) s2;

  typedef CWidth #(.P(8'h81), .N(TWO * 2)) cw_a_t;
  typedef CWidth #(.P(8'h01), .N(TWO * 2)) cw_b_t;
  cw_a_t cwa;
  cw_b_t cwb;

  typedef CType #(.P(8'h01), .T(type(tv)), .Q(8'h81)) ct_a_t;
  typedef CType #(.P(8'h01), .T(type(tv)), .Q(8'h01)) ct_b_t;
  ct_a_t cta;
  ct_b_t ctb;

  initial begin
    b1 = new;
    b2 = b1;
    if (b2.get() !== 8) $fatal(1, "byte mismatch");

    s1 = new;
    s2 = s1;
    if (s2.get() !== -5) $fatal(1, "signed mismatch");

    cwa = new;
    cwb = cwa;
    cw_a_t::shared = 7;
    if (cw_b_t::shared !== 7 || cw_b_t::val() !== 1)
      $fatal(1, "width trunc mismatch");

    cta = new;
    ctb = cta;
    ct_a_t::shared = 9;
    if (ct_b_t::shared !== 9 || ct_b_t::val() !== 1)
      $fatal(1, "type trunc mismatch");

    $display("PASSED");
    $finish;
  end
endmodule
