// Expected values: IEEE 1800-2017 16.14.1 (fail action; a fail count of 0 means every attempt
// passed or was vacuous), 16.14.3 (cover property counts non-vacuous successes),
// 16.12 (disable iff with an expression that is not 0, i.e. also 1 or x: the attempt is
// disabled while reset is not released), 16.9.3 ($past), 16.5.1 ($rose of a boolean
// compare of an enum), 27.5 (assertion in a generate block), 9.2.2.4 (always_ff with
// asynchronous reset). A reference model of the sampled values at each posedge computes
// the number of enabled ticks at which the antecedent holds and whether the consequent held.
`timescale 1ns/1ns
module svsvagenvar1;
  int nfail, nhit;
  logic clk = 0, rst_n = 0, upd_i = 0;
  always #5 clk = ~clk;
  typedef enum logic {SelSwReq, SelHwReq} req_sel_e;
  logic upd;
  assign upd = upd_i;
  if (1) begin : g
    logic lat_q, lat_d;
    req_sel_e id_q;
    logic idle_q;
    assign lat_d = upd & idle_q;
    always_ff @(posedge clk or negedge rst_n) begin
      if (!rst_n) idle_q <= 1'b1; else if (lat_d) idle_q <= 1'b0; else if (lat_q) idle_q <= 1'b1;
    end
    always_ff @(posedge clk or negedge rst_n) begin
      if (!rst_n) lat_q <= 1'b0; else lat_q <= lat_d;
    end
    always_ff @(posedge clk or negedge rst_n) begin
      if (!rst_n) id_q <= SelSwReq;
      else if (lat_d) id_q <= SelHwReq;
      else if (lat_q) id_q <= SelHwReq;
    end
    A: assert property (@(posedge clk) disable iff ((!rst_n) !== '0)
       $rose(id_q == SelHwReq) |-> $past(upd, 1) || $past(lat_q, 1))
       else begin nfail++; end
    C: cover property (@(posedge clk) disable iff ((!rst_n) !== '0)
       $rose(id_q == SelHwReq) |-> $past(upd, 1) || $past(lat_q, 1)) nhit++;
  end
  // reference model on values sampled at posedge (the always block runs before the NBA updates)
  bit s_rst[$], s_hw[$], s_upd[$], s_lat[$];
  always @(posedge clk) begin
    s_rst <= {s_rst, rst_n === 1'b1};
    s_hw  <= {s_hw, g.id_q === SelHwReq};
    s_upd <= {s_upd, upd === 1'b1};
    s_lat <= {s_lat, g.lat_q === 1'b1};
  end
  initial begin
    #23 rst_n = 1;
    repeat (3) @(negedge clk);
    upd_i = 1; @(negedge clk); upd_i = 0;
    repeat (8) @(negedge clk);
    #1 check();
    $finish;
  end
  task automatic check();
    int n = s_rst.size(), antecedents = 0, efail = 0, ehit = 0, errs = 0;
    for (int k = 1; k < n; k++) begin
      bit rose = s_hw[k] && !s_hw[k-1];
      if (rose && s_rst[k]) begin
        antecedents++;
        if (s_upd[k-1] || s_lat[k-1]) ehit++; else efail++;
      end
    end
    if (antecedents != 1) begin $error("model: expected exactly one enabled antecedent, got %0d", antecedents); errs++; end
    if (nfail != efail) begin $error("fail actions %0d expected %0d", nfail, efail); errs++; end
    if (nhit != ehit) begin $error("non-vacuous successes %0d expected %0d", nhit, ehit); errs++; end
    if (errs == 0) $display("PASSED");
  endtask
endmodule
