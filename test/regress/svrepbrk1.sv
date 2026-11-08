// Expected values: IEEE 1800-2017 12.8 (break and continue), 12.7.2 (repeat loop)
// n: break at the 4th pass -> 4.  m: all 5 passes, odd passes continue, k = 2 + 4 = 6.
// Third loop: 3 passes; each inner repeat adds 4 (k = 10, 110, 210 never exceeds 100 inside an inner pass before the
// break test except from the 2nd pass on, where it breaks at once), k == 9 never holds, so k += 100 each pass:
// pass 1: 6 -> 10 -> 110; pass 2: inner breaks at once -> 210; pass 3: -> 310.
module svrepbrk1;
  initial begin
    automatic int n = 0, m = 0, k = 0;
    repeat (10) begin
      n++;
      if (n == 4) break;
    end
    repeat (5) begin
      m++;
      if (m % 2) continue;
      k += m;
    end
    repeat (3) begin
      repeat (4) begin
        if (k > 100) break;
        k++;
      end
      if (k == 9) continue;
      k += 100;
    end
    if (n == 4 && m == 5 && k == 310) $display("PASSED");
    else $error("n=%0d m=%0d k=%0d, expected 4 5 310", n, m, k);
  end
endmodule
