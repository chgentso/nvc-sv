// Processes waiting for the same event are all released by it (order is not checked)
// Expected values: IEEE 1800-2017 9.4.2 (event control), 4.7 (the order of processes released by
// one event is undefined, so only the set of wake-ups is compared), 13.4 (task called in a loop).
`timescale 1ns/1ns
module svwake1;
  reg clk = 0;
  int errors = 0;
  // 14 toggles at 8,16,...,112; posedges at 8,24,40,56,72,88,104
  initial repeat (14) #8 clk = ~clk;

  int cnt[string];  // "name@time" -> count
  int tot[string];  // name -> count
  function automatic void hit(string n);
    string k = $sformatf("%s@%0d", n, $time);
    cnt[k] = (cnt.exists(k) ? cnt[k] : 0) + 1;
    tot[n] = (tot.exists(n) ? tot[n] : 0) + 1;
  endfunction

  task automatic w; @(posedge clk); endtask

  initial forever begin w(); hit("A"); end
  initial forever begin w(); hit("B"); end
  initial forever begin w(); hit("C"); end
  initial forever begin @(posedge clk); hit("P"); end
  initial forever begin @(posedge clk); hit("Q"); end
  initial forever begin @(posedge clk); @(posedge clk); hit("R"); end
  initial begin #20; forever begin @(posedge clk); hit("D"); end end

  function automatic int get(string n, int t);
    string k = $sformatf("%s@%0d", n, t);
    if (!cnt.exists(k)) return 0;
    return cnt[k];
  endfunction

  initial begin
    automatic int pe[$] = '{8, 24, 40, 56, 72, 88, 104};
    #200;
    // A, B, C, P, Q: once at every posedge
    foreach (pe[k]) begin
      if (get("A", pe[k]) != 1) begin $error("A at %0d", pe[k]); errors++; end
      if (get("B", pe[k]) != 1) begin $error("B at %0d", pe[k]); errors++; end
      if (get("C", pe[k]) != 1) begin $error("C at %0d", pe[k]); errors++; end
      if (get("P", pe[k]) != 1) begin $error("P at %0d", pe[k]); errors++; end
      if (get("Q", pe[k]) != 1) begin $error("Q at %0d", pe[k]); errors++; end
      // R: every second posedge (the 2nd, 4th, 6th)
      if (get("R", pe[k]) != ((k % 2 == 1) ? 1 : 0)) begin $error("R at %0d", pe[k]); errors++; end
      // D: starts waiting at t=20, so misses the first posedge
      if (get("D", pe[k]) != ((pe[k] >= 20) ? 1 : 0)) begin $error("D at %0d", pe[k]); errors++; end
    end
    // no wake-up at any other time
    foreach (tot[n]) begin
      if (tot[n] != ((n == "R") ? 3 : (n == "D") ? 6 : 7)) begin $error("%s woke %0d times", n, tot[n]); errors++; end
    end
    if (tot.num() != 7) begin $error("%0d processes woke", tot.num()); errors++; end
    if (errors == 0) $display("PASSED");
  end
endmodule
