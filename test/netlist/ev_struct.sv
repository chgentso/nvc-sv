typedef struct packed { logic q; logic qe; } f_t;
typedef struct packed { f_t a; f_t b; } s_t;
module ev_struct;
  logic clk, clk2, d, y;
  s_t r;
  logic z, w;
  always_ff @(posedge clk) begin r.a.q <= d; r.a.qe <= d; end
  always_ff @(posedge clk2) r.b <= {d, d};
  assign z = r.a.qe & r.a.q;
  always_ff @(posedge clk) w <= z;
  assign y = w;
endmodule
