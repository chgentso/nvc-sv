// A wait on an element of an associative array is not evaluated again
// while the array is unchanged and wakes when a key is added or an
// element is written
module vlog_waitassoc1;
  typedef struct { bit expected; } exp_t;
  exp_t flags[string];
  int   counts[int];
  bit   go;
  logic clk = 0;
  always #1 clk = ~clk;        // unrelated activity
  initial begin
    fork
      begin
        wait (flags["a"].expected == 1 && go);
        $display("%0t: flag woke", $time);
      end
      begin
        wait (counts[7] == 3);
        $display("%0t: count woke", $time);
      end
    join_none
    #10;
    go = 1;                          // not enough: key still missing
    #10 flags["a"] = '{expected: 0}; // key added, value 0
    #10 counts[7] = 1;
    #10 flags["a"].expected = 1;     // element written in place
    #10 counts[7] = 2;
    #10 counts[1] = 9;               // other key added
    #10 counts[7] = 3;
    #10 $finish;
  end
endmodule
