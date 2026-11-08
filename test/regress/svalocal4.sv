`timescale 1ns/1ns
// Local variables of narrow, signed and 4-state types assigned from expressions with
// casts, signedness conversions, $signed / $unsigned, multiplication, shifts and
// $past, in cover properties over an LFSR stimulus.
// Expected values: IEEE 1800-2017 16.10 (local variables), 16.14.3 (cover property of a
// sequence: one action per attempt, at its first match), 6.24.1 (casts: n'(e) truncates
// to n bits, signed'(e) / unsigned'(e) change the signedness only), 20.5 ($signed,
// $unsigned), 11.8 (expression types and sizes: a byte a in d[2:0] == 3'(a + 1) is
// evaluated in 32 bits, 8'd5 makes d + 8'd5 an 8-bit unsigned sum), 16.9.3 ($past).
//
// Reference model: d and v sampled at each of the 100 posedges (tick k at time 5 + 10 k)
// are recorded and the first match tick of each attempt is computed (sgn(d) is d read as
// an 8-bit two's complement number):
//   k1 byte a = d; at t+1: d[2:0] == (a + 1)[2:0]
//   k2 int a = sgn(d) - 100; at t+1: a < 0; at t+2: d[3:0] == a[3:0]
//   k3 shortint a = sgn(d) * -3; at t+1: a[7:0] == (d + 8'd5) mod 256, or a read as unsigned
//      16 bits > 60000 (true iff a is negative, i.e. sgn(d) > 0)
//   k4 logic [3:0] a = d[5:2] (d >> 2 truncated to 4 bits); at t+1: a == d[5:2]; at t+2:
//      8'(a) == $past(d[5:2], 2), which is d[5:2] at t, always equal to a
// each with v at t.
module svalocal4;
  localparam int NT = 100;
  logic clk = 0;
  logic [7:0] d = 0;
  logic v = 0, w = 0;
  logic [15:0] lfsr = 16'hACE1;
  int hits [0:31][NT];
  int errors = 0;
  task automatic hit(int i);
    int k = $time / 10;
    if (k >= 0 && k < NT) hits[i][k]++;
  endtask
  always #5 clk = ~clk;
  always @(negedge clk) begin
    lfsr <= {lfsr[14:0], lfsr[15] ^ lfsr[13] ^ lfsr[12] ^ lfsr[10]};
    v <= lfsr[0]; w <= lfsr[1] | lfsr[2]; d <= lfsr[10:3];
  end
  int sv[NT], sd[NT];
  int ntick = 0;
  always @(posedge clk) begin
    if (ntick < NT) begin sv[ntick] = v; sd[ntick] = d; end
    ntick++;
  end

  sequence k1;
    byte a;
    (v, a = d) ##1 (d[2:0] == 3'(a + 1));
  endsequence
  P16: cover property (@(posedge clk) k1) hit(27);
  sequence k2;
    int a;
    (v, a = int'(signed'(d) - 100)) ##1 (a < 0) ##1 (d[3:0] == 4'(a));
  endsequence
  P17: cover property (@(posedge clk) k2) hit(28);
  sequence k3;
    shortint a;
    (v, a = $signed(d) * -3) ##1 (unsigned'(a[7:0]) == d + 8'd5 || $unsigned(a) > 16'd60000);
  endsequence
  P18: cover property (@(posedge clk) k3) hit(29);
  sequence k4;
    logic [3:0] a;
    (v, a = 4'(d >> 2)) ##1 (a == d[5:2]) ##1 (8'(a) == $past(d[5:2], 2));
  endsequence
  P19: cover property (@(posedge clk) k4) hit(30);

  // ---------------- reference model ----------------
  function automatic int V(int k); return (k >= 0 && k < NT) ? sv[k] : 0; endfunction
  function automatic int D(int k); return (k >= 0 && k < NT) ? sd[k] : 0; endfunction
  function automatic int sgn(int x); return x >= 128 ? x - 256 : x; endfunction
  // tick of the match of the attempt at t for k1..k4 (m = 0..3), or -1
  function automatic int match_end(int m, int t);
    int a;
    if (!V(t)) return -1;
    case (m)
      0: if (t + 1 < NT && (D(t + 1) & 7) == ((D(t) + 1) & 7)) return t + 1;
      1: begin
        a = sgn(D(t)) - 100;
        if (t + 2 < NT && a < 0 && (D(t + 2) & 15) == (a & 15)) return t + 2;
      end
      2: begin
        a = sgn(D(t)) * -3;
        if (t + 1 < NT && ((a & 255) == ((D(t + 1) + 5) & 255) || a < 0)) return t + 1;
      end
      3: if (t + 2 < NT && ((D(t) >> 2) & 15) == ((D(t + 1) >> 2) & 15)) return t + 2;
    endcase
    return -1;
  endfunction

  initial begin
    #1000;
    if (ntick < NT) begin $error("only %0d ticks sampled", ntick); errors++; end
    for (int m = 0; m < 4; m++) begin
      int ecov[NT];
      automatic int total = 0;
      for (int k = 0; k < NT; k++) ecov[k] = 0;
      for (int t = 0; t < NT; t++) begin
        automatic int e = match_end(m, t);
        if (e >= 0) begin ecov[e]++; total++; end
      end
      for (int k = 0; k < NT; k++)
        if (hits[27 + m][k] != ecov[k]) begin
          $error("hit(%0d) tick %0d (time %0d): %0d hits, expected %0d", 27 + m, k, 5 + 10 * k, hits[27 + m][k], ecov[k]); errors++; end
      if (total == 0) begin $error("k%0d never matches in this trace: the check is vacuous", m + 1); errors++; end
    end
    for (int i = 0; i < 32; i++)
      if (i < 27 || i > 30) for (int k = 0; k < NT; k++) if (hits[i][k] != 0) begin $error("hit(%0d) used unexpectedly", i); errors++; end
    if (errors == 0) $display("PASSED");
    $finish;
  end
endmodule
