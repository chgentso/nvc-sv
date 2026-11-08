// Expected values: IEEE 1800-2017 16.8 (local variables in sequences and
// properties, assigned in sequence match items), 16.9.3 (sampled values),
// 16.12.6 (implication), 16.14 (assertion action blocks).
// Reference model (edge k at time 5+10k, k=0..9, sampled values):
//   d_k = k;  en_k = (k odd);  vld_k = ((k-1)%3==0 for k>=1)
//   p1: attempt k matches the antecedent iff k odd && k%3==0, with
//       x=f(d_k)=k+1 and y=g(d_{k+1})=2(k+1); the consequent is checked at edge
//       k+2: (d==x+y)||h(d)  ->  h(k+2) is true, so a1 never fails.
//   a1b (consequent d==x+y only): attempt k=3 fails at edge 5 (time 55);
//       attempt k=9 is incomplete at the end of simulation.
//   a3 (consequent d+7==x+y): attempt k=3 passes at edge 5 (d=5, x+y=12).
//   p2: attempt k (odd) has x=f(d)+g(d)=3k+1, checked at edge k+1: k+1>=3k-4;
//       k=1 passes (edge 2, time 25), k=3,5,7 fail at edges 4,6,8 (45,65,85).
// The number of times f/g/h are invoked is not defined by the LRM and is not
// checked.  Whether vacuous successes run the pass action is not relied on.
module svsvalocalcall1;
  logic clk = 0;
  int d = 0;
  logic en = 0, vld = 0;
  int fc = 0, gc = 0, hc = 0;
  int errs;
  longint a1_fail[$], a1_pass[$], a1b_fail[$], a1b_pass[$], a3_fail[$], a3_pass[$], a2_fail[$], a2_pass[$];
  function automatic int f(int v); fc++; return v + 1; endfunction
  function automatic int g(int v); gc++; return v * 2; endfunction
  function automatic bit h(int v); hc++; return v > 2; endfunction
  always #5 clk = ~clk;
  always @(posedge clk) begin
    d <= d + 1;
    en <= (d % 2) == 0;
    vld <= (d % 3) == 0;
  end
  property p1;
    int x, y;
    @(posedge clk) (en, x = f(d)) ##1 (vld, y = g(d)) |-> ##1 (d == x + y) || h(d);
  endproperty
  property p1b;
    int x, y;
    @(posedge clk) (en, x = f(d)) ##1 (vld, y = g(d)) |-> ##1 (d == x + y);
  endproperty
  property p3;
    int x, y;
    @(posedge clk) (en, x = f(d)) ##1 (vld, y = g(d)) |-> ##1 (d + 7 == x + y);
  endproperty
  property p2;
    int x;
    @(posedge clk) (en, x = f(d) + g(d)) |=> d >= x - 5;
  endproperty
  a1: assert property (p1) a1_pass.push_back($time); else a1_fail.push_back($time);
  a1b: assert property (p1b) a1b_pass.push_back($time); else a1b_fail.push_back($time);
  a3: assert property (p3) a3_pass.push_back($time); else a3_fail.push_back($time);
  a2: assert property (p2) a2_pass.push_back($time); else a2_fail.push_back($time);
  function automatic bit contains(longint q[$], longint v);
    foreach (q[i]) if (q[i] == v) return 1;
    return 0;
  endfunction
  task automatic chk_fail(string n, longint got[$], longint exp[$]);
    got.sort(); 
    if (got.size() != exp.size()) begin $error("%s: %0d failures, expected %0d", n, got.size(), exp.size()); errs++; end
    else foreach (exp[i]) if (got[i] != exp[i]) begin $error("%s: failure #%0d at %0d, expected %0d", n, i, got[i], exp[i]); errs++; end
  endtask
  task automatic chk_pass(string n, longint got[$], longint exp);
    if (!contains(got, exp)) begin $error("%s: no pass reported at %0d", n, exp); errs++; end
  endtask
  initial begin
    #100;
    chk_fail("a1", a1_fail, '{});
    chk_fail("a1b", a1b_fail, '{55});
    chk_fail("a3", a3_fail, '{});
    chk_pass("a3", a3_pass, 55);
    chk_fail("a2", a2_fail, '{45, 65, 85});
    chk_pass("a2", a2_pass, 25);
    if (errs == 0) $display("PASSED");
    $finish;
  end
endmodule
