// Self-checking: sequential/parallel blocks (join, join_any, join_none), disable, wait fork.
// Expected values: IEEE 1800-2017 9.3.1 (begin-end), 9.3.2 (fork-join: join waits for all, join_any for the
// first, join_none for none; unfinished children keep running), 9.3.5 (disable of a named block or fork),
// 9.6.1 (wait fork). Two same-time writes to x give one of the two values (9.4.1, order unspecified).
// Times are in ns (timescale given explicitly). tm[] records the time at which each marker was reached.
`timescale 1ns/1ns
`define CHK(g, e) if ((g) !== (e)) begin errs++; $error("check failed at line %0d", `__LINE__); end
module svlrm_c9b;
  int x; int errs;
  int tm[string];
  initial begin
    tm["start"] = $time;
    fork
      begin #3 tm["A"] = $time; end
      begin #1 tm["B"] = $time; end
      begin #2 tm["C"] = $time; end
    join
    tm["after join"] = $time;
    fork
      begin #5 tm["D"] = $time; end
      begin #2 tm["E"] = $time; end
    join_any
    tm["after join_any"] = $time;
    fork
      begin #7 tm["F"] = $time; end
    join_none
    tm["after join_none"] = $time;
    #10 tm["end"] = $time;
  end
  initial begin
    #30;
    begin : blk
      tm["in blk"] = $time;
      disable blk;
      tm["not shown 1"] = $time;
    end
    tm["after blk"] = $time;
    fork : f1
      begin #1 tm["f1a"] = $time; end
      begin #2 tm["f1b"] = $time; disable f1; end
      begin #10 tm["f1c"] = $time; end
    join
    tm["after f1"] = $time;
    fork begin #1 x = 1; end begin #1 x = 2; end join
    tm["after x"] = $time;
    fork
      for (int i = 0; i < 3; i++) begin
        automatic int j = i;
        fork #(j + 1) tm[$sformatf("j%0d", j)] = $time; join_none
      end
    join
    tm["after jfork"] = $time;
    #5;
    tm["done"] = $time;
    fork : wf
      #3 tm["w1"] = $time;
      #6 tm["w2"] = $time;
    join_none
    #4 disable wf;
    #10 tm["end2"] = $time;
    wait fork;
    tm["wait fork done"] = $time;
    // ---- checks (all processes have finished by now)
    `CHK(tm["start"], 0)
    `CHK(tm["B"], 1) `CHK(tm["C"], 2) `CHK(tm["A"], 3) `CHK(tm["after join"], 3)
    `CHK(tm["E"], 5) `CHK(tm["after join_any"], 5) `CHK(tm["after join_none"], 5)
    `CHK(tm["D"], 8) `CHK(tm["F"], 12) `CHK(tm["end"], 15)
    `CHK(tm["in blk"], 30) `CHK(tm.exists("not shown 1"), 0) `CHK(tm["after blk"], 30)
    `CHK(tm["f1a"], 31) `CHK(tm["f1b"], 32) `CHK(tm.exists("f1c"), 0) `CHK(tm["after f1"], 32)
    `CHK(x == 1 || x == 2, 1) `CHK(tm["after x"], 33)
    `CHK(tm["after jfork"], 33)
    `CHK(tm["j0"], 34) `CHK(tm["j1"], 35) `CHK(tm["j2"], 36)
    `CHK(tm["done"], 38)
    `CHK(tm["w1"], 41) `CHK(tm.exists("w2"), 0)
    `CHK(tm["end2"], 52) `CHK(tm["wait fork done"], 52)
    if (errs == 0) $display("PASSED");
  end
endmodule
