module child #(
  parameter int N = 4,
  parameter type rule_t = logic
) (
  input  rule_t        def_i,
  input  logic [7:0]   val_i,
  output logic [N-1:0][7:0] out_o
);
  always_comb begin
    out_o = '0;
    out_o[def_i.idx] = val_i;
  end
endmodule

module vlog_typeparam_idx1;
  typedef struct packed {
    logic [1:0] idx;
    logic [7:0] tag;
  } my_rule_t;

  my_rule_t r;
  logic [3:0][7:0] out;

  child #(.N(4), .rule_t(my_rule_t)) u_dut (
    .def_i(r),
    .val_i(8'h5a),
    .out_o(out)
  );

  initial begin
    r = '{idx: 2'd2, tag: 8'h00};
    #1;
    if (out[2] !== 8'h5a || out[0] !== 8'h00) begin
      $display("FAILED: out=%h", out);
      $finish;
    end
    $display("PASSED");
  end
endmodule
