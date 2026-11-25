module sub #(
  parameter type ar_chan_t = logic
) (
  input  ar_chan_t   ar_i,
  output logic [3:0] id_o
);
  typedef struct packed {
    ar_chan_t ar;
    logic     valid;
  } req_t;

  req_t req_q;
  always_comb begin
    req_q.ar    = ar_i;
    req_q.valid = 1'b1;
    id_o        = req_q.ar.id;
  end
endmodule

module vlog_typeparam_struct_field1;
  typedef struct packed {
    logic [3:0] id;
    logic [7:0] addr;
  } ar_a_t;

  typedef struct packed {
    logic [15:0] addr;
    logic [3:0]  id;
  } ar_b_t;

  ar_a_t a;
  ar_b_t b;
  logic [3:0] ida, idb;

  sub #(.ar_chan_t(ar_a_t)) u_a (.ar_i(a), .id_o(ida));
  sub #(.ar_chan_t(ar_b_t)) u_b (.ar_i(b), .id_o(idb));

  initial begin
    a = '{id: 4'h9, addr: 8'h12};
    b = '{addr: 16'h3456, id: 4'h5};
    #1;
    if (ida !== 4'h9 || idb !== 4'h5) begin
      $display("FAILED: ida=%h idb=%h", ida, idb);
      $finish;
    end
    $display("PASSED");
  end
endmodule
