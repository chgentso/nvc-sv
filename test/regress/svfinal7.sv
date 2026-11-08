// A final procedure may modify variables and call functions; it runs when no events remain.
// Expected values: IEEE 1800-2017 9.2.3 (final procedures run at the end of simulation, here reached when the
// initial block has ended), 13.4 (functions), 6.8 (n=4 after the initial block, n=5 after the increment).
`timescale 1ns/1ns
module svfinal7;
  int n = 0;
  int rep = -1;
  function automatic void report();
    rep = n;
    $display("report n=%0d", n);
  endfunction
  initial begin n = 4; #1 ; end
  final begin
    n = n + 1;
    report();
    if (rep !== 5 || n !== 5) $error("report must see n=5, got rep=%0d n=%0d", rep, n);
    else $display("PASSED");
  end
endmodule
