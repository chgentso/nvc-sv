// Expected values: IEEE 1800-2017 18.5.1 (external constraint blocks, `extern constraint`),
// 8.25 (parameterized classes; each specialization has its own copy of the constraints),
// 18.6.1 (randomize returns 1), 8.5 (class inheritance).
class base #(type T = int);
  rand T v;
  rand int w;
  extern constraint v_c;
  extern constraint w_c;
  static int cnt = 0;
endclass
class early extends base#(shortint);
endclass
constraint base::v_c { v inside {[3:5]}; }
constraint base::w_c { w == 7; }
class sub extends base#(bit [7:0]);
endclass
module svgenconstraint1;
  int bad = 0;
  initial begin
    automatic base#(int) a = new;
    automatic base#(bit [7:0]) b = new;
    automatic sub s = new;
    automatic early e = new;
    repeat (3) begin
      if (!a.randomize() || !b.randomize() || !s.randomize() || !e.randomize()) begin
        $error("randomize failed"); bad++;
      end
      if (!(e.v >= 3 && e.v <= 5)) begin $error("e.v=%0d", e.v); bad++; end
      if (!(a.v >= 3 && a.v <= 5)) begin $error("a.v=%0d", a.v); bad++; end
      if (!(b.v >= 3 && b.v <= 5)) begin $error("b.v=%0d", b.v); bad++; end
      if (!(s.v >= 3 && s.v <= 5)) begin $error("s.v=%0d", s.v); bad++; end
      if (a.w != 7 || b.w != 7 || s.w != 7 || e.w != 7) begin $error("w=%0d %0d %0d %0d", a.w, b.w, s.w, e.w); bad++; end
    end
    if (bad == 0) $display("PASSED"); else $display("FAILED");
  end
endmodule
