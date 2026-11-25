module adapter #(
  parameter type w_chan_t = logic,
  parameter type resp_t   = logic
) (
  input  logic [7:0] data_i,
  input  logic [1:0] id_i,
  input  logic       sel_i,
  output w_chan_t    w_o,
  output resp_t      resp_o
);
  assign w_o = w_chan_t'{
    data:    data_i,
    default: '0
  };

  assign resp_o.b = sel_i ? '{
    id:      id_i,
    default: '0
  } : '{
    id:      ~id_i,
    default: '0
  };
endmodule

module vlog_typeparam_pat1;
  typedef struct packed {
    logic [7:0] data;
    logic       strb;
  } w_a_t;

  typedef struct packed {
    logic [1:0] id;
    logic [1:0] resp;
  } b_a_t;

  typedef struct packed {
    b_a_t b;
    logic valid;
  } resp_a_t;

  typedef struct packed {
    logic [3:0] extra;
    logic [7:0] data;
  } w_b_t;

  typedef struct packed {
    logic [3:0] pad;
    logic [1:0] id;
  } b_b_t;

  typedef struct packed {
    b_b_t b;
  } resp_b_t;

  w_a_t    wa;
  resp_a_t ra;
  w_b_t    wb;
  resp_b_t rb;

  adapter #(.w_chan_t(w_a_t), .resp_t(resp_a_t)) u_a (
    .data_i(8'ha5), .id_i(2'b10), .sel_i(1'b1), .w_o(wa), .resp_o(ra)
  );
  adapter #(.w_chan_t(w_b_t), .resp_t(resp_b_t)) u_b (
    .data_i(8'h3c), .id_i(2'b01), .sel_i(1'b0), .w_o(wb), .resp_o(rb)
  );

  initial begin
    #1;
    if (wa.data !== 8'ha5 || wa.strb !== 1'b0 || ra.b.id !== 2'b10 || ra.b.resp !== 2'b00 ||
        wb.data !== 8'h3c || wb.extra !== 4'h0 || rb.b.id !== 2'b10 || rb.b.pad !== 4'h0) begin
      $display("FAILED: wa=%h ra=%h wb=%h rb=%h", wa, ra, wb, rb);
      $finish;
    end
    $display("PASSED");
  end
endmodule
