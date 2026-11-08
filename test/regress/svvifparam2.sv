// Struct-valued and defaulted parameters read through virtual interface handles.
// Expected values: IEEE 1800-2017 25.9 (virtual interface parameters), 6.20.2/6.20.4 (value
// parameters, localparam), 7.2 (struct members).
typedef struct { int n; int w; } svvifparam2_t;
interface svvifparam2_if #(parameter svvifparam2_t P = '{3, 8}, parameter int Q = 5, localparam int L = Q * 2);
  logic [7:0] d;
endinterface

class svvifparam2_mon;
  virtual svvifparam2_if #(.Q(7)) vi;
  int errors;
  function new(virtual svvifparam2_if #(.Q(7)) i);
    vi = i;
  endfunction
  function void show();
    if (vi.P.n != 3 || vi.P.w != 8 || vi.Q != 7 || vi.L != 14) begin
      $error("mon %0d %0d %0d %0d", vi.P.n, vi.P.w, vi.Q, vi.L); errors++;
    end
  endfunction
endclass

module svvifparam2;
  svvifparam2_if #(.Q(7)) x();
  svvifparam2_if y();
  virtual svvifparam2_if #(.Q(7)) v;
  virtual svvifparam2_if w;
  svvifparam2_mon m;
  int k;
  int errors = 0;
  initial begin
    v = x;
    w = y;
    m = new(x);
    k = v.Q + w.Q;
    if (v.P.n != 3 || v.P.w != 8 || v.Q != 7 || w.Q != 5 || k != 12) begin
      $error("%0d %0d %0d %0d %0d", v.P.n, v.P.w, v.Q, w.Q, k); errors++;
    end
    if (v.P.w + w.P.n != 11) begin $error("sum %0d", v.P.w + w.P.n); errors++; end
    if (w.L != 10) begin $error("w.L %0d", w.L); errors++; end
    m.show();
    if (errors == 0 && m.errors == 0) $display("PASSED");
  end
endmodule
