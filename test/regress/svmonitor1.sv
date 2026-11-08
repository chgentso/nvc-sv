`timescale 1ns/1ns
// $monitor (via $fmonitor), $monitoroff/on, $monitorh, $strobe and $display; all output goes to a file that is
// read back and compared at the end (lines are tagged with the time step and compared as a multiset, so the order
// of lines inside one time step is not checked).
// Expected values: IEEE 1800-2017 21.2.1 (display), 21.2.2 (strobe: arguments are those at the end of the time step),
// 21.2.3 (monitor: one line per time step in which an argument changed, $time does not trigger; $monitoroff
// suppresses; $monitoron re-enables and displays; a new monitor call replaces the old one), 9.4.2 (always @), 21.3
module svmonitor1;
  // ---- checking helpers: the output goes to a file that is read back at the end ----
  integer fd, fails = 0;
  string got[$];
  task automatic slurp(input string fn);
    integer f; string l;
    f = $fopen(fn, "r");
    if (f == 0) begin $error("cannot reopen %s", fn); fails++; return; end
    while ($fgets(l, f) > 0) begin
      if (l.len() > 0 && l[l.len()-1] == "\n") l = l.substr(0, l.len()-2);
      got.push_back(l);
    end
    $fclose(f);
  endtask
  function automatic bit take(input string s);
    foreach (got[i]) if (got[i] == s) begin got.delete(i); return 1; end
    return 0;
  endfunction
  task automatic expect_line(input string s);
    if (!take(s)) begin $error("missing line [%s]", s); fails++; end
  endtask
  task automatic optional_line(input string s);
    void'(take(s));
  endtask
  task automatic finish_checks();
    foreach (got[i]) begin $error("unexpected line [%s]", got[i]); fails++; end
    if (fails == 0) $display("PASSED");
  endtask

  reg [3:0] a = 0, b = 0;
  reg clk = 0;
  wire [4:0] sum = a + b;
  reg [7:0] r;
  integer cnt = 0;
  initial begin
    fd = $fopen("svmonitor1.out", "w");
    $fdisplay(fd, "start");
    $fmonitor(fd, "mon t=%0d a=%h b=%h sum=%0d", $time, a, b, sum);
    #1 a = 1;
    #1 a = 2; b = 3;
    #1 begin a = 4; a = 5; end
    #1 $monitoroff;
    #1 a = 6;
    #1 $monitoron;
    #1 b = 1;
    #1 $fmonitorh(fd, "hex t=%0d a=%0d b=%0d", $time, a, b);
    #1 a = 15;
    #1 $fmonitor(fd, "second t=%0d %b", $time, a[1:0]);
    #1 a = 14;
    #1 b = 7;
    #1 a = a;
    #1 $fstrobe(fd, "strobe t=%0d %0d a=%0d", $time, cnt, a);
    cnt = 5; a = 3; r <= 8'hab;
    #1 $fdisplay(fd, "display t=%0d a=%0d r=%h", $time, a, r);
    $fstrobe(fd, "s1 t=%0d r=%h", $time, r);
    $fstrobe(fd, "s2 t=%0d r=%h", $time, r);
    r <= 8'hcd;
    #1 $monitoroff;
    $fstrobeb(fd, "sb t=%0d %0d", $time, a);
    #5 $fclose(fd);
    slurp("svmonitor1.out");

    expect_line("start");
    // first monitor: one line at the call and one per step with a change (a=4;a=5 gives one line, with a=5)
    expect_line("mon t=0 a=0 b=0 sum=0");
    expect_line("mon t=1 a=1 b=0 sum=1");
    expect_line("mon t=2 a=2 b=3 sum=5");
    expect_line("mon t=3 a=5 b=3 sum=8");
    // off at t=4; the change at t=5 is not printed; $monitoron at t=6 displays the current values
    expect_line("mon t=6 a=6 b=3 sum=9");
    expect_line("mon t=7 a=6 b=1 sum=7");
    // $monitorh replaces the monitor at t=8
    expect_line("hex t=8 a=6 b=1");
    expect_line("hex t=9 a=15 b=1");
    // the next monitor only watches a[1:0] (t=10 call, t=11 change, t=12/13 no change, t=14 change)
    expect_line("second t=10 11");
    expect_line("second t=11 10");
    expect_line("second t=14 11");
    // 21.2.3 lets only the most recent monitor stay active; the lines an implementation that keeps the older
    // monitors alive prints in addition are tolerated (not required)
    optional_line("mon t=9 a=f b=1 sum=16");
    optional_line("mon t=11 a=e b=1 sum=15");
    optional_line("mon t=12 a=e b=7 sum=21");
    optional_line("mon t=14 a=3 b=7 sum=10");
    optional_line("hex t=11 a=14 b=1");
    optional_line("hex t=12 a=14 b=7");
    optional_line("hex t=14 a=3 b=7");
    // strobes see the end-of-step values, including the nonblocking update of the same step
    expect_line("strobe t=14 5 a=3");
    expect_line("display t=15 a=3 r=ab");
    expect_line("s1 t=15 r=cd");
    expect_line("s2 t=15 r=cd");
    expect_line("sb t=16 3");
    // always @(a): one line per step with a change (a=4;a=5 in one step: at least the final value; a=a is no event)
    expect_line("always t=1 a=1");
    expect_line("always t=2 a=2");
    optional_line("always t=3 a=4");
    expect_line("always t=3 a=5");
    expect_line("always t=5 a=6");
    expect_line("always t=9 a=15");
    expect_line("always t=11 a=14");
    expect_line("always t=14 a=3");
    finish_checks();
  end
  always @(a) $fdisplay(fd, "always t=%0d a=%0d", $time, a);
endmodule
