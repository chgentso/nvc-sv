// IEEE 1800-2017 20.12: assertion control tasks take a levels argument and
// a list of scopes or assertions.  Levels 0 applies to the named scope and
// all instances below it, levels n to n levels of hierarchy starting with
// the named scope (1 = only the scope itself); naming an assertion applies
// to that assertion only; assertions elsewhere are unaffected.  $assertoff
// stops new attempts but lets attempts in progress finish, $assertkill also
// aborts the attempts in progress, and $assertcontrol with control type 4
// (Off), 3 (On) and 5 (Kill) acts like these tasks.
`timescale 1ns/1ns
module svassertscope1_leaf(input logic clk);
  int n = 0, n2 = 0;
  A_L: assert property (@(posedge clk) 0) else n++;
  A_L2: assert property (@(posedge clk) 0) else n2++;
endmodule

module svassertscope1_mid(input logic clk);
  int n = 0;
  A_M: assert property (@(posedge clk) 0) else n++;
  svassertscope1_leaf u_l(clk);
endmodule

module svassertscope1_sib(input logic clk);
  int n = 0, nlong = 0;
  A_S: assert property (@(posedge clk) 0) else n++;
  // Each attempt fails two clocks after it starts
  A_LONG: assert property (@(posedge clk) 1 |-> ##2 0) else nlong++;
endmodule

module svassertscope1;
  logic clk = 0;
  int n = 0;
  always #5 clk = ~clk;          // posedges at 5, 15, ...

  A_T: assert property (@(posedge clk) 0) else n++;

  svassertscope1_mid u_m(clk);
  svassertscope1_sib u_s(clk);

  int b_t, b_m, b_l, b_l2, b_s, b_long;

  task automatic mark;
    b_t = n; b_m = u_m.n; b_l = u_m.u_l.n; b_l2 = u_m.u_l.n2; b_s = u_s.n;
    b_long = u_s.nlong;
  endtask

  // Run three clocks and check how many failures each assertion reported
  task automatic phase(string name, int t, int m, int l, int l2, int s);
    mark();
    repeat (3) @(posedge clk);
    @(negedge clk);
    if (n - b_t != t || u_m.n - b_m != m || u_m.u_l.n - b_l != l
        || u_m.u_l.n2 - b_l2 != l2 || u_s.n - b_s != s)
      $fatal(1, "%s: top %0d mid %0d leaf %0d leaf2 %0d sib %0d", name,
             n - b_t, u_m.n - b_m, u_m.u_l.n - b_l, u_m.u_l.n2 - b_l2,
             u_s.n - b_s);
  endtask

  initial begin
    @(negedge clk);
    phase("all on", 3, 3, 3, 3, 3);

    $assertoff(1, u_m);                         // u_m only
    phase("off 1 u_m", 3, 0, 3, 3, 3);

    $asserton;
    $assertoff(0, u_m);                         // u_m and below
    phase("off 0 u_m", 3, 0, 0, 0, 3);

    $asserton(0);
    $assertoff(2, svassertscope1);              // top and its children
    phase("off 2 top", 0, 0, 3, 3, 0);

    $asserton(0, svassertscope1);
    $assertoff(0, u_m.u_l.A_L);                 // One assertion
    phase("off A_L", 3, 3, 0, 3, 3);

    $asserton(0, u_m.u_l.A_L);
    $assertoff(0, u_m.u_l, u_s);                // Two scopes
    phase("off two scopes", 3, 3, 0, 0, 0);

    $asserton(1, u_s);
    $asserton(0, u_m.u_l);
    $assertcontrol(4, 15, 7, 0, u_m);           // Off, u_m and below
    phase("control off", 3, 0, 0, 0, 3);
    $assertcontrol(3, 15, 7, 0, u_m);           // On
    phase("control on", 3, 3, 3, 3, 3);

    // $assertoff lets the two attempts in progress fail; $assertkill not
    $assertoff(0, u_s.A_LONG);
    mark();
    repeat (4) @(posedge clk);
    @(negedge clk);
    if (u_s.nlong - b_long != 2)
      $fatal(1, "assertoff: %0d attempts finished, expected 2",
             u_s.nlong - b_long);
    $asserton(0, u_s.A_LONG);
    repeat (3) @(posedge clk);
    @(negedge clk);                              // Two attempts in progress
    $assertkill(0, u_s);
    mark();
    repeat (4) @(posedge clk);
    @(negedge clk);
    if (u_s.nlong - b_long != 0 || u_s.n - b_s != 0)
      $fatal(1, "assertkill: long %0d short %0d", u_s.nlong - b_long,
             u_s.n - b_s);
    $asserton(0, u_s);
    repeat (3) @(posedge clk);
    @(negedge clk);
    $assertcontrol(5, 15, 7, 0, u_s.A_LONG);     // Kill
    mark();
    repeat (4) @(posedge clk);
    @(negedge clk);
    if (u_s.nlong - b_long != 0 || u_s.n - b_s != 4)
      $fatal(1, "assertcontrol kill: long %0d short %0d",
             u_s.nlong - b_long, u_s.n - b_s);

    $display("PASSED");
    $finish;
  end
endmodule
