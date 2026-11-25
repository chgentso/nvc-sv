module checker #(
  parameter int N = 2,
  parameter type r_chan_t = logic
) (
  input  r_chan_t [N-1:0] arr_i,
  output logic    [N-1:0] last_o,
  output logic    [7:0]   byte0_o
);
  for (genvar i = 0; i < N; i++) begin : gen_c
    assign last_o[i] = arr_i[i].last;
  end
  assign byte0_o = arr_i[1].data[7:0];
endmodule

module vlog_typeparam_arr1;
  typedef struct packed {
    logic [7:0] data;
    logic       last;
  } chan8_t;

  typedef struct packed {
    logic [15:0] data;
    logic        last;
  } chan16_t;

  chan8_t  [1:0] a;
  chan16_t [1:0] b;
  logic [1:0] la, lb;
  logic [7:0] da, db;

  checker #(.N(2), .r_chan_t(chan8_t))  u_a (.arr_i(a), .last_o(la), .byte0_o(da));
  checker #(.N(2), .r_chan_t(chan16_t)) u_b (.arr_i(b), .last_o(lb), .byte0_o(db));

  initial begin
    a[0] = '{data: 8'h11, last: 1'b1};
    a[1] = '{data: 8'hab, last: 1'b0};
    b[0] = '{data: 16'h2233, last: 1'b0};
    b[1] = '{data: 16'h44cd, last: 1'b1};
    #1;
    if (la !== 2'b01 || da !== 8'hab || lb !== 2'b10 || db !== 8'hcd) begin
      $display("FAILED: la=%b da=%h lb=%b db=%h", la, da, lb, db);
      $finish;
    end
    $display("PASSED");
  end
endmodule
