// Vectors of 65..257 bits holding x and z bits through variables, nets and bitwise operators.
// Expected values: IEEE 1800-2017 6.3 (4-state values), 11.4.8 (~x and ~z are x), 6.5/10.3
// (a net follows its continuous assignment, z is preserved), 20.9 ($countones).
module w #(parameter W = 70) (output int nerr);
  logic [W-1:0] a, b, c;
  wire [W-1:0] n = a;
  logic [W-1:0] r;
  logic [W-1:0] ea, ec, ones, sparse;
  int errors = 0;
  assign nerr = errors;
  initial begin
    for (int i = 0; i < W; i++) begin
      a[i] = (i % 7 == 0) ? 1'bx : (i % 5 == 0) ? 1'bz : (i % 3 == 0);
      // expected value of a and of ~a, bit by bit
      ea[i] = (i % 7 == 0) ? 1'bx : (i % 5 == 0) ? 1'bz : (i % 3 == 0);
      ec[i] = (i % 7 == 0) ? 1'bx : (i % 5 == 0) ? 1'bx : !(i % 3 == 0);
    end
    #1;
    b = a;
    c = ~a;
    r = n;
    if (a !== ea) begin $error("W=%0d a=%b", W, a); errors++; end
    if (b !== ea) begin $error("W=%0d b=%b", W, b); errors++; end
    if (c !== ec) begin $error("W=%0d c=%b", W, c); errors++; end
    if (r !== ea) begin $error("W=%0d r=%b", W, r); errors++; end
    // spot checks of individual bits
    if (a[0] !== 1'bx || a[5] !== 1'bz || a[3] !== 1'b1 || a[1] !== 1'b0 || a[W-1] !== ea[W-1]) begin
      $error("W=%0d bit spot check", W); errors++;
    end
    a = {W{1'b1}};
    #1;
    if (n !== {W{1'b1}}) begin $error("W=%0d n=%b exp all ones", W, n); errors++; end
    a = '0;
    a[W-1] = 1;
    a[W/2] = 1;
    a[0] = 1;
    #1;
    sparse = '0; sparse[W-1] = 1; sparse[W/2] = 1; sparse[0] = 1;
    if (n !== sparse) begin $error("W=%0d n=%b", W, n); errors++; end
    if ($countones(n) != 3) begin $error("W=%0d countones=%0d", W, $countones(n)); errors++; end
  end
endmodule

module svwidepack1;
  int e65, e70, e127, e128, e129, e200, e257;
  w #(65) w65(.nerr(e65));
  w #(70) w70(.nerr(e70));
  w #(127) w127(.nerr(e127));
  w #(128) w128(.nerr(e128));
  w #(129) w129(.nerr(e129));
  w #(200) w200(.nerr(e200));
  w #(257) w257(.nerr(e257));
  initial begin
    #5;
    if (e65 + e70 + e127 + e128 + e129 + e200 + e257 == 0) $display("PASSED");
  end
endmodule
