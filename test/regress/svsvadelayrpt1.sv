// Expected values: IEEE 1800-2017 16.9.2 (##[+] is ##[1:$], ##[*] is ##[0:$]),
// 16.9.8 (first_match), 16.12.6 (|=>: the consequent starts one tick after the match),
// 16.14.3 (cover property: the pass statement runs once per successful attempt, and a
// vacuous success is not counted), 16.5.1 (sampled values). A reference model over the
// values sampled at each posedge predicts the hit counts h1, h2, h3.
`timescale 1ns/1ns
module svsvadelayrpt1;
  logic clk = 0, a = 0, b = 0, c = 0;
  int h1, h2, h3;
  always #5 clk = ~clk;
  property p1; a ##[+] b; endproperty
  property p2; a ##[*] b; endproperty
  cover property (@(posedge clk) a ##[+] b) h1++;
  cover property (@(posedge clk) a ##[*] b) h2++;
  cover property (@(posedge clk) first_match(a ##[+] b) |=> c) h3++;
  bit sa[$], sb[$], sc[$];
  always @(posedge clk) begin sa.push_back(a); sb.push_back(b); sc.push_back(c); end
  initial begin
    @(negedge clk) a = 1;
    @(negedge clk) a = 0; b = 1;
    @(negedge clk) b = 0; c = 1;
    @(negedge clk) a = 1; b = 1; c = 0;
    repeat (3) @(negedge clk);
    check();
    $finish;
  end
  task automatic check();
    int n = sa.size();
    int e1 = 0, e2 = 0, e3 = 0, errs = 0;
    for (int k = 0; k < n; k++) begin
      if (!sa[k]) continue;
      // h1: a ##[1:$] b, h2: a ##[0:$] b; one hit per attempt with a match
      for (int j = 1; k + j < n; j++) if (sb[k+j]) begin e1++; break; end
      for (int j = 0; k + j < n; j++) if (sb[k+j]) begin e2++; break; end
      // h3: first_match(a ##[1:$] b) |=> c
      for (int j = 1; k + j < n; j++) if (sb[k+j]) begin
        if (k + j + 1 < n && sc[k+j+1]) e3++;
        break;
      end
    end
    if (n != 7) begin $error("expected 7 ticks, saw %0d", n); errs++; end
    if (h1 !== e1) begin $error("h1 = %0d expected %0d", h1, e1); errs++; end
    if (h2 !== e2) begin $error("h2 = %0d expected %0d", h2, e2); errs++; end
    if (h3 !== e3) begin $error("h3 = %0d expected %0d", h3, e3); errs++; end
    if (e1 != 3 || e2 != 4 || e3 != 1) begin $error("model sanity %0d %0d %0d", e1, e2, e3); errs++; end
    if (errs == 0) $display("PASSED");
  endtask
endmodule
