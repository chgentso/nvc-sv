// Test break and continue in multi-dimensional foreach loops (IEEE 1800-2017 12.7.3)

module svforeachbrk1;
  logic [3:0] grid[3][2];

  function automatic int count_break();
    count_break = 0;
    foreach (grid[i, j]) begin
      count_break++;
      break;
    end
  endfunction

  function automatic int count_continue();
    count_continue = 0;
    foreach (grid[i, j]) begin
      count_continue++;
      continue;
      count_continue += 100;
    end
  endfunction

  localparam int P_BRK = count_break();
  localparam int P_CNT = count_continue();

  initial begin
    automatic int r_brk = count_break();
    automatic int r_cnt = count_continue();

    if (P_BRK !== 1 || r_brk !== 1)
      $fatal(1, "break mismatch: P=%0d R=%0d", P_BRK, r_brk);
    if (P_CNT !== 6 || r_cnt !== 6)
      $fatal(1, "continue mismatch: P=%0d R=%0d", P_CNT, r_cnt);

    $display("PASSED");
    $finish;
  end
endmodule
