`timescale 1ns/1ns
// $monitor / $monitoroff / $monitoron, with the monitor output redirected to a file ($fmonitor) and checked.
// Expected values: IEEE 1800-2017 21.2.3 (a monitor line is printed in the time step where an argument changed, with
// the arguments evaluated when printed; $time does not trigger a print; $monitoron re-enables the monitor and makes it
// display in that time step), 21.3 (file I/O)
module svmonitor3;
  // ---- checking helpers: the monitor output goes to a file that is read back at the end ----
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
  task automatic finish_checks();
    foreach (got[i]) begin $error("unexpected line [%s]", got[i]); fails++; end
    if (fails == 0) $display("PASSED");
  endtask

  reg [3:0] a = 0;
  initial begin
    fd = $fopen("svmonitor3.out", "w");
    $fmonitor(fd, "m a=%0d t=%0d", a, $time);
    #1 $monitoroff;
    #1 $monitoron;
    #1 $monitoron;
    #1 $monitoron; $monitoron; $monitoron;
    #1 $monitoroff; $monitoroff; $monitoron;
    #1 $monitoroff; a = 7; $monitoron;
    #1 a = 8; $monitoron;
    #1 $monitoron; a = 9;
    #1 $monitoron;
    #1 $fclose(fd);
    slurp("svmonitor3.out");
    // one line in the first time step; none at t=1 (monitor off, nothing changed); then exactly one per step
    // that has a change or a $monitoron
    expect_line("m a=0 t=0");
    expect_line("m a=0 t=2");
    expect_line("m a=0 t=3");
    expect_line("m a=0 t=4");
    expect_line("m a=0 t=5");
    expect_line("m a=7 t=6");
    expect_line("m a=8 t=7");
    expect_line("m a=9 t=8");
    expect_line("m a=9 t=9");
    finish_checks();
  end
endmodule
