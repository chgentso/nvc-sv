// IEEE 1800-2017 17.2, 17.3: a checker formal of type event takes an event
// expression such as posedge clk as its actual; the formal is then usable as
// a clocking event in procedures, assertions and sampled value functions
// (16.9.3), and checker formals may be untyped with a default.
checker svchkevent1_chk(event ev, logic a, logic b = 1'b1);
  int n = 0;
  always_ff @(ev) n <= n + 1;
  a1: assert property (@(ev) a |=> b) else svchkevent1.fail(1);
  c1: cover property (@(ev) $rose(a, @(ev))) svchkevent1.rose_seen++;
  final svchkevent1.cnt[0] = n;
endchecker

module svchkevent1;
  logic clk = 0, a = 0, b = 0;
  int rose_seen = 0, errs = 0;
  int cnt[1];

  function void fail(int i); errs++; endfunction

  always #5 clk = ~clk;

  svchkevent1_chk c_pos(posedge clk, a, b);
  svchkevent1_chk c_neg(negedge clk, a);       // b defaults to 1

  initial begin
    @(negedge clk) a = 1;  // Sampled at 15 by c_pos
    @(negedge clk) b = 1;  // b at 25
    @(negedge clk) a = 0;
    @(negedge clk);
    #1;
    if (errs != 0) $fatal(1, "assertion failed %0d times", errs);
    // $rose(a) once for each checker instance
    if (rose_seen != 2) $fatal(1, "rose_seen = %0d", rose_seen);
    $display("PASSED");
    $finish;
  end
endmodule
