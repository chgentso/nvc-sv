// A hierarchical name of a generate block which is a sibling of the block
// containing the force, in a module instantiated by VHDL
module svgenref1_sub #(parameter EN = 1) (input clk, input [3:0] d, output [3:0] q);
  if (EN) begin : gen_rd
    reg [3:0] doutb_reg = 4'h5;
    always @(posedge clk) doutb_reg <= d;
    assign q = doutb_reg;
  end
  if (EN) begin : gen_coll
    always @(posedge clk) begin
      if (d == 4'h3) force gen_rd.doutb_reg = 4'hx;
      else release gen_rd.doutb_reg;
    end
  end
endmodule
