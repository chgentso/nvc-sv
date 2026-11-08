// Expected values: IEEE 1800-2017 18.17 (randsequence: productions executed in
// order, repeat/if productions), 9.4.1 (#1 delays), 21.2.1.
// main : a b c;  a adds 1; b = two d's, each adds 10; c = e (+100) if n > 5 else f (+1000).
// Per pass: n grows by 1 + 20 + (100 or 1000); each code block takes #1.
`timescale 1ns/1ns
module svblock3;
  int n = 0;
  int errors = 0;
  time a_times[$];
  initial begin
    repeat (3) randsequence (main)
      main : a b c;
      a : { #1; n += 1; a_times.push_back($time); };
      b : repeat (2) d;
      d : { #1; n += 10; };
      c : if (n > 5) e else f;
      e : { #1; n += 100; };
      f : { #1; n += 1000; };
    endsequence
    // pass 1: n = 1, 11, 21, c takes e (21 > 5) -> 121; pass 2: 122, 132, 142, 242;
    // pass 3: 243, 253, 263, 363
    if (n != 363) begin $error("n = %0d, expected 363", n); errors++; end
    // each pass takes 4 time units (a, d, d, c)
    if ($time != 12) begin $error("time = %0t, expected 12", $time); errors++; end
    if (a_times.size() != 3) begin $error("a ran %0d times", a_times.size()); errors++; end
    else begin
      if (a_times[0] != 1) begin $error("a_times[0] = %0t", a_times[0]); errors++; end
      if (a_times[1] != 5) begin $error("a_times[1] = %0t", a_times[1]); errors++; end
      if (a_times[2] != 9) begin $error("a_times[2] = %0t", a_times[2]); errors++; end
    end
    if (errors == 0) $display("PASSED");
  end
endmodule
