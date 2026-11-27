// IEEE 1800-2017 12.4.2.1 & 12.5.3.1: unique/unique0/priority violation
// reports are deferred to the end of the time step and flushed if the
// process re-executes in the same time step.

`timescale 1ns/1ns

module sv_unique_defer1_sub (
  input  logic a,
  input  logic b,
  output logic [1:0] y
);
  always_comb begin
    y = 2'd0;
    unique case (1'b1)
      a: y = 2'd1;
      b: y = 2'd2;
    endcase
  end
endmodule

module sv_unique_defer1;
  logic sel_a = 1'b1;
  logic sel_b = 1'b0;
  logic [1:0] y_comb, y_star;
  int runs_star = 0;

  // Fused SV island always_comb under default, normal process under --rtl-islands=off
  sv_unique_defer1_sub u_sub (.a(sel_a), .b(sel_b), .y(y_comb));

  // Normal Verilog always @* (not fused because of runs_star)
  always @* begin
    runs_star = runs_star + 1;
    y_star = 2'd0;
    unique if (sel_a)
      y_star = 2'd1;
    else if (sel_b)
      y_star = 2'd2;
  end

  initial begin
    // Step 1 (t=10ns): transient glitch {sel_a,sel_b} = 2'b11 then #0 -> 2'b01
    // Both always_comb and always @* re-execute in the same time step, so
    // the queued violation reports must be flushed (zero warnings at t=10ns).
    #10;
    sel_b = 1'b1;
    #0;
    sel_a = 1'b0;
    #10;
    $display("after_glitch y_comb=%0d y_star=%0d", y_comb, y_star);

    // Step 2 (t=20ns): persistent no-match {0,0} -> both emit at end of t=20ns
    sel_b = 1'b0;
    #10;
    $display("after_nomatch y_comb=%0d y_star=%0d", y_comb, y_star);

    // Step 3 (t=30ns): valid {1,0}, then at t=40ns persistent multi-match {1,1}
    sel_a = 1'b1;
    #10;
    sel_b = 1'b1;
    #10;
    $display("PASSED");
    $finish;
  end
endmodule
