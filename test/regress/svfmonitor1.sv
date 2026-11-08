// $fmonitor / $fmonitorh / $fmonitorb to files, $monitoroff / $monitoron.
// Each monitor is given its own variables and the variables of an earlier
// monitor are not changed once a later monitor has been installed, so the
// expected file contents do not depend on whether several monitors coexist.
// Expected values: IEEE 1800-2017 21.2.3 (continuous monitoring: one display
// at the invocation, then one display at the end of a time step in which any
// argument changed, $time arguments do not trigger; $monitoroff/$monitoron),
// 21.3.1 (opening/closing files), 21.3.4.3 ($fgets).
// Left out: whether $monitoron itself makes the active monitors print once
// (accepted either way, but then with the current values), and the output of
// the $fmonitor sent to the descriptor with bit 31 set (stdout, not captured).
module svfmonitor1;
  reg [3:0] a = 0, b = 0, c = 0, d = 0, e = 0, s = 0;
  integer f1, f2, f3, n, errors = 0;
  string line, exp_lines[$];

  // read the file back as a queue of lines (without the newline)
  task automatic slurp(string fname, ref string lines[$]);
    integer fd;
    string l;
    lines.delete();
    fd = $fopen(fname, "r");
    if (fd == 0) begin $error("cannot reopen %s", fname); errors++; return; end
    while ($fgets(l, fd) != 0) begin
      if (l.len() > 0 && l.substr(l.len() - 1, l.len() - 1) == "\n") l = l.substr(0, l.len() - 2);
      lines.push_back(l);
    end
    $fclose(fd);
  endtask

  function automatic bit same(string x[$], string y[$]);
    if (x.size() != y.size()) return 0;
    foreach (x[i]) if (x[i] != y[i]) return 0;
    return 1;
  endfunction

  // the file must hold exactly exp, or exp with opt_line inserted at position
  // opt_at (the display that $monitoron may produce)
  task automatic expect_file(string fname, string tag, string exp[$], int opt_at, string opt_line);
    string got[$], alt[$];
    slurp(fname, got);
    alt = exp;
    alt.insert(opt_at, opt_line);
    if (!same(got, exp) && !same(got, alt)) begin
      $error("%s: file contents wrong (%0d lines)", tag, got.size());
      foreach (got[i]) $display("  got '%s'", got[i]);
      errors++;
    end
  endtask

  initial begin
    f1 = $fopen("svfmonitor1_a.tmp", "w");
    f2 = $fopen("svfmonitor1_b.tmp", "w");
    f3 = $fopen("svfmonitor1_c.tmp", "w");
    // phase 1: two monitored variables and $time
    $fmonitor(f1, "A a=%0d b=%0d t=%0d", a, b, $time);      // t=0: printed once
    #1 a = 1;                                               // t=1: one line
    #1 b = 2; a = 2;                                        // t=2: one line for two changes
    #1 a = 2;                                               // t=3: no change, no line
    #1 a = 3;                                               // t=4
    // phase 2: hex
    #1 $fmonitorh(f2, "B c=%h d=%h", c, d);                 // t=5
    #1 c = 4'ha;                                            // t=6
    #1 d = 4'h5;                                            // t=7
    // phase 3: binary, with monitoring switched off and on
    #1 $fmonitorb(f3, "C e=%b", e);                         // t=8
    #1 e = 4'b1010;                                         // t=9
    #1 $monitoroff;                                         // t=10
    #1 e = 4'b0001;                                         // t=11 while off: no line
    #1 $monitoron;                                          // t=12
    #1 e = 4'b1111;                                         // t=13
    #1 $fclose(f1); $fclose(f2); $fclose(f3);               // t=14
    $fmonitor(32'h80000001, "S one s=%0d", s);              // descriptor with bit 31: stdout
    #1 s = 1;
    #1;

    exp_lines = '{"A a=0 b=0 t=0", "A a=1 b=0 t=1", "A a=2 b=2 t=2", "A a=3 b=2 t=4"};
    expect_file("svfmonitor1_a.tmp", "fileA", exp_lines, 4, "A a=3 b=2 t=12");
    exp_lines = '{"B c=0 d=0", "B c=a d=0", "B c=a d=5"};
    expect_file("svfmonitor1_b.tmp", "fileB", exp_lines, 3, "B c=a d=5");
    exp_lines = '{"C e=0000", "C e=1010", "C e=1111"};
    expect_file("svfmonitor1_c.tmp", "fileC", exp_lines, 2, "C e=0001");
    if (errors == 0) $display("PASSED");
    $finish;
  end
endmodule
