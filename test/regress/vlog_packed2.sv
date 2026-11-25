// Test packed SV signal sub-slice level triggers when multiple bits of the
// same packed vector are written within a single delta cycle (both inside one
// always_comb writing multiple slices and across sibling processes).
module vlog_packed2;
  localparam int N = 8;
  localparam int IdxW = 3;

  logic [N-1:0] req_i = '0;
  logic [N-1:0] gnt_o;
  logic         valid_o;
  logic         ready_i = 1'b1;

  logic [2**(IdxW+1)-2:0] req_tree;
  logic [2**(IdxW+1)-2:0] gnt_tree;

  for (genvar level = 0; level < IdxW + 1; level++) begin : gen_tree
    localparam int Base0 = (2**level) - 1;
    localparam int Base1 = (2**(level + 1)) - 1;
    for (genvar offset = 0; offset < 2**level; offset++) begin : gen_level
      localparam int Pa = Base0 + offset;
      localparam int C0 = Base1 + 2 * offset;
      localparam int C1 = Base1 + 2 * offset + 1;
      if (level == IdxW) begin : gen_leafs
        assign req_tree[Pa]  = req_i[offset];
        assign gnt_o[offset] = gnt_tree[Pa];
      end else begin : gen_nodes
        logic sel;
        always_comb begin : p_node
          sel          = ~req_tree[C0];
          req_tree[Pa] = req_tree[C0] | req_tree[C1];
          gnt_tree[C0] = gnt_tree[Pa] & ~sel;
          gnt_tree[C1] = gnt_tree[Pa] &  sel;
        end
      end
    end
  end

  assign valid_o     = req_tree[0];
  assign gnt_tree[0] = valid_o & ready_i;

  // Wide (>64-bit) packed vector with disjoint same-delta writers & watchers.
  logic [95:0] wide_v = '0;
  logic        w_low_seen, w_high_seen;
  logic        trig_wide = 1'b0;

  always_comb begin
    wide_v[1]  = trig_wide;
    wide_v[70] = trig_wide;
  end

  always_comb w_low_seen  = wide_v[1];
  always_comb w_high_seen = wide_v[70];

  // Two sibling processes writing different bits in the same delta.
  logic [7:0] sib_v = 8'hff;
  logic       sib_t = 1'b0;
  logic       sib_obs0, sib_obs1;

  always @(sib_t) sib_v[0] = sib_t;
  always @(sib_t) sib_v[3] = sib_t;

  always_comb sib_obs0 = sib_v[0];
  always_comb sib_obs1 = sib_v[3];

  initial begin
    #1;
    for (int k = 0; k < 64; k++) begin
      req_i = (k % 4 == 0) ? '0
            : ((8'h1 << (k % N)) | (8'h1 << ((k * 3 + 1) % N)));
      #1;
      if ((|gnt_o) && !valid_o)
        $fatal(1, "GntImpliesValid failed k=%0d req=%b gnt=%b valid=%b",
               k, req_i, gnt_o, valid_o);
      if ((|req_i) !== valid_o)
        $fatal(1, "valid mismatch k=%0d req=%b valid=%b", k, req_i, valid_o);
      if (valid_o && !$onehot(gnt_o))
        $fatal(1, "gnt not onehot k=%0d req=%b gnt=%b", k, req_i, gnt_o);
    end

    trig_wide = 1'b1;
    #1;
    if (w_low_seen !== 1'b1 || w_high_seen !== 1'b1)
      $fatal(1, "wide set failed low=%b high=%b", w_low_seen, w_high_seen);
    trig_wide = 1'b0;
    #1;
    if (w_low_seen !== 1'b0 || w_high_seen !== 1'b0)
      $fatal(1, "wide clear failed low=%b high=%b", w_low_seen, w_high_seen);

    sib_t = 1'b1;
    #1;
    if (sib_obs0 !== 1'b1 || sib_obs1 !== 1'b1)
      $fatal(1, "sib set failed obs0=%b obs1=%b", sib_obs0, sib_obs1);
    sib_t = 1'b0;
    #1;
    if (sib_obs0 !== 1'b0 || sib_obs1 !== 1'b0)
      $fatal(1, "sib clear failed obs0=%b obs1=%b", sib_obs0, sib_obs1);

    $display("PASSED");
    $finish;
  end
endmodule
