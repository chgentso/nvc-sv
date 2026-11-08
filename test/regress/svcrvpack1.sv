// Self-checking: packed structs, nested objects and std::randomize with inline constraints.
// Expected values: IEEE 1800-2017 18.5 (constraints, solve-before), 18.7 (inline constraints),
// 18.12 (std::randomize), 7.2.1 (packed structure member order).
typedef enum bit [1:0] {RA, RB, RC} col_t;
typedef struct packed { bit [3:0] lo; bit [3:0] hi; } byte_t;
typedef struct packed {
  bit ill;
  byte_t inner;
  int sew;
  col_t col;
  logic [3:0] lg;
} wide_t;
typedef struct packed { bit [7:0] p; bit [7:0] q; } pair_t;

class sub;
  rand pair_t pr;
  constraint c { pr.p > pr.q; pr.q > 100; }
endclass

class c;
  rand wide_t w;
  pair_t fixed;
  rand pair_t pr;
  rand sub sb;
  rand bit [7:0] v;
  function new; sb = new; fixed = 16'h1234; endfunction
  constraint k {
    w.sew inside {8, 16, 32};
    w.inner.hi == 4'hA;
    w.inner.lo < 3;
    w.col != RB;
    w.lg[1:0] == 2'b10;
    w.ill == 1;
    pr == 16'h0102 + v;
    v inside {[1:3]};
    fixed.p == 8'h12;
    v > fixed.q[3:0] - 3;
    sb.pr.p < 120;
    solve w before v;
  }
endclass

module svcrvpack1;
  pair_t loc;
  initial begin
    automatic c o = new;
    automatic int ok = 1;
    automatic int n = 0;
    repeat (50) begin
      if (!o.randomize()) begin $error("randomize failed"); ok = 0; end
      else begin
        if (!(o.w.sew inside {8,16,32}) || o.w.inner.hi != 4'hA || o.w.inner.lo >= 3
            || o.w.col == RB || o.w.lg[1:0] != 2'b10 || !o.w.ill
            || o.pr != 16'h0102 + o.v || !(o.v inside {[1:3]})
            || !(o.sb.pr.p > o.sb.pr.q) || o.sb.pr.q <= 100 || o.sb.pr.p >= 120) begin
          $error("constraint violated: w=%p pr=%p v=%0d", o.w, o.pr, o.v); ok = 0;
        end
      end
    end
    repeat (10) begin
      if (!std::randomize(loc) with { loc.p == 5; loc.q inside {[1:4]}; }
          || loc.p != 5 || !(loc.q inside {[1:4]})) begin $error("std::randomize result bad: %p", loc); ok = 0; end
    end
    repeat (10) begin
      if (!o.randomize() with { w.sew == 16; w.inner == 8'h1A; } || o.w.sew != 16 || o.w.inner != 8'h1A) begin
        $error("inline constraint result bad"); ok = 0;
      end
    end
    if (ok) $display("PASSED");
  end
endmodule
