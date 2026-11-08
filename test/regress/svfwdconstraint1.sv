// Expected values: IEEE 1800-2017 18.5 (constraint blocks, inside, foreach, soft constraints 18.5.14),
// 8.27 (forward typedef of a class), 18.6.1 (randomize returns 1 when all constraints are met).
// The soft constraints c[i].lo == 5+i do not conflict with the hard ones, so they must hold.
package p;
  typedef class cfg;
  class env;
    rand cfg c[2];
    rand int n;
    function new(); foreach (c[i]) c[i] = new(); endfunction
    constraint cons_c {
      n inside {[1:3]};
      foreach (c[i]) {
        soft c[i].lo == 5 + i;
        c[i].hi > c[i].lo;
      }
    }
  endclass
  class cfg;
    rand int lo, hi;
    constraint r_c { hi < 100; lo >= 0; }
  endclass
endpackage
module svfwdconstraint1;
  import p::*;
  env e = new();
  int bad = 0;
  initial begin
    repeat (3) begin
      if (!e.randomize()) begin $error("randomize failed"); bad++; end
      else begin
        if (!(e.n >= 1 && e.n <= 3)) begin $error("n=%0d", e.n); bad++; end
        if (e.c[0].lo != 5 || e.c[1].lo != 6) begin $error("soft lo %0d %0d", e.c[0].lo, e.c[1].lo); bad++; end
        foreach (e.c[i])
          if (!(e.c[i].hi > e.c[i].lo && e.c[i].hi < 100 && e.c[i].lo >= 0)) begin
            $error("c[%0d] lo=%0d hi=%0d", i, e.c[i].lo, e.c[i].hi); bad++;
          end
      end
    end
    if (bad == 0) $display("PASSED"); else $display("FAILED");
    $finish;
  end
endmodule
