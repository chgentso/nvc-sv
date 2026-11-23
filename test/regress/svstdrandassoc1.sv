// std::randomize of an associative array solves the values of its entries
// and keeps its keys
module svstdrandassoc1;
  int a[int];
  bit [7:0] b[string];
  int bad, diff;
  initial begin
    a[1] = 0; a[5] = 0; a[9] = 0;
    b["x"] = 0; b["y"] = 0;
    for (int n = 0; n < 10; n++) begin
      if (!std::randomize(a)) bad++;
      if (a.num() != 3 || !a.exists(1) || !a.exists(5) || !a.exists(9)) bad++;
      if (a[1] != a[5]) diff++;
    end
    for (int n = 0; n < 10; n++) begin
      if (!std::randomize(b) with { foreach (b[k]) b[k] inside {[10:20]}; })
        bad++;
      if (b.num() != 2 || b["x"] < 10 || b["x"] > 20 || b["y"] < 10
          || b["y"] > 20) bad++;
    end
    if (bad == 0 && diff > 5) $display("PASSED");
    else $display("FAILED %0d %0d", bad, diff);
  end
endmodule
