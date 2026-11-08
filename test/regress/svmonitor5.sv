`timescale 1ns/1ns
// $strobe, $display and monitors that are started or replaced during the run; all output goes to a file
// ($fstrobe, $fmonitor, $fdisplay) that is read back at the end. Lines carry their time step and are compared as a
// multiset, so the order of the strobe lines relative to the monitor lines of one time step is not checked.
// Expected values: IEEE 1800-2017 21.2.2 (a strobe prints at the end of the time step with the values at that
// time), 21.2.3 (a monitor prints at the end of the time step of its call and when an argument changed; the most
// recent monitor call is the one that reports), 21.2.1 ($display prints at once), 21.3.
module svmonitor5;
  // ---- checking helpers: the output goes to a file that is read back at the end ----
  integer fd = $fopen("svmonitor5.out", "w");
  integer fails = 0;
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
  initial begin $fstrobe(fd, "S0 t=%0d a=%0d", $time, a); $fmonitor(fd, "M t=%0d a=%0d", $time, a); end
  initial $fmonitor(fd, "FMON t=%0d b=%0d", $time, b);
  initial begin
    #10 a = 1; $fstrobe(fd, "S1 t=%0d a=%0d", $time, a); $fmonitor(fd, "M2 t=%0d a=%0d", $time, a);
    $fdisplay(fd, "D t=%0d", $time); a = 2; $fstrobe(fd, "S2 t=%0d", $time); b = 1;
    #10 $fstrobe(fd, "S3 t=%0d", $time); a = 3; $fstrobe(fd, "S4 t=%0d", $time); b = 2;
    #10 $fclose(fd);
    slurp("svmonitor5.out");
    expect_line("S0 t=0 a=0");
    expect_line("D t=10");
    expect_line("S1 t=10 a=2");   // end-of-step value, not the value at the call
    expect_line("S2 t=10");
    expect_line("S3 t=20");
    expect_line("S4 t=20");
    // the monitor started last (M2 at t=10) is active in every case, and it reports the final values of the step
    expect_line("M2 t=10 a=2");
    expect_line("M2 t=20 a=3");
    // the first monitor lines of t=0: the monitor of the later initial block may have replaced the other one
    if (!take("M t=0 a=0") && !take("FMON t=0 b=0")) begin $error("no monitor line at t=0"); fails++; end
    optional_line("M t=0 a=0");
    optional_line("FMON t=0 b=0");
    // an implementation may keep replaced monitors alive; their later lines are tolerated
    optional_line("M t=10 a=2");
    optional_line("M t=20 a=3");
    optional_line("FMON t=10 b=1");
    optional_line("FMON t=20 b=2");
    finish_checks();
  end
endmodule
