// A named begin-end block which is the body of a join_none fork branch is
// disabled by name from the parent while the branch is blocked in a task.
// IEEE 1800-2017 9.6.2 (disable a named block: execution of the block
// stops wherever it is, including in tasks it called) and 9.3.2.
module svforkdisblk1;
  event go;
  int reached = 0, other = 0;

  task t();
    @go;
  endtask

  initial begin
    fork
      begin : blk
        t();
        reached = 1;
      end
      begin : keep
        t();
        other = 1;
      end
    join_none
    #5 disable blk;
    #1 ->go;
    #1;
    if (reached == 0 && other == 1) $display("PASSED");
    else $error("reached=%0d other=%0d", reached, other);
  end
endmodule
