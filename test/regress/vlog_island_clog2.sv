// Dynamic $clog2 lowers to a VPI syscall (clog2_tf) and must not be absorbed
// into an SV RTL island (localparam $clog2 is folded before island analysis).
module dut #(
  parameter int W = 32
) (
  input  logic [W-1:0]         din,
  output logic [$clog2(W):0]   out_const,
  output logic [5:0]           out_pass,
  output logic [5:0]           out_dyn
);
  localparam int LOGW = $clog2(W);

  assign out_const = LOGW;
  assign out_pass  = din[5:0];
  assign out_dyn   = $clog2(din);
endmodule

module vlog_island_clog2;
  logic [31:0] din;
  logic [5:0]  out_const;
  logic [5:0]  out_pass;
  logic [5:0]  out_dyn;

  dut #(.W(32)) u_dut (
    .din       (din),
    .out_const (out_const),
    .out_pass  (out_pass),
    .out_dyn   (out_dyn)
  );

  initial begin
    din = 32'd1;
    #1;
    if (out_const !== 6'd5 || out_pass !== 6'd1) begin
      $display("FAIL const/pass: const=%0d pass=%0d", out_const, out_pass);
      $fatal(1);
    end
    if (out_dyn !== 6'd0) begin
      $display("FAIL dyn(1): got %0d expected 0", out_dyn);
      $fatal(1);
    end

    din = 32'd16;
    #1;
    if (out_dyn !== 6'd4 || out_pass !== 6'd16) begin
      $display("FAIL dyn(16): dyn=%0d pass=%0d", out_dyn, out_pass);
      $fatal(1);
    end

    din = 32'd17;
    #1;
    if (out_dyn !== 6'd5 || out_pass !== 6'd17) begin
      $display("FAIL dyn(17): dyn=%0d pass=%0d", out_dyn, out_pass);
      $fatal(1);
    end

    $display("PASSED");
    $finish;
  end
endmodule
