// Test implicitly-typed parameters initialized from struct, enum, and $sformatf

module sub #(parameter N = 123);
  parameter STR = $sformatf("id=%0d", N);
  typedef struct {
    int unsigned a;
    logic [32:0] b;
  } pair_t;
  typedef enum logic [1:0] { E0 = 2'd1, E1 = 2'd2 } kind_e;

  localparam pair_t P0 = '{a: 32'd7, b: 33'h1_0000_0005};
  localparam P1 = P0;
  localparam K1 = E1;

  initial begin
    if (STR != "id=123") $fatal(1, "STR mismatch: %s", STR);
    if (P1.a !== 32'd7 || P1.b !== 33'h1_0000_0005)
      $fatal(1, "P1 mismatch: %0d %h", P1.a, P1.b);
    if (K1 !== E1 || K1.name() != "E1")
      $fatal(1, "K1 mismatch: %s", K1.name());
    $display("PASSED");
    $finish;
  end
endmodule

module svparamimp1;
  sub u();
endmodule
