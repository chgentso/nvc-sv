// Regression test for super-island cluster dirty_count accounting during
// rt_island_eval_sv_reset().  When an island in a super-island cluster
// re-evaluates during model_reset() with isl->dirty == false (e.g. pass >= 1
// after a non-direct input settles), decrementing cl->dirty_count
// unconditionally stole another dirty island's count (such as a cluster member
// with a comb-written async reset that is ineligible for sv_reset), causing
// island_eval_cluster() to exit early and permanently orphan a downstream
// island with isl->dirty == true.

module reset_dirty_src (
  input  logic clk_i,
  input  logic rst_ni,
  input  logic rst_en_i,
  input  logic d_i,
  input  logic en_i,
  output logic link_o,
  output logic rst_o,
  output logic q_o
);
  // Comb-written async reset (also an output port so it stays fused in the
  // island with RT_ISIG_COMB_WRITTEN) makes this island ineligible for
  // sv_reset so its isl->dirty remains true across model_reset() passes.
  assign rst_o  = rst_ni & rst_en_i;
  assign link_o = d_i & en_i;

  always_ff @(posedge clk_i or negedge rst_o) begin
    if (!rst_o)
      q_o <= 1'b0;
    else
      q_o <= d_i & en_i;
  end
endmodule

module reset_dirty_head (
  input  logic       clk_i,
  input  logic       from_src_i,
  input  wire  [1:0] ext_i,
  output logic       fb_o,
  output logic [1:0] ext_q_o
);
  assign fb_o = from_src_i;
  always_ff @(posedge clk_i)
    ext_q_o <= ext_i;
endmodule

module reset_dirty_intr (
  input  logic clk_i,
  input  logic rst_ni,
  input  logic ev_i,
  input  logic state_q_i,
  input  logic en_i,
  output logic state_d_o,
  output logic intr_o
);
  logic status;
  generate
    if (1) begin : gen_ev
      logic new_ev;
      assign new_ev    = ev_i;
      assign state_d_o = new_ev | state_q_i;
      assign status    = state_q_i;
    end
    if (1) begin : gen_flop
      always_ff @(posedge clk_i or negedge rst_ni) begin
        if (!rst_ni)
          intr_o <= 1'b0;
        else
          intr_o <= status & en_i;
      end
    end
  endgenerate
endmodule

module reset_dirty_ext (
  input  logic       a_i,
  output wire  [1:0] b_o
);
  assign b_o = (a_i === 1'b1) ? 2'b00 : 2'b01;
endmodule

module vlog_island_reset_dirty1;
  logic clk = 1'b0;
  logic rst_n = 1'b1;
  logic rst_en = 1'b1;
  logic a_const = 1'b0;
  logic d_in;
  logic en;
  logic state_q;
  logic src_link;
  logic src_rst, src_q;
  logic intr_d, intr_o;
  logic head_fb;
  logic [1:0] head_ext_q;
  wire  [1:0] ext_b;

  // Unfused observer on ext_b prevents direct-linking u_ext into u_head.
  // ext_i is an FF-only input on u_head (!RT_ISIG_COMB_READ), so when u_ext
  // settles ext_b in pass 0, u_head->dirty stays 0 while pass 1's memcmp
  // triggers need_eval = true on u_head.
  always @(ext_b) if (ext_b === 2'bzz) $display("z");

  // u_head and u_intr sit in the same super-island cluster as u_src via
  // src_link (no external readers).
  reset_dirty_head u_head (
    .clk_i      (clk),
    .from_src_i (src_link),
    .ext_i      (ext_b),
    .fb_o       (head_fb),
    .ext_q_o    (head_ext_q)
  );
  reset_dirty_intr u_intr (
    .clk_i     (clk),
    .rst_ni    (rst_n),
    .ev_i      (src_link),
    .state_q_i (state_q),
    .en_i      (en),
    .state_d_o (intr_d),
    .intr_o    (intr_o)
  );
  reset_dirty_src u_src (
    .clk_i    (clk),
    .rst_ni   (rst_n),
    .rst_en_i (rst_en),
    .d_i      (d_in),
    .en_i     (en),
    .link_o   (src_link),
    .rst_o    (src_rst),
    .q_o      (src_q)
  );
  reset_dirty_ext u_ext (
    .a_i (a_const),
    .b_o (ext_b)
  );

  task automatic tick;
    #5 clk = 1'b1;
    #5 clk = 1'b0;
  endtask

  initial begin
    #1;
    if (ext_b !== 2'b01) begin
      $display("FAIL0: ext_b=%b expected 01", ext_b);
      $fatal(1);
    end
    rst_n = 1'b0;
    d_in = 1'b1;
    en = 1'b0;
    state_q = 1'b0;
    tick();
    rst_n = 1'b1;
    tick();

    en = 1'b1;
    #1;
    if (head_fb !== 1'b1) begin
      $display("FAIL1: head_fb=%b expected 1", head_fb);
      $fatal(1);
    end
    tick();
    if (src_q !== 1'b1) begin
      $display("FAIL2: src_q=%b expected 1", src_q);
      $fatal(1);
    end
    if (intr_o !== 1'b0) begin
      $display("FAIL3: intr_o=%b expected 0", intr_o);
      $fatal(1);
    end

    d_in = 1'b0;
    state_q = 1'b1;
    #1;
    if (head_fb !== 1'b0) begin
      $display("FAIL4: head_fb=%b expected 0", head_fb);
      $fatal(1);
    end
    if (intr_d !== 1'b1) begin
      $display("FAIL5: intr_d=%b expected 1", intr_d);
      $fatal(1);
    end
    tick();
    if (src_q !== 1'b0) begin
      $display("FAIL6: src_q=%b expected 0", src_q);
      $fatal(1);
    end
    if (intr_o !== 1'b1) begin
      $display("FAIL7: intr_o=%b expected 1", intr_o);
      $fatal(1);
    end

    $display("PASSED");
    $finish;
  end
endmodule
