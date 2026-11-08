// Expected values: IEEE 1800-2017 7.2.1 (packed structs: first member is most significant),
// 7.4.1 (packed arrays: '{...} assigns the first element to the leftmost index), 10.9.2 (struct patterns,
// default), 6.24.1 (cast to a packed struct converts as an integral value), 6.20 (localparam)
package pk;
  typedef struct packed {
    logic [31:0] base;
    logic [31:0] limit;
    logic [3:0]  policy_sel;
    logic        enable;
  } range_t;
endpackage
module svlparampat1;
  localparam int W = 32;
  localparam pk::range_t [0:0] R = '{
    '{
      base:  {W{1'b0}},
      limit: {W{1'b1}},
      policy_sel: pk::range_t'(4'd3),
      enable: 1'b1
    }
  };
  localparam pk::range_t [1:0] R2 = '{ '{base: 32'd1, limit: 32'd2, policy_sel: 4'd5, enable: 1'b0}, '{default: '1} };
  int errs = 0;
  initial begin
    if ($bits(R) != 69 || $bits(R2) != 138) begin errs++; $error("bits"); end
    if (R !== {32'h0, 32'hffffffff, 4'd3, 1'b1}) begin errs++; $error("R=%h", R); end
    if (R2 !== {{32'd1, 32'd2, 4'd5, 1'b0}, {69{1'b1}}}) begin errs++; $error("R2=%h", R2); end
    if (R[0].limit !== 32'hffffffff || R[0].policy_sel !== 4'd3) begin errs++; $error("R[0]"); end
    if (R2[1].policy_sel !== 4'd5) begin errs++; $error("R2[1].policy_sel"); end
    if (R2[1].base !== 32'd1 || R2[1].limit !== 32'd2 || R2[1].enable !== 1'b0) begin errs++; $error("R2[1]"); end
    if (R2[0] !== {69{1'b1}}) begin errs++; $error("R2[0]"); end
    if (R[0].limit != 4294967295) begin errs++; $error("limit decimal"); end
    if (errs == 0) $display("PASSED");
  end
endmodule
