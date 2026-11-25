module sub #(
  parameter type ar_chan_t = logic,
  parameter type full_req_t = logic
) (
  input  ar_chan_t   ar_i,
  input  full_req_t  req_i,
  output logic [3:0] id_o,
  output logic [3:0] id2_o
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
    id2_o       = req_i.ar.id;
  end
endmodule

module vlog_typeparam_struct_field1;
  typedef struct packed {
    logic [3:0] id;
    logic [7:0] addr;
  } ar_a_t;

  typedef struct packed {
    ar_a_t ar;
    logic  valid;
  } full_a_t;

  typedef struct packed {
    logic [15:0] addr;
    logic [3:0]  id;
  } ar_b_t;

  typedef struct packed {
    logic  valid;
    ar_b_t ar;
  } full_b_t;

  ar_a_t   a;
  full_a_t fa;
  ar_b_t   b;
  full_b_t fb;
  logic [3:0] ida, id2a, idb, id2b;

  sub #(.ar_chan_t(ar_a_t), .full_req_t(full_a_t)) u_a (.ar_i(a), .req_i(fa), .id_o(ida), .id2_o(id2a));
  sub #(.ar_chan_t(ar_b_t), .full_req_t(full_b_t)) u_b (.ar_i(b), .req_i(fb), .id_o(idb), .id2_o(id2b));

  initial begin
    a  = '{id: 4'h9, addr: 8'h12};
    fa = '{ar: '{id: 4'ha, addr: 8'h34}, valid: 1'b1};
    b  = '{addr: 16'h3456, id: 4'h5};
    fb = '{valid: 1'b1, ar: '{addr: 16'h789a, id: 4'h6}};
    #1;
    if (ida !== 4'h9 || id2a !== 4'ha || idb !== 4'h5 || id2b !== 4'h6) begin
      $display("FAILED: ida=%h id2a=%h idb=%h id2b=%h", ida, id2a, idb, id2b);
      $finish;
    end
    $display("PASSED");
  end
endmodule
