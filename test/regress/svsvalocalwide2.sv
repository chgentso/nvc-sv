// Expected values: IEEE 1800-2017 16.8 (local variables captured by match items,
// 100-bit values), 16.9.3 (sampled values), 16.12.6, 11.4.2-11.4.3 and 11.4.8
// (wide arithmetic, division, modulus, power; modulo 2**100), 11.4.1 (+=, *=, /=
// assignment operators inside sequence match items), 16.14.
// Reference model: d_{k+1} = 3*d_k + 2**64 + 1 (mod 2**100), d_0 = 2**64+3,
// k_n = 7+n, computed in 128-bit arithmetic.  Edges k=0..9 (time 5+10k); every
// |-> property (antecedent constant true) completes at its attempt edge: 10
// passes; pa (|-> ##1) completes one edge later: 9 passes; pf2 fails for
// each edge where d/3 != d/2, which the model evaluates independently.
module svsvalocalwide2;
  logic clk = 0;
  logic [99:0] d = 100'h1_0000_0000_0000_0003;
  logic signed [99:0] sd = -100'sd12345678901234567890123;
  logic [99:0] k = 100'd7;
  int errs, pf2_c;
  logic [99:0] D[10], DS[10], DK[10];
  int mk;
  always @(posedge clk) begin D[mk] = d; DS[mk] = sd; DK[mk] = k; mk++; end
  int pm_c, pd_c, ps_c, pp_c, pa_c, pc_c;
  always #5 clk = ~clk;
  always @(posedge clk) begin
    d <= d * 100'd3 + 100'h1_0000_0000_0000_0001;
    sd <= sd - 100'sd98765432109876543210;
    k <= k + 1;
  end
  property pm;
    logic [99:0] x;
    @(posedge clk) (1, x = d) |-> (x * 100'd7 == d * 100'd7 && x * k == d * k);
  endproperty
  property pd;
    logic [99:0] x, y;
    @(posedge clk) (1, x = d, y = k) |-> (x / y == d / k && x % y == d % k && x / 100'd1000 == d / 100'd1000);
  endproperty
  property ps;
    logic signed [99:0] x, y;
    @(posedge clk) (1, x = sd, y = $signed(k)) |-> (x / y == sd / $signed(k) && x % y == sd % $signed(k) && x * y == sd * $signed(k));
  endproperty
  property pp;
    logic [99:0] x;
    @(posedge clk) (1, x = k) |-> (x ** 5 == k ** 5 && x ** 0 == 1);
  endproperty
  property pa;
    logic [99:0] x;
    @(posedge clk) (1, x = d) |-> ##1 (x * 100'd3 + 100'h1_0000_0000_0000_0001 == d);
  endproperty
  property pc;
    logic [99:0] x;
    @(posedge clk) (1, x = d, x *= 2, x /= 2) |-> (x == d);
  endproperty
  am: assert property (pm) pm_c++; else begin errs++; $error("pm FAIL"); end
  ad: assert property (pd) pd_c++; else begin errs++; $error("pd FAIL"); end
  as: assert property (ps) ps_c++; else begin errs++; $error("ps FAIL"); end
  ap: assert property (pp) pp_c++; else begin errs++; $error("pp FAIL"); end
  aa: assert property (pa) pa_c++; else begin errs++; $error("pa FAIL"); end
  ac: assert property (pc) pc_c++; else begin errs++; $error("pc FAIL"); end
  property pf2; logic [99:0] x; @(posedge clk) (1, x = d) |-> (x / 100'd3 == d / 100'd2); endproperty
  af2: assert property (pf2) else pf2_c++;
  initial begin
    automatic logic [127:0] m = 128'h1_0000_0000_0000_0003, ms = -128'sd12345678901234567890123;
    automatic logic [127:0] mask = (128'h1 << 100) - 1;
    automatic int expf = 0;
    #100;
    for (int i = 0; i < 10; i++) begin
      if (D[i] !== m[99:0]) begin $error("d at edge %0d is %h, model %h", i, D[i], m[99:0]); errs++; end
      if (DS[i] !== ms[99:0]) begin $error("sd at edge %0d is %h, model %h", i, DS[i], ms[99:0]); errs++; end
      if (DK[i] != 7 + i) begin $error("k at edge %0d is %0d", i, DK[i]); errs++; end
      if (m / 3 != m / 2) expf++;
      m = (m * 3 + 128'h1_0000_0000_0000_0001) & mask;
      ms = (ms - 128'd98765432109876543210) & mask;
    end
    if (d !== m[99:0] || k !== 100'd17) begin $error("end state d=%h k=%0d, model %h 17", d, k, m[99:0]); errs++; end
    if (pm_c != 10 || pd_c != 10 || ps_c != 10 || pp_c != 10 || pc_c != 10) begin
      $error("pass counts %0d %0d %0d %0d %0d, expected 10 each", pm_c, pd_c, ps_c, pp_c, pc_c); errs++; end
    if (pa_c != 9) begin $error("pa passes %0d, expected 9", pa_c); errs++; end
    if (pf2_c != expf) begin $error("pf2 failures %0d, expected %0d", pf2_c, expf); errs++; end
    if (expf == 0) begin $error("model sanity: pf2 should fail"); errs++; end
    if (errs == 0) $display("PASSED");
    $finish;
  end
endmodule
