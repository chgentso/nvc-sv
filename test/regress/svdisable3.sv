// Disabling an enclosing named block from a forked child terminates the parent
// that is waiting inside the block; execution resumes after the block.
// Expected values: IEEE 1800-2017 9.6.2 (disable of a named block and the
// processes spawned from it), 9.3.2 (fork join_none), 9.4.1 (delay), 9.4.2 (event wait).
`timescale 1ns/1ns
`define CHK(c, m) if (!(c)) begin err++; $error(m); end
module svdisable3;
  event e;
  int err = 0;
  time t_child, t_after_blk, t_c2, t_after_blk2, t_17, t_c3, t_after_f3, t_later;
  bit never1, never2, never3;
  initial begin
    begin : blk
      fork
        begin #10 t_child = $time; disable blk; end
      join_none
      #100 never1 = 1;
    end
    t_after_blk = $time;
    #200 t_later = $time;
  end
  initial begin
    begin : blk2
      fork
        begin #12 t_c2 = $time; disable blk2; end
      join_none
      @e never2 = 1;
    end
    t_after_blk2 = $time;
    #5 t_17 = $time;
  end
  initial begin
    fork : f3
      begin
        fork
          begin #30 t_c3 = $time; disable f3; end
        join_none
        #500 never3 = 1;
      end
    join
    t_after_f3 = $time;
    #1000;
    `CHK(t_child == 10, "child time")
    `CHK(t_after_blk == 10, "resume after blk")
    `CHK(t_c2 == 12, "c2 time")
    `CHK(t_after_blk2 == 12, "resume after blk2")
    `CHK(t_17 == 17, "blk2 + 5")
    `CHK(t_c3 == 30, "c3 time")
    `CHK(t_after_f3 == 30, "resume after f3")
    `CHK(t_later == 210, "later")
    `CHK(!never1 && !never2 && !never3, "disabled statements must not run")
    if (err == 0) $display("PASSED");
  end
  initial begin #400 -> e; end
endmodule
