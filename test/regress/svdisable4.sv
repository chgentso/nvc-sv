// More disable of named blocks from forked children: blocked in wait/forever,
// nested block, and disable of a block that has already completed (no effect).
// Expected values: IEEE 1800-2017 9.6.2 (disable), 9.3.2 (join_none), 9.4 (timing
// controls), 9.4.3 (wait), 9.4.2 (posedge).  Events at equal times are never ordered.
`timescale 1ns/1ns
`define CHK(c, m) if (!(c)) begin err++; $error(m); end
module svdisable4;
  reg clk = 0;
  reg flag = 0;
  integer n = 0;
  int err = 0;
  time t_ca, t_after_a, t_a20, t_cb, t_after_b, t_b1, t_b11, t_after_c, t_c7, t_c57;
  time t_ce, t_e_end, t_noop, t_e30;
  integer n_after_a;
  bit never_b, never_c, never_d;
  initial repeat (30) #5 clk = ~clk;
  initial begin
    begin : a
      fork
        begin #22 t_ca = $time; disable a; end
      join_none
      forever @(posedge clk) n = n + 1;
    end
    t_after_a = $time; n_after_a = n;
    #20 t_a20 = $time;
  end
  initial begin
    begin : b
      fork
        begin #7 t_cb = $time; disable b; end
      join_none
      wait (flag);
      never_b = 1;
    end
    t_after_b = $time;
    #1 t_b1 = $time;
    #10 t_b11 = $time;
  end
  initial begin
    begin : c
      begin : d
        fork
          begin #3 disable c; end
        join_none
        #50 never_c = 1;
      end
      never_d = 1;
    end
    t_after_c = $time;
    #7 t_c7 = $time;
    #50 t_c57 = $time;
  end
  initial begin
    begin : e
      fork
        begin #3 t_ce = $time; end
      join_none
      #20 t_e_end = $time;
    end
    fork
      begin #1 disable e; t_noop = $time; end
    join_none
    #30 t_e30 = $time;
  end
  initial begin
    #100 flag = 1;
    #5;
    `CHK(t_ca == 22, "ca")
    `CHK(t_after_a == 22, "after a")
    `CHK(n_after_a == 2, "posedges at 5 and 15 before a is disabled")
    `CHK(t_a20 == 42, "a+20")
    `CHK(t_cb == 7, "cb")
    `CHK(t_after_b == 7, "after b")
    `CHK(t_b1 == 8, "b+1")
    `CHK(t_b11 == 18, "b+11")
    `CHK(t_after_c == 3, "after c")
    `CHK(t_c7 == 10, "c+7")
    `CHK(t_c57 == 60, "c+57")
    `CHK(t_ce == 3, "ce")
    `CHK(t_e_end == 20, "block e is not disturbed")
    `CHK(t_noop == 21, "disable of a finished block has no effect")
    `CHK(t_e30 == 50, "e+30")
    `CHK(!never_b && !never_c && !never_d, "disabled statements must not run")
    if (err == 0) $display("PASSED");
  end
endmodule
