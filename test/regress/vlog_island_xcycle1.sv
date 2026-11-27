// Combinational paths between two SV island instances that form a cycle at
// the island level but not at the signal level, plus a true combinational
// loop, a latch-style loop and a loop through one slice of a bus.

// Independent cross paths: A drives y from x, B drives x from cnt and
// reads y back into z
module xc_a (input  logic [3:0] x_i,
             input  logic [3:0] k_i,
             output logic [3:0] y_o,
             output logic [3:0] w_o);
  assign y_o = ~x_i;
  assign w_o = k_i + 4'd1;
endmodule

module xc_b (input  logic [3:0] cnt_i,
             input  logic [3:0] y_i,
             input  logic [3:0] w_i,
             output logic [3:0] x_o,
             output logic [3:0] k_o,
             output logic [3:0] z_o);
  assign x_o = cnt_i;
  assign k_o = cnt_i ^ 4'b1010;
  assign z_o = y_i ^ w_i;
endmodule

// Latch-style loop through two islands: q holds when en is low
module xl_mux (input  logic en_i,
               input  logic d_i,
               input  logic fb_i,
               output logic q_o);
  assign q_o = en_i ? d_i : fb_i;
endmodule

module xl_buf (input  logic a_i,
               output logic y_o,
               output logic mon_o);
  assign y_o = a_i;
  assign mon_o = a_i;
endmodule

// Loop through a sliced bus: bit 0 goes out, bit 1 comes back
module xs_lo (input  logic [1:0] bus_i,
              input  logic       s_i,
              output logic [1:0] bus_o);
  assign bus_o = {bus_i[1], s_i};
endmodule

module xs_hi (input  logic [1:0] bus_i,
              output logic [1:0] bus_o,
              output logic [1:0] mon_o);
  assign bus_o = {~bus_i[0], bus_i[0]};
  assign mon_o = {~bus_i[0], bus_i[0]};
endmodule

module vlog_island_xcycle1;
  logic [3:0] cnt = 4'd0;
  logic [3:0] x, y, k, w, z;

  xc_a ua (.x_i(x), .k_i(k), .y_o(y), .w_o(w));
  xc_b ub (.cnt_i(cnt), .y_i(y), .w_i(w), .x_o(x), .k_o(k), .z_o(z));

  logic en = 1'b1, d = 1'b0, q, fb, qm;

  xl_mux um (.en_i(en), .d_i(d), .fb_i(fb), .q_o(q));
  xl_buf uf (.a_i(q), .y_o(fb), .mon_o(qm));

  logic       s = 1'b0;
  logic [1:0] lo2hi, hi2lo, mon2;

  xs_lo ul (.bus_i(hi2lo), .s_i(s), .bus_o(lo2hi));
  xs_hi uh (.bus_i(lo2hi), .bus_o(hi2lo), .mon_o(mon2));

  initial begin
    for (int i = 0; i < 6; i++) begin
      cnt = cnt + 4'd3;
      #1 if (z !== (~cnt ^ ((cnt ^ 4'b1010) + 4'd1)))
        $fatal(1, "z mismatch: cnt=%h z=%h", cnt, z);
    end
    #1 d = 1'b1;
    #1 en = 1'b0;
    #1 d = 1'b0;
    #1 if (qm !== 1'b1) $fatal(1, "latch lost value: q=%b", qm);
    #1 en = 1'b1;
    #1 if (qm !== 1'b0) $fatal(1, "latch not transparent: q=%b", qm);
    #1 s = 1'b1;
    #1 if (mon2 !== 2'b01) $fatal(1, "slice loop: %b", mon2);
    #1 s = 1'b0;
    #1 if (mon2 !== 2'b10) $fatal(1, "slice loop: %b", mon2);
    $display("PASSED");
    $finish;
  end
endmodule
