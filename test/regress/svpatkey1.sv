module svpatkey1;
  localparam int K0 = 0;
  localparam int K2 = 2;
  localparam int P = 1;

  typedef enum int { E1 = 1, E3 = 3 } idx_e;

  localparam logic [31:0] LUT[4] = '{
    K0: 32'h1111_1111,
    K2: 32'h2222_2222,
    default: 32'h0
  };

  int arr[6] = '{K0: 10, P + 1: 20, K2 * 2: 40, default: 99};
  int earr[4];

  genvar g;
  for (g = 0; g < 2; g++) begin : gen_b
    logic [15:0] ga[4];
    always_comb ga = '{g: 16'habcd, default: 16'h0};
    initial begin
      #1;
      if (ga[g] !== 16'habcd) $fatal(1, "ga[%0d]=%h", g, ga[g]);
      if (ga[3] !== 16'h0) $fatal(1, "ga[3]=%h", ga[3]);
    end
  end

  initial begin
    earr = '{E1: 100, E3: 300, default: 5};
    #2;
    if (LUT[0] !== 32'h1111_1111) $fatal(1, "LUT[0]=%h", LUT[0]);
    if (LUT[1] !== 32'h0) $fatal(1, "LUT[1]=%h", LUT[1]);
    if (LUT[2] !== 32'h2222_2222) $fatal(1, "LUT[2]=%h", LUT[2]);
    if (arr[0] !== 10 || arr[2] !== 20 || arr[4] !== 40 || arr[5] !== 99)
      $fatal(1, "arr mismatch");
    if (earr[0] !== 5 || earr[1] !== 100 || earr[2] !== 5 || earr[3] !== 300)
      $fatal(1, "earr mismatch");
    $display("PASSED");
    $finish;
  end
endmodule
