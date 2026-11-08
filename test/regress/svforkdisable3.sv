// Expected values: IEEE 1800-2017 9.3.2 (fork-join), 9.6.2 (disabling a named fork ends it and
// execution continues after the fork statement).
`timescale 1ns/1ns
module svforkdisable3;
  string q[$];
  int bad = 0;
  function automatic void rec(string s); q.push_back($sformatf("%s@%0d", s, int'($time))); endfunction
  function automatic int cnt(string s); cnt = 0; foreach (q[i]) if (q[i] == s) cnt++; endfunction
  initial begin
    fork : t3
      begin #200 rec("never"); end
      begin #10 rec("t3 child"); end
    join
    rec("t3 joined");
  end
  initial begin #50 rec("killer"); disable t3; rec("killed"); end
  initial begin
    automatic string exp[$] = '{"t3 child@10", "killer@50", "killed@50", "t3 joined@50"};
    #300;
    foreach (exp[i]) if (cnt(exp[i]) != 1) begin $error("missing/duplicate event %s", exp[i]); bad++; end
    if (q.size() != exp.size()) begin $error("unexpected events, got %0d want %0d", q.size(), exp.size()); bad++; end
    if (bad == 0) $display("PASSED"); else $display("FAILED");
  end
endmodule
