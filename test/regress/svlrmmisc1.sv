// Assorted LRM features
//  $cast to a string (6.24.2), foreach over a hierarchical array (12.7.3),
//  ->> with an intra-assignment delay (15.5.2), $global_clock (14.14),
//  process::suspend and resume (9.7), disable of a task (9.6.2), streaming
//  operators on queues with a variable length (11.4.14), a covergroup bin
//  array from an array expression (19.5.1), a checker with parameters (17),
//  a ref argument which sees later writes (13.5.2), $assertoff with an
//  instance argument (20.12).
`timescale 1ns/1ns

module svlrmmisc1_sub;
  int arr[3] = '{1, 2, 3};
  int f = 0;
  logic clk = 0;
  always #5 clk = ~clk;
  int c = 0;
  always @(posedge clk) c <= c + 1;
  assert property (@(posedge clk) c < 3) else f++;
endmodule

checker svlrmmisc1_chk #(parameter int LIM = 3) (logic clk, int v);
  int bad = 0;
  assert property (@(posedge clk) v < LIM) else bad++;
endchecker

module svlrmmisc1;
  int errors = 0;
  task automatic check(bit ok, string what);
    if (!ok) begin $error("%s", what); errors++; end
  endtask

  svlrmmisc1_sub u();
  logic gclk = 0; always #5 gclk = ~gclk;
  global clocking gc @(posedge gclk); endclocking
  int gn = 0;
  always @($global_clock) gn++;

  svlrmmisc1_chk #(.LIM(4)) k1(gclk, gn);
  svlrmmisc1_chk k2(gclk, gn);

  string s;
  int sum = 0;
  bit q[$], r[$];
  bit [7:0] a = 8'b1100_1010;
  byte bq[$] = '{8'h12, 8'h34};
  bit [15:0] w;
  event e;
  realtime te = 0;
  process pr;
  int pn = 0;
  int tn = 0;
  int g = 0;
  bit saw3 = 0;
  int vals[4] = '{1, 2, 3, 4};
  covergroup cg with function sample(int x);
    cp: coverpoint x { bins b[] = vals; }
  endgroup
  cg cov = new;

  task automatic tk; forever begin #1 tn++; end endtask
  task st; begin #1 tn += 100; #1 tn += 100; end endtask
  task automatic waitg(ref int x); wait (x == 3); saw3 = 1; endtask

  initial begin
    check($cast(s, "abc") && s == "abc", "$cast to string");
    #1;
    foreach (u.arr[i]) sum += u.arr[i];
    check(sum == 6, "foreach over hierarchical array");
    q = {<<{a}};
    check(q.size() == 8 && q[0] == 0 && q[1] == 1, "streaming to a queue");
    r = {<<1{q}};
    check(r.size() == 8 && r[0] == 1 && r[7] == 0, "reverse stream of a queue");
    w = {<<4{bq}};
    check(w == 16'h4321, "nibble stream of a queue");
    cov.sample(2); cov.sample(4);
    check(cov.get_coverage() == 50.0, "bins from an array");
  end

  initial begin @(e) te = $realtime; end
  initial begin ->> #5 e; end

  initial begin pr = process::self(); forever begin #1 pn++; end end
  initial begin
    #3.5 pr.suspend();
    #5 check(pn == 3, "suspended process runs");
    pr.resume();
    #2 check(pn >= 4, "resumed process does not run");
  end

  initial tk;
  initial st;
  initial begin #1.5 disable st; end

  initial waitg(g);
  initial begin #2 g = 1; #2 g = 3; end

  initial begin
    $assertoff(0, u);
    #100;
    check(te == 5, "->> with a delay");
    check(tn == 99 + 100, "disable of a task");
    check(saw3, "ref argument sees later writes");
    check(u.f == 0, "$assertoff with an instance");
    check(gn == 10, "$global_clock");
    check(k1.bad == 6 && k2.bad == 7, "checker parameters");
    if (errors == 0) $display("PASSED");
    $finish;
  end
endmodule
