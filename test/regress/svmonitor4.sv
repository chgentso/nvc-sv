`timescale 1ns/1ns
// Three monitors on overlapping variables, started in the same time step; the output goes to a file ($fmonitor)
// that is read back at the end.
// Expected values: IEEE 1800-2017 21.2.3 (a monitor prints once in the time step of its call and once per time
// step in which one of its arguments changed, all arguments evaluated at the end of the step), 21.3.
// The order of the three monitors' lines inside one time step, the order of the three initial blocks, and whether
// a monitor call replaces an earlier one are not specified for $fmonitor, so the test requires that at least one
// complete monitor series is present, that each change step prints something, and that every printed line is
// one of the lines the three monitors can produce.
module svmonitor4;
  // ---- checking helpers: the output goes to a file that is read back at the end ----
  integer fd = $fopen("svmonitor4.out", "w");
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
  function automatic int count(input string s);
    count = 0;
    foreach (got[i]) if (got[i] == s) count++;
  endfunction
  function automatic bit take(input string s);
    foreach (got[i]) if (got[i] == s) begin got.delete(i); return 1; end
    return 0;
  endfunction
  function automatic bit series(input string l[$]);
    foreach (l[i]) if (count(l[i]) != 1) return 0;
    return 1;
  endfunction
  task automatic finish_checks();
    foreach (got[i]) begin $error("unexpected line [%s]", got[i]); fails++; end
    if (fails == 0) $display("PASSED");
  endtask

  reg [3:0] a = 0, b = 0;
  initial $fmonitor(fd, "F1 t=%0d a=%0d", $time, a);
  initial $fmonitor(fd, "F2 t=%0d b=%0d", $time, b);
  initial $fmonitor(fd, "F3 t=%0d a=%0d b=%0d", $time, a, b);
  initial begin
    #10 a = 1; b = 1;
    #10 b = 2;
    #10 b = 3; a = 3;
    #10 $fclose(fd);
    slurp("svmonitor4.out");
    begin
      automatic string f1[$] = '{"F1 t=0 a=0", "F1 t=10 a=1", "F1 t=30 a=3"};
      automatic string f2[$] = '{"F2 t=0 b=0", "F2 t=10 b=1", "F2 t=20 b=2", "F2 t=30 b=3"};
      automatic string f3[$] = '{"F3 t=0 a=0 b=0", "F3 t=10 a=1 b=1", "F3 t=20 a=1 b=2", "F3 t=30 a=3 b=3"};
      if (!series(f1) && !series(f2) && !series(f3)) begin
        $error("no monitor printed its complete series"); fails++;
      end
      // F1 never prints at t=20 (a is unchanged); the step must be reported by a monitor that watches b
      if (count("F2 t=20 b=2") + count("F3 t=20 a=1 b=2") == 0) begin
        $error("nothing printed at t=20"); fails++;
      end
      // the candidate lines of every monitor; whatever is left in the file is a bogus line
      foreach (f1[i]) void'(take(f1[i]));
      foreach (f2[i]) void'(take(f2[i]));
      foreach (f3[i]) void'(take(f3[i]));
    end
    finish_checks();
  end
endmodule
