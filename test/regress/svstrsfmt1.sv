// Expected values: IEEE 1800-2017 21.3.3 ($sformatf returns a string), 6.16 (string ==, !=, < compare
// contents; "7" == "7", "7" != "8", "7" < "8"), 8.4 (class property access), 7.10 (queues)
class I; string label; int idx; endclass
module svstrsfmt1;
  I q[$];
  int n = 7;
  int errs = 0;
  initial begin
    I x;
    automatic bit m1 = 0, m2 = 0, df = 0, ls = 0;
    x = new;
    x.label = "7";
    q.push_back(x);
    for (int j = 0; j < q.size(); j++)
      if (q[j].label == $sformatf("%0d", n)) m1 = 1;
    if ($sformatf("%0d", n) == q[0].label) m2 = 1;
    if (q[0].label != $sformatf("%0d", n + 1)) df = 1;
    if (q[0].label < $sformatf("%0d", n + 1)) ls = 1;
    if (!m1) begin $error("label == sformatf failed"); errs++; end
    if (!m2) begin $error("sformatf == label failed"); errs++; end
    if (!df) begin $error("label != sformatf(n+1) failed"); errs++; end
    if (!ls) begin $error("label < sformatf(n+1) failed"); errs++; end
    if (errs == 0) $display("PASSED");
  end
endmodule
