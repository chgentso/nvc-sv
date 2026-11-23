// A string literal inside a set holding a queue or array of strings, or a
// queue which is the element of an associative array (IEEE 1800-2017 11.4.13)
module vlog_inside_str1;
  string fields[$];
  string q[$];
  string sa[2];
  string aq[int][$];
  initial begin
    fields = '{"smoketest", "1", "new_rules"};
    q = fields[2:$];
    sa = '{"a", "new_rules"};
    aq[3] = q;
    $display("%0d %0d", "new_rules" inside {q}, "other" inside {q});
    $display("%0d %0d", "new_rules" inside {sa}, "other" inside {sa});
    $display("%0d %0d", "new_rules" inside {aq[3]}, "other" inside {aq[3]});
    $display("%0d %0d", "other" inside {"x", q}, "new_rules" inside {"x", q});
  end
endmodule
