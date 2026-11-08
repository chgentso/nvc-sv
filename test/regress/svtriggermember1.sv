`timescale 1ns/1ns
// Expected values: IEEE 1800-2017 9.4.2 (event control on a member of a packed struct port), 7.2.1,
// 9.2.2.2 (always_comb), 11.4.5 (===), 11.4.6 (^)
typedef struct packed { logic ping_p; logic ping_n; } rx_t;
module dut(input rx_t rx, input logic pq, output logic req, output logic [1:0] both);
  int nevt;
  time evt_t;
  logic evt_v;
  always_comb req = (rx.ping_p ^ pq) === 1'b1;
  initial begin #1; forever begin @(rx.ping_n); nevt++; evt_t = $time; evt_v = rx.ping_n; end end
  always_comb both = {rx.ping_p, rx.ping_n};
endmodule
module svtriggermember1;
  rx_t rx = '0;
  logic pq = 0, req;
  logic [1:0] both;
  int errs;
  dut d(.rx(rx), .pq(pq), .req(req), .both(both));
  initial begin
    #1; if (req !== 1'b0 || both !== 2'b00) begin $error("t1 %b %b", req, both); errs++; end
    rx.ping_p = 1; #1; if (req !== 1'b1 || both !== 2'b10) begin $error("t2 %b %b", req, both); errs++; end
    pq = 1; #1; if (req !== 1'b0 || both !== 2'b10) begin $error("t3 %b %b", req, both); errs++; end
    rx.ping_n = 1; #1; if (req !== 1'b0 || both !== 2'b11) begin $error("t4 %b %b", req, both); errs++; end
    if (d.nevt !== 1 || d.evt_t !== 3 || d.evt_v !== 1'b1) begin $error("ping_n events: n=%0d t=%0d v=%b", d.nevt, d.evt_t, d.evt_v); errs++; end
    if (errs == 0) $display("PASSED");
  end
endmodule
