// Expected values: IEEE 1800-2017 9.3.2 (join_none), 9.6.2 (disable of a named fork, also from a task).
// Events are logged with their time and compared as a set.
`timescale 1ns/1ns
module svforkdisable2;
  string q[$];
  int bad = 0;
  function automatic void rec(string s); q.push_back($sformatf("%s@%0d", s, int'($time))); endfunction
  function automatic int cnt(string s); cnt = 0; foreach (q[i]) if (q[i] == s) cnt++; endfunction
  initial begin
    fork : worker
      begin #5 rec("w1"); end
      begin #50 rec("w2"); end
    join_none
    #10;
    disable worker;
    rec("disabled");
    #100 rec("end");
  end
  initial begin
    fork : other
      begin #20 rec("o1"); end
      begin forever #7 rec("tick"); end
    join_none
    #25 disable other;
    #30 rec("after other");
  end
  task automatic stopper();
    #3 disable viatask;
  endtask
  initial begin
    fork : viatask
      begin #1 rec("v1"); end
      begin #20 rec("v2"); end
      begin stopper(); end
    join_none
    #30 rec("vt done");
  end
  initial begin
    automatic string exp[$] = '{"w1@5", "disabled@10", "end@110", "o1@20", "tick@7", "tick@14", "tick@21",
                      "after other@55", "v1@1", "vt done@30"};
    #310;
    foreach (exp[i]) if (cnt(exp[i]) != 1) begin $error("missing/duplicate event %s", exp[i]); bad++; end
    if (q.size() != exp.size()) begin $error("unexpected events, got %0d want %0d", q.size(), exp.size()); bad++; end
    if (bad == 0) $display("PASSED"); else $display("FAILED");
  end
endmodule
