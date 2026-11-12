interface x_if;
  typedef logic [3:0] rq_t;
endinterface

module top;
  x_if if0 ();
  localparam type p0_t = if0.rq_t;
  localparam type p1_t = if0.rq_t;
endmodule
