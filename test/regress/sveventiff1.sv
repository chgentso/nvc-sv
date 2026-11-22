module sveventiff1;
  logic sig = 0;
  logic qual = 0;
  int hits = 0;

  always @((sig) iff (qual)) begin
    hits++;
  end

  initial begin
    #1 sig = 1; // qual == 0: ignored
    #1 if (hits !== 0) $fatal(1, "unexpected hit when qual=0: %0d", hits);
    qual = 1;
    #1 sig = 0; // qual == 1: hit 1
    #1 sig = 1; // qual == 1: hit 2
    #1 if (hits !== 2) $fatal(1, "hits=%0d exp=2", hits);
    qual = 0;
    #1 sig = 0; // qual == 0: ignored
    #1 if (hits !== 2) $fatal(1, "hits=%0d exp=2 after qual=0", hits);
    $display("PASSED");
    $finish;
  end
endmodule
