module svpackedsign1;
  typedef logic signed [2:0] s3_t;
  typedef s3_t [1:0] s3x2_t;
  typedef logic signed [1:0][2:0] s2x3_t;

  s3x2_t [1:0] nn;
  s3_t [1:0][1:0] n2d;
  s2x3_t [1:0] nu;
  logic signed [1:0][1:0][2:0] un;

  logic [11:0] r12;
  logic [5:0]  r6;
  logic [2:0]  r3;

  initial begin
    nn  = 12'b100000_000000;
    n2d = 12'b100000_000000;
    nu  = 12'b100000_000000;
    un  = 12'b100000_000000;

    r12 = nn >>> 1;      if (r12 !== 12'b010000_000000) $fatal(1, "nn whole");
    r6  = nn[1] >>> 1;   if (r6  !== 6'b010000)         $fatal(1, "nn[1]");
    r3  = nn[1][1] >>> 1;if (r3  !== 3'b110)            $fatal(1, "nn[1][1]");

    r12 = n2d >>> 1;      if (r12 !== 12'b010000_000000) $fatal(1, "n2d whole");
    r6  = n2d[1] >>> 1;   if (r6  !== 6'b010000)         $fatal(1, "n2d[1]");
    r3  = n2d[1][1] >>> 1;if (r3  !== 3'b110)            $fatal(1, "n2d[1][1]");

    r12 = nu >>> 1;      if (r12 !== 12'b010000_000000) $fatal(1, "nu whole");
    r6  = nu[1] >>> 1;   if (r6  !== 6'b110000)         $fatal(1, "nu[1]");
    r3  = nu[1][1] >>> 1;if (r3  !== 3'b010)            $fatal(1, "nu[1][1]");

    r12 = un >>> 1;      if (r12 !== 12'b110000_000000) $fatal(1, "un whole");
    r6  = un[1] >>> 1;   if (r6  !== 6'b010000)         $fatal(1, "un[1]");
    r3  = un[1][1] >>> 1;if (r3  !== 3'b010)            $fatal(1, "un[1][1]");

    if ($dimensions(nn) !== 3) $fatal(1, "dims nn");
    if ($size(nn, 1) !== 2 || $size(nn, 2) !== 2 || $size(nn, 3) !== 3)
      $fatal(1, "size nn");
    if ($bits(nn) !== 12 || $bits(n2d) !== 12 || $bits(nu) !== 12)
      $fatal(1, "bits");

    $display("PASSED");
    $finish;
  end
endmodule
