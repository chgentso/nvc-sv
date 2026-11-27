module sv_disable_hier1;
  int i;
  int x;

  initial begin
    begin : named
      for (i = 0; i < 10; ++i) begin : loop
        if (i == 5) disable sv_disable_hier1.named;
      end
    end
    if (i !== 5) $fatal(1, "expected i=5, got %0d", i);

    begin : blk
      x = 0;
      fork : fork_blk
        begin
          #4;
          x = 3;
        end
        begin : begin_blk
          x = 1;
          #2;
          x = 2;
        end
      join_none
      #1;
      disable fork_blk.begin_blk;
      #2;
      if (x !== 1) $fatal(1, "expected x=1, got %0d", x);
      #2;
      if (x !== 3) $fatal(1, "expected x=3, got %0d", x);
    end

    $display("PASSED");
    $finish;
  end
endmodule
