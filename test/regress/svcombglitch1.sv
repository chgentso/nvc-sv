// Self-checking. IEEE 1800-2017 9.2.2.2.1: an always_comb process executes
// to completion before the processes sensitive to its variables resume, and
// those read only its final values.  Two blocks that assign a default and
// then conditionally override it, each reading the other's result, are in a
// stable state; the transient default value must not wake the other block
// (otherwise the pair never reaches the end of the time step).
// Explicit event controls on such a variable still see each change.
module svcombglitch1;
  logic a = 1, b = 1;
  always_comb begin a = 1'b0; if (b) a = 1'b1; end
  always_comb begin b = 1'b0; if (a) b = 1'b1; end

  logic d = 0, q;
  int nq = 0;
  always_comb begin q = ~d; if (d) q = 1'b1; end   // q follows 1
  always @(q) nq++;
  logic o;
  int nobs = 0;
  always_comb begin o = q; end
  always @(o) nobs++;

  initial begin
    #1 if (a !== 1'b1 || b !== 1'b1) $error("a=%b b=%b expected 1 1", a, b);
    d = 1;
    #1 if (q !== 1'b1) $error("q = %b expected 1", q);
    // q went 1 -> 0 -> 1 inside one activation of the always_comb block: the
    // update events still resume an explicit event control (9.4.2) but the
    // always_comb block sensitive to q does not run for them
    if (nq != 1) $error("q event seen %0d times, expected 1", nq);
    if (nobs != 0) $error("always_comb observer ran %0d times for q, expected 0", nobs);
    d = 0;
    #1 if (q !== 1'b1) $error("q = %b expected 1", q);
    $display("PASSED");
  end
endmodule
