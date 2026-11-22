package cp;
  typedef struct packed { logic v; logic [3:0] t; } cap_t;
  // Reads the return value through the name of the function
  function automatic logic eq(cap_t a, cap_t b, logic [31:0] x, logic [31:0] y);
    cap_t ua;
    ua = a;
    eq = (a.v == b.v) && (a.t == b.t) && (x == y);
    return eq;
  endfunction
endpackage

module m(input cp::cap_t a, b, input logic [31:0] x, y, input logic [1:0] sel,
         output logic [31:0] r);
  import cp::*;
  always_comb begin
    r = 0;
    case (sel)
      2'd1: r = 32'(eq(a, b, x, y));
      default: ;
    endcase
  end
endmodule

module vlog_comb_func_retval;
  cp::cap_t a = '{1'b1, 4'd3}, b = a;
  logic [31:0] x = 5, y = 5, r;
  m u(a, b, x, y, 2'd1, r);
  initial begin
    #1;
    if (r !== 1) begin $display("FAILED %0d", r); $finish; end
    y = 6;
    #1;
    if (r === 0) $display("PASSED");
    else $display("FAILED %0d", r);
  end
endmodule
