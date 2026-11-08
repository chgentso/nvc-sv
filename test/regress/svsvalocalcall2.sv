`timescale 1ns/1ns
// Function and task calls taking sequence local variables as arguments: in a boolean expression of a sequence
// (alone, nested, with two local variables, in the consequent of an implication), in a match item assignment,
// in a repetition with an accumulating local variable, and as a void function or task called from a match item,
// with covers and an assertion over an LFSR stimulus
// Expected values: IEEE 1800-2017 16.6 (functions without output or ref arguments may be called in sequence
// expressions), 16.10 (local variables are passed by value, with the value of the thread at that point),
// 16.11 (a subroutine attached to a sequence element is called each time the element matches on a thread, with
// that thread's local variable values), 16.9.2 ([*n]), 16.12.7 (|=>), 16.14.3 (cover property of a sequence:
// one action per attempt, at its first match), 16.14.1 (fail action), 16.9.3 (sampled values).
//
// Reference model (tick k at time 5 + 10 k, D/V/W the values of d/v/w sampled at tick k, f(a) = (a + 3) % 16,
// g(a, b) = a ^ b, lt(a, b) = a < b):
//   c1 (1, x = d) ##1 (d == f(x))                         match at t+1 iff D[t+1] == f(D[t])
//   c2 (1, x = d) ##1 (1, x = g(x, d)) ##1 (d == x)       match at t+2 iff D[t+2] == D[t] ^ D[t+1]
//   c3 (v, x = d) ##1 (1, note(3, x))                      note at t+1 with D[t] iff V[t]; always matches
//   c4 (1, x = d) ##1 (1, y = f(x)) ##1 lt(g(y, x), d)     match at t+2 iff (f(D[t]) ^ D[t]) < D[t+2]
//   c5 (v, x = d) ##1 (w, vnote(5, x + 1))                vnote at t+1 with D[t] + 1 iff V[t] && W[t+1]
//   a6 (v, x = d) |=> (d != f(x))                         fails at t+1 iff V[t] && D[t+1] == f(D[t])
//   c7 (v, x = d, c = 0) ##1 (1, c = c + f(x))[*2] ##0 (d == c / 2)
//                                                         match at t+2 iff V[t] && D[t+2] == f(D[t])
// The number of times f, g and lt are called is not defined by the LRM and is not checked.
module svsvalocalcall2;
  localparam int NT = 200;
  logic clk = 0;
  logic [3:0] d = 0;
  logic v = 0, w = 0;
  logic [15:0] lfsr = 16'hACE1;
  int hits [8][NT];
  longint xsum [8];
  int errors = 0;

  function automatic int f(int a); return (a + 3) % 16; endfunction
  function automatic int g(int a, int b); return a ^ b; endfunction
  function automatic bit lt(int a, int b); return a < b; endfunction

  function automatic void hit(int i, int x = 0);
    int k = $time / 10;
    if (k >= 0 && k < NT) begin
      hits[i][k]++;
      xsum[i] += x;
    end
  endfunction
  task automatic note(int i, int x); hit(i, x); endtask
  function automatic void vnote(int i, int x); hit(i, x); endfunction

  always #5 clk = ~clk;
  always @(negedge clk) begin
    lfsr <= {lfsr[14:0], lfsr[15] ^ lfsr[13] ^ lfsr[12] ^ lfsr[10]};
    v <= lfsr[0];
    w <= lfsr[1] | lfsr[2];
    d <= lfsr[9:6];
  end

  int sv[NT], sw[NT], sd[NT];
  int ntick = 0;
  always @(posedge clk) begin
    if (ntick < NT) begin sv[ntick] = v; sw[ntick] = w; sd[ntick] = d; end
    ntick++;
  end

  sequence c1; int x; (1, x = d) ##1 (d == f(x)); endsequence
  sequence c2; int x; (1, x = d) ##1 (1, x = g(x, d)) ##1 (d == x); endsequence
  sequence c3; int x; (v, x = d) ##1 (1, note(3, x)); endsequence
  sequence c4; int x, y; (1, x = d) ##1 (1, y = f(x)) ##1 lt(g(y, x), d); endsequence
  sequence c5; int x; (v, x = d) ##1 (w, vnote(5, x + 1)); endsequence
  property a6; int x; (v, x = d) |=> (d != f(x)); endproperty
  sequence c7; int x, c; (v, x = d, c = 0) ##1 (1, c = c + f(x))[*2] ##0 (d == c / 2); endsequence

  C1: cover property (@(posedge clk) c1) hit(1);
  C2: cover property (@(posedge clk) c2) hit(2);
  C3: cover property (@(posedge clk) c3);
  C4: cover property (@(posedge clk) c4) hit(4);
  C5: cover property (@(posedge clk) c5);
  A6: assert property (@(posedge clk) a6) else hit(6);
  C7: cover property (@(posedge clk) c7) hit(7);

  // ---------------- reference model ----------------
  int en [8][NT];
  longint ex [8];
  function automatic void exp_hit(int i, int k, int x = 0);
    if (k < NT) begin en[i][k]++; ex[i] += x; end
  endfunction
  task automatic model;
    for (int i = 0; i < 8; i++) begin
      ex[i] = 0;
      for (int k = 0; k < NT; k++) en[i][k] = 0;
    end
    for (int t = 0; t + 1 < NT; t++) begin
      if (sd[t + 1] == f(sd[t])) exp_hit(1, t + 1);
      if (t + 2 < NT && sd[t + 2] == (sd[t] ^ sd[t + 1])) exp_hit(2, t + 2);
      if (sv[t]) exp_hit(3, t + 1, sd[t]);
      if (t + 2 < NT && (f(sd[t]) ^ sd[t]) < sd[t + 2]) exp_hit(4, t + 2);
      if (sv[t] && sw[t + 1]) exp_hit(5, t + 1, sd[t] + 1);
      if (sv[t] && sd[t + 1] == f(sd[t])) exp_hit(6, t + 1);
      if (t + 2 < NT && sv[t] && sd[t + 2] == f(sd[t])) exp_hit(7, t + 2);
    end
  endtask

  initial begin
    #(10 * NT);
    model;
    if (ntick < NT) begin $error("only %0d ticks sampled", ntick); errors++; end
    for (int i = 1; i < 8; i++) begin
      automatic int nh = 0, ne = 0;
      for (int k = 0; k < NT; k++) begin
        nh += hits[i][k];
        ne += en[i][k];
        if (hits[i][k] != en[i][k]) begin
          $error("id %0d tick %0d: %0d actions, expected %0d", i, k, hits[i][k], en[i][k]); errors++;
        end
      end
      if (xsum[i] != ex[i]) begin
        $error("id %0d: argument sum %0d, expected %0d", i, xsum[i], ex[i]); errors++;
      end
      if (ne == 0) begin $error("id %0d: stimulus never exercises it", i); errors++; end
    end
    if (errors == 0) $display("PASSED");
    $finish;
  end
endmodule
