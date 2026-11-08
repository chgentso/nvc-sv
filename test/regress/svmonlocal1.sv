`timescale 1ns/1ns
// $strobe and $monitor of the ports and local variables of a task or function; the output goes to a file
// ($fstrobe, $fmonitor) that is read back at the end.
// Expected values: IEEE 1800-2017 21.2.2 (the strobe arguments are evaluated at the end of the time step),
// 21.2.3 (a monitor prints in its call step and whenever an argument changed), 13.3/13.4 (task and function
// formals are variables that live on), 21.3.
module svmonlocal1;
  // ---- checking helpers: the output goes to a file that is read back at the end ----
  integer fd = $fopen("svmonlocal1.out", "w");
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
  task automatic finish_checks();
    foreach (got[i]) begin $error("unexpected line [%s]", got[i]); fails++; end
    if (fails == 0) $display("PASSED");
  endtask

  integer g;
  task t_strobe(input integer a, input integer b);
    $fstrobe(fd, "t=%0d strobe a=%0d b=%0d g=%0d", $time, a, b, g);
    g = g + 1;
  endtask
  task t_mon(input integer p);
    $fmonitor(fd, "t=%0d mon p=%0d g=%0d", $time, p, g);
  endtask
  function integer f(input integer x);
    $fstrobe(fd, "t=%0d func x=%0d g=%0d", $time, x, g);
    return x + 1;
  endfunction
  integer r;
  initial begin
    g = 0;
    t_strobe(1, 2);
    #1 t_strobe(3, 4);
    #1 r = f(5);
    #1 t_mon(7);
    #1 g = 20;
    #1 g = 30;
    #1 $fclose(fd);
    slurp("svmonlocal1.out");
    if (r !== 6) begin $error("f(5) returned %0d", r); fails++; end
    // the strobe sees g after the increment that follows it in the same time step
    expect_line("t=0 strobe a=1 b=2 g=1");
    expect_line("t=1 strobe a=3 b=4 g=2");
    expect_line("t=2 func x=5 g=2");
    // the monitor starts at t=3 and then follows g (p never changes)
    expect_line("t=3 mon p=7 g=2");
    expect_line("t=4 mon p=7 g=20");
    expect_line("t=5 mon p=7 g=30");
    finish_checks();
  end
endmodule
