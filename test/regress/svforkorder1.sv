// Expected values: IEEE 1800-2017 9.3.2: join_none children do not start until the parent blocks or
// terminates; join waits for all children. Only these constraints and the times are checked; the
// order between unrelated same-time processes is not.
`timescale 1ns/1ns
module svforkorder1;
  reg clk = 0;
  initial repeat (6) #5 clk = ~clk;
  string q[$];
  int bad = 0;
  function automatic void rec(string s); q.push_back($sformatf("%s@%0d", s, int'($time))); endfunction
  function automatic int idx(string s); idx = -1; foreach (q[i]) if (q[i] == s) idx = i; endfunction
  function automatic int cnt(string s); cnt = 0; foreach (q[i]) if (q[i] == s) cnt++; endfunction

  initial begin
    @(posedge clk);
    rec("older");
  end

  task automatic spawn(string tag);
    fork
      rec({"child ", tag});
    join_none
    rec({"parent after ", tag});
  endtask

  initial begin
    @(posedge clk);
    spawn("a");
    rec("parent blocks");
    #1;
    fork
      rec("join child 1");
      rec("join child 2");
    join
    rec("parent after join");
  end

  initial begin
    @(posedge clk);
    rec("other");
  end

  initial begin
    // The expected events in one order that 9.3.2 allows
    automatic string exp[$] = '{"older@5", "parent after a@5", "parent blocks@5", "child a@5",
                                "other@5", "join child 1@6", "join child 2@6", "parent after join@6"};
    #30;
    foreach (exp[i]) if (cnt(exp[i]) != 1) begin $error("missing/duplicate event %s", exp[i]); bad++; end
    if (q.size() != exp.size()) begin $error("unexpected events"); bad++; end
    // join_none child starts only after the parent has reached its blocking statement
    if (!(idx("parent after a@5") < idx("parent blocks@5") && idx("parent blocks@5") < idx("child a@5")))
      begin $error("child a started before the parent blocked"); bad++; end
    // join: both children precede the statement after the join
    if (!(idx("join child 1@6") < idx("parent after join@6") && idx("join child 2@6") < idx("parent after join@6")))
      begin $error("join did not wait for its children"); bad++; end
    if (bad == 0) $display("PASSED"); else $display("FAILED");
  end
endmodule
