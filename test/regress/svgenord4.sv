// Expected values: IEEE 1800-2017 27.4 (loop generate), 9.2 (initial procedures), 4.7 (scheduling).
// Every initial runs exactly once at its delay; the execution order of same-time processes is not
// checked (events are logged with their time and compared as a set).
`timescale 1ns/1ns
module svgenord4;
  string q[$];
  int bad = 0;
  function automatic void rec(string s); q.push_back($sformatf("%s@%0d", s, int'($time))); endfunction
  function automatic int cnt(string s); cnt = 0; foreach (q[i]) if (q[i] == s) cnt++; endfunction
  genvar i;
  generate
    for (i = 0; i < 3; i++) begin : g
      initial begin #3 rec($sformatf("A g[%0d]", i)); end
      initial begin #3 rec($sformatf("B g[%0d]", i)); end
      always @(i) ;
    end
  endgenerate
  initial #3 rec("M1");
  initial #3 rec("M2");
  initial begin #2; rec("t2"); #1 rec("t3"); end
  initial begin
    automatic string exp[$] = '{"A g[0]@3", "A g[1]@3", "A g[2]@3", "B g[0]@3", "B g[1]@3", "B g[2]@3",
                      "M1@3", "M2@3", "t2@2", "t3@3"};
    #10;
    foreach (exp[k]) if (cnt(exp[k]) != 1) begin $error("missing/duplicate event %s", exp[k]); bad++; end
    if (q.size() != exp.size()) begin $error("unexpected events, got %0d", q.size()); bad++; end
    if (bad == 0) $display("PASSED"); else $display("FAILED");
  end
endmodule
