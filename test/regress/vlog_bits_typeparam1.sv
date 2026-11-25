module logger #(
  parameter type chan_t = logic
) (
  input chan_t ch_i,
  output int   id_w_o,
  output int   ch_w_o,
  output int   n_ids_o
);
  localparam int unsigned IdWidth = $bits(ch_i.id);
  localparam int unsigned ChWidth = $bits(chan_t);
  localparam int unsigned NoIds   = 2**IdWidth;

  logic [NoIds-1:0] seen;

  assign id_w_o  = IdWidth;
  assign ch_w_o  = ChWidth;
  assign n_ids_o = $size(seen);
endmodule

module vlog_bits_typeparam1;
  typedef struct packed {
    logic [2:0] id;
    logic [7:0] data;
  } chan_a_t;

  typedef struct packed {
    logic [4:0]  id;
    logic [15:0] data;
  } chan_b_t;

  chan_a_t a;
  chan_b_t b;
  int id_a, ch_a, n_a;
  int id_b, ch_b, n_b;

  logger #(.chan_t(chan_a_t)) u_a (.ch_i(a), .id_w_o(id_a), .ch_w_o(ch_a), .n_ids_o(n_a));
  logger #(.chan_t(chan_b_t)) u_b (.ch_i(b), .id_w_o(id_b), .ch_w_o(ch_b), .n_ids_o(n_b));

  initial begin
    #1;
    if (id_a !== 3 || ch_a !== 11 || n_a !== 8 ||
        id_b !== 5 || ch_b !== 21 || n_b !== 32) begin
      $display("FAILED: a=(%0d,%0d,%0d) b=(%0d,%0d,%0d)",
               id_a, ch_a, n_a, id_b, ch_b, n_b);
      $finish;
    end
    $display("PASSED");
  end
endmodule
