// Expected values: IEEE 1800-2017 9.3.2 (fork-join waits only for its own children; join_none and
// join_any semantics; spawned processes start when the parent blocks or ends), 9.4.2 (@event), 9.4.3.
// Checked: event set with times plus the parent-before-spawned-child constraints. Same-time order
// between unrelated processes is not checked.
`timescale 1ns/1ns
module svforknest1;
  event e;
  string q[$];
  int bad = 0;
  function automatic void rec(string s); q.push_back($sformatf("%s@%0d", s, int'($time))); endfunction
  function automatic int idx(string s); idx = -1; foreach (q[i]) if (q[i] == s) idx = i; endfunction
  function automatic int cnt(string s); cnt = 0; foreach (q[i]) if (q[i] == s) cnt++; endfunction
  task automatic ord(string a, string b);
    if (idx(a) < 0 || idx(b) < 0 || idx(a) > idx(b)) begin $error("%s must precede %s", a, b); bad++; end
  endtask
  initial begin
    fork
      begin
        fork rec("j1 n1"); join_none
        fork rec("j1 n2"); join_none
        rec("j1");
      end
      begin
        fork rec("j2 n1"); join_none
        rec("j2");
      end
      rec("j3");
    join
    rec("after join");
    fork
      begin #1 rec("any slow"); end
      begin
        fork rec("ja n1"); join_none
        rec("ja");
      end
    join_any
    rec("after join_any");
    #5;
    fork
      begin
        fork begin rec("d1"); #1 rec("d1 late"); end join_none
        fork rec("d2"); join_none
        #0 rec("p0 after #0");
      end
    join_none
    fork rec("x1"); join_none
    fork rec("x2"); join_none
    @e;
    rec("got e");
  end
  initial begin
    #20;
    fork rec("e1"); join_none
    fork rec("e2"); join_none
    -> e;
    rec("fired");
  end
  initial begin
    automatic string exp[$] = '{"j1 n1@0", "j1 n2@0", "j1@0", "j2 n1@0", "j2@0", "j3@0", "after join@0",
                      "ja n1@0", "ja@0", "after join_any@0", "any slow@1",
                      "d1@5", "d1 late@6", "d2@5", "p0 after #0@5", "x1@5", "x2@5",
                      "e1@20", "e2@20", "fired@20", "got e@20"};
    #40;
    foreach (exp[i]) if (cnt(exp[i]) != 1) begin $error("missing/duplicate event %s", exp[i]); bad++; end
    if (q.size() != exp.size()) begin $error("unexpected events, got %0d want %0d", q.size(), exp.size()); bad++; end
    ord("j1@0", "j1 n1@0"); ord("j1@0", "j1 n2@0"); ord("j2@0", "j2 n1@0");
    ord("ja@0", "ja n1@0");
    ord("d1@5", "d1 late@6");
    if (bad == 0) $display("PASSED"); else $display("FAILED");
  end
endmodule
