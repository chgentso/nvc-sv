// Expected values: IEEE 1800-2017 9.3.2 (fork-join, join_any), 9.6.2 (disable of named blocks/forks).
// Events are logged with their time and compared as a set (same-time order is not checked).
`timescale 1ns/1ns
module svforkdisable1;
  string q[$];
  int bad = 0;
  function automatic void rec(string s); q.push_back($sformatf("%s@%0d", s, int'($time))); endfunction
  function automatic int cnt(string s); cnt = 0; foreach (q[i]) if (q[i] == s) cnt++; endfunction
  initial begin
    fork : outer
      begin
        fork : inner
          begin #5; rec("inner a"); disable inner; end
          begin #8; rec("inner b"); end
        join
        rec("after inner");
        #10 rec("outer a end");
      end
      begin #20; rec("outer b"); disable outer; end
      begin #100; rec("outer c"); end
    join
    rec("joined");
    lab: fork
      begin #3 rec("x"); end
      begin #4 rec("y"); disable lab; end
    join_any
    rec("any");
    fork : f3
      begin #1; rec("one"); end
      begin
        for (int i = 0; i < 5; i++) begin
          #2; rec($sformatf("i=%0d", i));
          if (i == 2) disable f3;
        end
      end
    join
    rec("end");
    #50;
    begin
      automatic string exp[$] = '{"inner a@5", "after inner@5", "outer a end@15", "outer b@20", "joined@20",
                        "x@23", "any@23", "y@24", "one@24", "i=0@25", "i=1@27", "i=2@29", "end@29"};
      foreach (exp[i]) if (cnt(exp[i]) != 1) begin $error("missing/duplicate event %s", exp[i]); bad++; end
      if (q.size() != exp.size()) begin $error("unexpected events, got %0d want %0d", q.size(), exp.size()); bad++; end
    end
    if (bad == 0) $display("PASSED"); else $display("FAILED");
  end
endmodule
