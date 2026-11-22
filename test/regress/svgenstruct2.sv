package svgenstruct2_pkg;
  parameter bit UseSep = 1'b0;
  typedef struct packed {
    logic en;
    logic data;
  } drv_t;
endpackage

module svgenstruct2;
  import svgenstruct2_pkg::*;
  drv_t drv;
  logic y;

  if (UseSep) begin : gen_sep
    assign y = !drv.pp_en & !drv.od_en;
  end else begin : gen_nosep
    assign drv.en   = 1'b1;
    assign drv.data = 1'b0;
    assign y        = !drv.en | drv.data;
  end

  initial begin
    #1;
    if (y !== 1'b0 || drv.en !== 1'b1)
      $fatal(1, "y=%b drv=%b", y, drv);
    $display("PASSED");
    $finish;
  end
endmodule
