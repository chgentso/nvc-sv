module svstreammem1;
  typedef struct {
    logic [7:0] a;
    logic [7:0] b;
  } s_t;
  typedef s_t arr_t[2];

  localparam int W = $bits(arr_t);
  logic [W-1:0] src;
  arr_t aw_r, aw_l;

  always_comb {>>{aw_r}} = src;
  always_comb {<<8{aw_l}} = src;

  wire [W-1:0] rd_r = {>>{aw_r}};
  wire [W-1:0] rd_l = {<<8{aw_l}};

  initial begin
    src = 32'h1234_5678;
    #1;
    if (rd_r !== 32'h1234_5678) $fatal(1, "rd_r=%h", rd_r);
    if (rd_l !== 32'h1234_5678) $fatal(1, "rd_l=%h", rd_l);
    src = 32'haabb_ccdd;
    #1;
    if (rd_r !== 32'haabb_ccdd) $fatal(1, "rd_r2=%h", rd_r);
    if (rd_l !== 32'haabb_ccdd) $fatal(1, "rd_l2=%h", rd_l);
    $display("PASSED");
    $finish;
  end
endmodule
