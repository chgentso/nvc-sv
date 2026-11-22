interface ai(input logic clk);
  int n;
  always @(posedge clk) n++;
endinterface

module leaf(input logic clk_i);
endmodule

module pd_main(input logic clk_i);
  leaf u_leaf(.clk_i(clk_i));
endmodule

module vlog_hier_inst_array;
  logic clk = 0;
  always #5 clk = ~clk;
  pd_main pd(.clk_i(clk));
  // Hierarchical reference in the connection of an instance array
  ai ai_i[2](.clk(vlog_hier_inst_array.pd.u_leaf.clk_i));
  initial begin
    #52;
    if (ai_i[0].n == 5 && ai_i[1].n == 5) $display("PASSED");
    else $display("FAILED %0d %0d", ai_i[0].n, ai_i[1].n);
    $finish;
  end
endmodule
