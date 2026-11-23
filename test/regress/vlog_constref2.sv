// An argument of the caller passed on as the actual of a const ref port: the
// address of the variable is stored in the reference, so the variable must stay
// in memory when the JIT promotes local allocations to registers.
class Scb;
  int m;
  int total;
  function void chk(const ref int p, int addr);
    // addr takes the direction of the previous port, so it is a const ref too
    total += p + addr;
  endfunction
  function void go1(int a);
    chk(m, a);
  endfunction
  function void go2(int a);
    int t;
    t = a;
    chk(m, t);
  endfunction
endclass

function void fgo(Scb s, int a);
  s.chk(s.m, a);
endfunction

module vlog_constref2;
  initial begin
    Scb s = new;
    s.m = 100;
    fgo(s, 5);
    if (s.total != 105) $error("fgo %0d", s.total);
    s.go1(9);
    if (s.total != 214) $error("go1 %0d", s.total);
    s.go2(4);
    if (s.total != 318) $error("go2 %0d", s.total);
    $display("PASSED");
  end
endmodule
