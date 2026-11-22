package svgenstruct1_pkg;
  parameter bit UseSep = 1'b0;
  typedef struct packed {
    logic en;
    logic data;
  } drv_t;
endpackage

module svgenstruct1;
  import svgenstruct1_pkg::*;
  drv_t drv;

  if (UseSep) begin : gen_sep
    assign drv.pp_en = 1'b1;
    assign drv.od_en = 1'b0;
  end else begin : gen_vod
    assign drv.en   = 1'b1;
    assign drv.data = 1'b1;
  end

  initial begin
    #1;
    if (drv.en !== 1'b1 || drv.data !== 1'b1)
      $fatal(1, "drv=%b exp=11", drv);
    $display("PASSED");
    $finish;
  end
endmodule
