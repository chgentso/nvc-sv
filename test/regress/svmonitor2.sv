`timescale 1ns/1ns
// Several modules start a monitor at different times; the most recent monitor call is the one that reports.
// The monitor output goes to a file ($fmonitor) that is read back at the end.
// Expected values: IEEE 1800-2017 21.2.3 (a monitor prints in the time step of its call and then in every time
// step in which one of its arguments changed; the most recent call is the active one; $monitor calls issued at the
// same time are unordered, so the test accepts either of leaf b/leaf c as the survivor of time 2), 21.3
module leaf #(parameter id = 1, parameter start = 1, parameter step = 3) ();
  reg [7:0] s = 0;
  wire [7:0] w = s + id;
  initial begin
    #(start) $fmonitor(svmonitor2.fd, "leaf%0d t=%0d s=%0d w=%0d", id, $time, s, w);
    #(step) s = id * 10;
    #(step) s = id * 10 + 1;
    #(step) s = id * 10 + 2;
  end
endmodule
module svmonitor2;
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

  initial fd = $fopen("svmonitor2.out", "w");
  leaf #(1, 1, 3) a();
  leaf #(2, 2, 2) b();
  leaf #(3, 2, 5) c();
  leaf #(4, 30, 1) d();
  initial begin
    #40 $fclose(fd);
    slurp("svmonitor2.out");
    // t=1: leaf a is the only monitor
    expect_line("leaf1 t=1 s=0 w=1");
    // t=2: leaf b and leaf c both call; at least the survivor reports
    if (take("leaf2 t=2 s=0 w=2")) begin
      // leaf b is the survivor: s changes at 4, 6, 8
      expect_line("leaf2 t=4 s=20 w=22");
      expect_line("leaf2 t=6 s=21 w=23");
      expect_line("leaf2 t=8 s=22 w=24");
    end else begin
      // leaf c is the survivor: s changes at 7, 12, 17
      expect_line("leaf3 t=2 s=0 w=3");
      expect_line("leaf3 t=7 s=30 w=33");
      expect_line("leaf3 t=12 s=31 w=34");
      expect_line("leaf3 t=17 s=32 w=35");
    end
    // t=30: leaf d takes over; its s changes at 31, 32, 33
    expect_line("leaf4 t=30 s=0 w=4");
    expect_line("leaf4 t=31 s=40 w=44");
    expect_line("leaf4 t=32 s=41 w=45");
    expect_line("leaf4 t=33 s=42 w=46");
    // An implementation that keeps replaced monitors alive may print their later changes as well; these lines
    // are tolerated but not required (only the most recent monitor is active, 21.2.3).
    optional_line("leaf1 t=4 s=10 w=11");
    optional_line("leaf1 t=7 s=11 w=12");
    optional_line("leaf1 t=10 s=12 w=13");
    optional_line("leaf2 t=4 s=20 w=22");
    optional_line("leaf2 t=6 s=21 w=23");
    optional_line("leaf2 t=8 s=22 w=24");
    optional_line("leaf2 t=2 s=0 w=2");
    optional_line("leaf3 t=2 s=0 w=3");
    optional_line("leaf3 t=7 s=30 w=33");
    optional_line("leaf3 t=12 s=31 w=34");
    optional_line("leaf3 t=17 s=32 w=35");
    finish_checks();
  end
endmodule
