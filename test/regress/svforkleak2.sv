// Expected values: IEEE 1800-2017 9.3.2 (join_any), 9.6.1 (disable fork), 9.4.2 (event control).
// scl rises at 7, 17, 27, ... (ns). Hand trace of wait_a: n=0 starts at 3 (timeout 5 -> 8) and sees the
// edge at 7: hit, resume at 10. For n>=1 the call starts at a multiple of 10: odd n (timeout 9) sees the
// edge at +7: hit; even n (timeout 5) times out at +5: miss. 20000 calls: hits = 1 + 10000, misses = 9999.
`timescale 1ns/1ns
module svforkleak2;
  logic scl = 0;
  bit r;
  int hits, misses;

  initial begin
    #7;
    forever begin
      scl = 1;
      #5;
      scl = 0;
      #5;
    end
  end

  task automatic wait_a(input int timeout, output bit r);
    fork
      begin : iso_fork
        fork
          begin @(posedge scl); r = 1; end
          begin #(timeout); r = 0; end
        join_any
        disable fork;
      end : iso_fork
    join
  endtask

  initial begin
    #3;
    for (int n = 0; n < 20000; n++) begin
      wait_a(n % 2 ? 9 : 5, r);
      if (r) hits++; else misses++;
      #(10 - ($time % 10));
    end
    if (hits != 10001 || misses != 9999) begin
      $error("hits %0d misses %0d, want 10001 9999", hits, misses);
      $display("FAILED");
    end else $display("PASSED");
    $finish;
  end
endmodule
