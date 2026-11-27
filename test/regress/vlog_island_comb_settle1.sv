// Continuous-assignment SV islands driven by external continuous assignments
// or blocking updates must settle before active-region edge-triggered processes
// in the same delta cycle, matching non-island V_ASSIGN (reschedq) semantics.
module pass_buf (
  input  logic a_i,
  output logic y_o
);
  generate
    if (1) begin : gen_bypass
      assign y_o = a_i;
    end
  endgenerate
endmodule

module vlog_island_comb_settle1;
  logic clk = 1'b0;
  logic src = 1'b0;
  logic mid1, mid2;
  logic out_y;
  logic sampled = 1'b0;
  int   edges = 0;

  // Chain: external continuous assign -> SV island -> external continuous assign
  assign mid1 = src;

  pass_buf u_buf (
    .a_i (mid1),
    .y_o (mid2)
  );

  assign out_y = mid2;

  always @(posedge clk) begin
    sampled <= out_y;
    edges   <= edges + 1;
    if (edges == 0 && out_y !== 1'b1) begin
      $display("FAIL edge 0: out_y=%b expected 1 (mid1=%b mid2=%b)",
               out_y, mid1, mid2);
      $fatal(1);
    end
    if (edges == 1 && out_y !== 1'b0) begin
      $display("FAIL edge 1: out_y=%b expected 0 (mid1=%b mid2=%b)",
               out_y, mid1, mid2);
      $fatal(1);
    end
  end

  initial begin
    #1;
    // Blocking updates in same delta: in non-island mode, continuous assigns
    // settle in reschedq before always @(posedge clk) runs in the next delta.
    src = 1'b1;
    clk = 1'b1;
    #1;
    clk = 1'b0;
    #1;
    // Non-blocking updates in same delta: NBA commits src and clk together,
    // and continuous assigns settle before always @(posedge clk) runs.
    src <= 1'b0;
    clk <= 1'b1;
    #2;
    if (edges !== 2) begin
      $display("FAIL edges=%0d expected 2", edges);
      $fatal(1);
    end
    $display("PASSED");
    $finish;
  end
endmodule
