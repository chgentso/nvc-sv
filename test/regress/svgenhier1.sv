// Expected values: IEEE 1800-2017 27.4 (loop generate: block g_l[i] with genvar i), 27.5
// (conditional generate), 23.6 (hierarchical names into generate blocks), 10.3 (continuous assign).
// g_l[i].t = 7*i, g_l[i].v = i, g_c.w = 5, r = g_l[2].t + 1000 = 1014.
module svgenhier1;
  int r;
  for (genvar i = 0; i < 3; i++) begin : g_l
    int t = i * 7;
    logic [3:0] v = i;
  end
  if (1) begin : g_c
    int w = 5;
  end
  assign r = g_l[2].t + 1000;
  int bad = 0;
  initial begin
    #1;
    if (r != 1014) begin $error("r=%0d", r); bad++; end
    if (g_l[0].t != 0 || g_l[1].t != 7 || g_l[2].t != 14) begin $error("t values"); bad++; end
    if (g_l[0].v !== 4'd0 || g_l[1].v !== 4'd1 || g_l[2].v !== 4'd2) begin $error("v values"); bad++; end
    if (g_c.w != 5) begin $error("w=%0d", g_c.w); bad++; end
    g_l[0].t = 99;
    #1;
    if (g_l[0].t != 99) begin $error("g_l[0].t=%0d", g_l[0].t); bad++; end
    if (r != 1014) begin $error("r changed to %0d", r); bad++; end
    if (bad == 0) $display("PASSED"); else $display("FAILED");
  end
endmodule
