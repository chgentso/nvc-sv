// Expected values: IEEE 1800-2017 9.4.2 (event control: each always @(posedge) block resumes on a 0->1 edge of clk),
// 9.2.1/9.2.2 (initial and always procedures), 27.4 (generate loops/conditionals create the blocks).
// Same-time process order is not specified (4.7, 9.3.2), so only the number of wakeups per process is checked.
`timescale 1ns/1ns
module svgenwake1;
  logic clk = 0;
  int hits[string];
  time when[string];
  int err = 0;
  initial #5 clk = 1;
  `define HIT(n) always @(posedge clk) begin if (!hits.exists(n)) hits[n] = 0; hits[n]++; when[n] = $time; end
  `HIT("top-a")
  for (genvar i = 0; i < 2; i++) begin : g
    `HIT($sformatf("g[%0d].a", i))
    for (genvar j = 0; j < 2; j++) begin : h
      `HIT($sformatf("g[%0d].h[%0d].a", i, j))
      initial hits[$sformatf("init g[%0d].h[%0d]", i, j)] = 1;
    end
    `HIT($sformatf("g[%0d].b", i))
    initial hits[$sformatf("init g[%0d]", i)] = 1;
  end
  if (1) begin : c
    `HIT("c.a")
    if (1) begin : d
      `HIT("c.d.a")
    end
    `HIT("c.b")
  end
  `HIT("top-b")

  task automatic chk(string n, int exp_hits);
    if (!hits.exists(n) || hits[n] != exp_hits) begin
      err++; $error("%s: wrong hit count, expected %0d", n, exp_hits);
    end
  endtask

  initial begin
    #1;
    chk("init g[0]", 1); chk("init g[1]", 1);
    chk("init g[0].h[0]", 1); chk("init g[0].h[1]", 1);
    chk("init g[1].h[0]", 1); chk("init g[1].h[1]", 1);
    #20;
    foreach (when[k]) if (when[k] != 5)
      begin err++; $error("%s woke at %0t, expected 5", k, when[k]); end
    chk("top-a", 1); chk("top-b", 1);
    chk("g[0].a", 1); chk("g[1].a", 1); chk("g[0].b", 1); chk("g[1].b", 1);
    chk("g[0].h[0].a", 1); chk("g[0].h[1].a", 1);
    chk("g[1].h[0].a", 1); chk("g[1].h[1].a", 1);
    chk("c.a", 1); chk("c.d.a", 1); chk("c.b", 1);
    if (hits.num() != 6 + 13) begin err++; $error("unexpected number of processes: %0d", hits.num()); end
    if (err == 0) $display("PASSED");
  end
endmodule
