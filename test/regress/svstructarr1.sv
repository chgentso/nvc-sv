// Expected values: IEEE 1800-2017 7.2.1 (packed structures are vectors; member and element selects),
// 7.4.1 (packed arrays of structs), 6.9/10.3.2 (continuous assignment to struct members / elements),
// 23.2.2 (ports), 25.9 (virtual interfaces), 20.6.2 ($bits), 11.4.9 (reduction xor), 9.4.3 (wait)
package p;
  typedef struct packed { logic alert_p; logic alert_n; } tx_t;
  typedef struct packed { logic ack_p; logic ack_n; } rx_t;
  typedef struct packed { logic [3:0] a; logic [3:0] b; } s_t;
  typedef s_t [2:0] svec_t;
endpackage

interface i_if;
  p::rx_t alert_rx;
  p::tx_t [1:0] arr;
endinterface

module m(input p::svec_t pol, output p::tx_t [1:0] alert_tx_o, output logic par);
  int errs = 0;
  assign alert_tx_o[0].alert_p = 1'b1;
  assign alert_tx_o[0].alert_n = 1'b0;
  assign alert_tx_o[1] = 2'b01;
  wire x = alert_tx_o[0].alert_p;
  assign par = ^pol;
  initial begin
    #1;
    // v[1] = 24, v[2].b = 0, 3 elements of 8 bits
    if (alert_tx_o[0].alert_p !== 1'b1 || x !== 1'b1 || $bits(pol) != 24 || pol[1] !== 8'h24 || pol[2].b !== 4'h0) begin
      $error("m: %b %b %0d %h %h", alert_tx_o[0].alert_p, x, $bits(pol), pol[1], pol[2].b); errs++;
    end
  end
endmodule

module svstructarr1;
  p::tx_t [1:0] t;
  p::svec_t v;
  logic par;
  m u(.pol(v), .alert_tx_o(t), .par(par));
  i_if i();
  virtual i_if vif = i;
  int errs = 0;
  initial begin
    v[0] = 8'h11; v[1].a = 4'h2; v[1].b = 4'h4; v[2] = '{a: 4'h8, b: 4'h0};
    i.alert_rx = 2'b10;
    i.arr[1].alert_p = 1'b1;
    #2;
    if (vif.alert_rx.ack_p !== 1'b1 || i.arr[1].alert_p !== 1'b1 || t !== 4'b0110 || par !== 1'b1) begin
      $error("top: %b %b %b %b", vif.alert_rx.ack_p, i.arr[1].alert_p, t, par); errs++;
    end
    if (v !== 24'h802411 || $bits(v) != 24 || !(v == 24'h802411)) begin
      $error("v=%h bits=%0d", v, $bits(v)); errs++;
    end
    wait (vif.alert_rx.ack_p == 1'b1);
    if (errs + u.errs == 0) $display("PASSED");
  end
endmodule
