// Expected values: IEEE 1800-2017 9.3.2 (join_none children run concurrently with the parent and
// start no later than when it blocks or ends), 8.7 (constructors), 13.4.4 (fork..join_none in functions).
`timescale 1ns/1ns
string q[$];
function automatic void rec(string s); q.push_back($sformatf("%s@%0d", s, int'($time))); endfunction
function automatic int cnt(string s); cnt = 0; foreach (q[i]) if (q[i] == s) cnt++; endfunction
class W;
  int n;
  string name;
  function new(string nm);
    name = nm;
    fork
      begin #5; n = 1; rec({name, " child1"}); end
      begin #7; n = 2; rec({name, " child2"}); end
      begin rec({name, " child0"}); end
    join_none
    rec({name, " ctor done"});
  endfunction
endclass
module svforkelab1;
  W w, w2;
  int bad = 0;
  initial begin
    automatic string exp[$] = '{"w child0@0", "w child1@5", "w child2@7", "w ctor done@0",
                      "w2 child0@0", "w2 child1@5", "w2 child2@7", "w2 ctor done@0"};
    // 13.4.4: a function containing fork..join_none must be called from an initial/always thread
    w = new("w");
    w2 = new("w2");
    #10;
    if (w.n != 2 || w2.n != 2) begin $error("n=%0d %0d, want 2 2", w.n, w2.n); bad++; end
    foreach (exp[i]) if (cnt(exp[i]) != 1) begin $error("missing/duplicate event %s", exp[i]); bad++; end
    if (q.size() != exp.size()) begin $error("unexpected events"); bad++; end
    if (bad == 0) $display("PASSED"); else $display("FAILED");
  end
endmodule
