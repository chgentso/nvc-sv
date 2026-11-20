module svenumsize1;
  typedef enum [2:0] { ONES = ~0 } three_t;
  three_t three = ONES;
  var logic [ONES:0] sized_from_enum;

  typedef enum logic [3:0] {
    ALL1 = '1,
    ZERO = {2'b0, 2'h0},
    ONE
  } four_t;

  initial begin
    if (ONES !== 3'b111) $fatal(1, "ONES=%0d", ONES);
    if (three !== 3'b111) $fatal(1, "three=%0d", three);
    if ($bits(sized_from_enum) !== 8) $fatal(1, "bits=%0d", $bits(sized_from_enum));
    if ($bits(three_t) !== 3) $fatal(1, "three_t bits=%0d", $bits(three_t));
    if (ALL1 !== 4'hf) $fatal(1, "ALL1=%0d", ALL1);
    if (ZERO !== 4'h0) $fatal(1, "ZERO=%0d", ZERO);
    if (ONE !== 4'h1) $fatal(1, "ONE=%0d", ONE);
    $display("PASSED");
    $finish;
  end
endmodule
