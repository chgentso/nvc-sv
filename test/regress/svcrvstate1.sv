// Self-checking: constraints referencing object state, constraint_mode and randomize(var) with randc.
// Expected values: IEEE 1800-2017 18.5 (constraints, state variables), 18.9 (constraint_mode),
// 18.11 (randomize with a variable list), 18.4.2 (randc).
class W; int id; function new(int i); id = i; endfunction endclass
class L;
  W seqs[$];
  rand int unsigned sel;
  randc bit [15:0] selc;
  constraint c1 { sel inside {[0:seqs.size()-1]}; }
  constraint c2 { selc inside {[0:seqs.size()-1]}; }
  function void go();
    for (int i = 0; i < 4; i++) begin W w = new(i); seqs.push_back(w); end
    c2.constraint_mode(0);
    if (!randomize()) $error("randomize() failed");
    if (!(sel < 4)) $error("sel=%0d, expected < 4", sel);
    c1.constraint_mode(0);
    c2.constraint_mode(1);
    if (!randomize(selc)) $error("randomize(selc) failed");
    if (!(selc < 4)) $error("selc=%0d, expected < 4", selc);
    if (sel < 4 && selc < 4) $display("PASSED");
  endfunction
endclass
module svcrvstate1;
  initial begin automatic L l = new; l.go(); end
endmodule
