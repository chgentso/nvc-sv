// Expected values: IEEE 1800-2017 9.3.2 (join_none children start when the parent blocks or ends),
// 15.4 (mailbox put/get/peek/num). Checked: the event set with times, "parent" before its
// children, a spawner before the children it spawned, and that every mailbox message is delivered exactly
// once at its put time. The order between unrelated same-time processes is not checked.
`timescale 1ns/1ns
module svforkmbox1;
  mailbox m = new();
  string q[$];
  int bad = 0;
  int got_val[4], got_t[4], ngot = 0;
  int peek_v = -1, peek_t = -1, npeek = 0;
  function automatic void rec(string s); q.push_back($sformatf("%s@%0d", s, int'($time))); endfunction
  function automatic int idx(string s); idx = -1; foreach (q[i]) if (q[i] == s) idx = i; endfunction
  function automatic int cnt(string s); cnt = 0; foreach (q[i]) if (q[i] == s) cnt++; endfunction
  task automatic ord(string a, string b);
    if (idx(a) < 0 || idx(b) < 0 || idx(a) > idx(b)) begin $error("%s must precede %s", a, b); bad++; end
  endtask
  task automatic spawn_two(string tag);
    fork rec({tag, " x"}); join_none
    fork rec({tag, " y"}); join_none
  endtask
  initial begin
    for (int i = 0; i < 4; i++) begin
      automatic int id = i;
      fork rec($sformatf("child %0d", id)); join_none
    end
    rec("parent");
    #1;
    fork
      rec("a");
      rec("b");
      rec("c");
    join_none
    #1;
    for (int i = 0; i < 3; i++) begin
      automatic int id = i;
      fork begin rec($sformatf("blk %0d", id)); end join_none
    end
    #1;
    spawn_two("t1");
    spawn_two("t2");
    #1;
    fork
      begin
        fork rec("n1"); join_none
        fork rec("n2"); join_none
        rec("outer1");
      end
      rec("outer2");
    join_none
    #1;
    for (int i = 0; i < 4; i++) begin
      automatic int id = i;
      fork begin int v; m.get(v); got_val[ngot] = v; got_t[ngot] = int'($time); ngot++; end join_none
    end
    fork begin int v; m.peek(v); peek_v = v; peek_t = int'($time); npeek++; end join_none
    #1;
    m.put(1); #1; m.put(2); m.put(3); #1; m.put(4);
    #1;
    begin
      automatic string exp[$] = '{"child 0@0", "child 1@0", "child 2@0", "child 3@0", "parent@0",
                        "a@1", "b@1", "c@1", "blk 0@2", "blk 1@2", "blk 2@2",
                        "t1 x@3", "t1 y@3", "t2 x@3", "t2 y@3",
                        "n1@4", "n2@4", "outer1@4", "outer2@4"};
      foreach (exp[i]) if (cnt(exp[i]) != 1) begin $error("missing/duplicate event %s", exp[i]); bad++; end
      if (q.size() != exp.size()) begin $error("unexpected events"); bad++; end
      for (int i = 0; i < 4; i++) ord("parent@0", $sformatf("child %0d@0", i));
      ord("outer1@4", "n1@4");
      ord("outer1@4", "n2@4");
      if (ngot != 4) begin $error("%0d getters completed, want 4", ngot); bad++; end
      else begin
        automatic int seen = 0;
        for (int i = 0; i < 4; i++) begin
          int tput;
          tput = (got_val[i] == 1) ? 6 : (got_val[i] == 4) ? 8 : 7;
          if (got_val[i] < 1 || got_val[i] > 4 || got_t[i] != tput) begin
            $error("getter got %0d at %0d", got_val[i], got_t[i]); bad++;
          end else seen |= 1 << got_val[i];
        end
        if (seen != 'b11110) begin $error("messages not delivered exactly once"); bad++; end
      end
      if (m.num() != 0) begin $error("num=%0d, want 0", m.num()); bad++; end
      // peek does not remove; if it completed it saw a message at its put time
      if (npeek > 1 || (npeek == 1 && (peek_v < 1 || peek_v > 4 || peek_t != ((peek_v == 1) ? 6 : (peek_v == 4) ? 8 : 7))))
        begin $error("peek %0d at %0d", peek_v, peek_t); bad++; end
    end
    if (bad == 0) $display("PASSED"); else $display("FAILED");
  end
endmodule
