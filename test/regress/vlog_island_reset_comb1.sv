// Fused SV island always_comb blocks must not execute during model_reset()
// (before 0ms+0), so a unique case inside an always_comb block produces no
// spurious (init) warnings and matches --rtl-islands=off.

`timescale 1ns/1ns

module vlog_island_reset_comb1_pure (
  input  logic [1:0] sel,
  output logic [7:0] y
);
  always_comb begin
    y = 8'h00;
    unique case (sel)
      2'b01: y = 8'h11;
      2'b10: y = 8'h22;
    endcase
  end
endmodule

module vlog_island_reset_comb1_mix (
  input  logic [1:0] sel,
  input  logic [7:0] a,
  output logic [7:0] pass_o,
  output logic [7:0] y
);
  assign pass_o = a + 8'h01;

  always_comb begin
    y = 8'h00;
    unique case (sel)
      2'b01: y = pass_o;
      2'b10: y = pass_o + 8'h10;
    endcase
  end
endmodule

module vlog_island_reset_comb1;
  logic [1:0] sel;   // Uninitialized (X) during model_reset(); set at t=0
  logic [7:0] a = 8'h40;
  logic [7:0] y_pure, y_mix, pass_o;

  vlog_island_reset_comb1_pure u_pure (.sel(sel), .y(y_pure));
  vlog_island_reset_comb1_mix  u_mix  (.sel(sel), .a(a), .pass_o(pass_o), .y(y_mix));

  initial begin
    sel = 2'b01;
    #1;
    $display("t=%0t pass_o=%02x y_pure=%02x y_mix=%02x", $time, pass_o, y_pure, y_mix);
    sel = 2'b00;   // Real persistent violation at t=1ns (not init)
    #1;
    $display("t=%0t y_pure=%02x y_mix=%02x", $time, y_pure, y_mix);
    sel = 2'b10;
    #1;
    $display("t=%0t y_pure=%02x y_mix=%02x", $time, y_pure, y_mix);
    $display("PASSED");
    $finish;
  end
endmodule
