// bind with a wildcard (.*) port connection and with implicit (.name) connections (self-checking).
// Expected values: IEEE 1800-2017 23.11 (bind), 23.3.2.4 (.* connects each port of the bound instance to the
// same-named signal visible in the target scope; .name connects to the signal of that name), 9.4.2.
// Reference model: clk rises at 5,15,25,35 (7 toggles from 0).  data=3,en=1 from t=1; data=7 from t=11; en=0
// from t=21; en=1 from t=31.  cov_if records an edge with en set: (5,3), (15,7), (35,7); chk records done
// (= en through the assign in dut) at every edge: 1,1,0,1.
`timescale 1ns/1ns
interface cov_if(input logic clk, input logic [3:0] data, input logic en);
  int n = 0;
  longint t [0:3];
  logic [3:0] dv [0:3];
  always @(posedge clk) if (en) begin t[n] = $time; dv[n] = data; n++; end
endinterface
module dut(input logic clk, input logic [3:0] data, input logic en, output logic done);
  assign done = en;
endmodule
module chk(input logic clk, input logic done);
  int n = 0;
  logic dn [0:3];
  longint tn [0:3];
  always @(posedge clk) begin dn[n] = done; tn[n] = $time; n++; end
endmodule
bind dut cov_if u_cov(.*);
bind dut chk u_chk(.clk, .done);
module svbindwild1;
  logic clk = 0; logic [3:0] data = 0; logic en = 0; wire done;
  int errs = 0;
  dut d(.*);
  initial repeat (7) #5 clk = ~clk;
  initial begin
    #1 data = 3; en = 1;
    #10 data = 7;
    #10 en = 0;
    #10 en = 1;
    #9;
    if (d.u_cov.n != 3) begin errs++; $error("cov_if saw %0d enabled edges, expected 3", d.u_cov.n); end
    else begin
      if (d.u_cov.t[0] != 5 || d.u_cov.dv[0] !== 4'd3) begin errs++; $error("cov edge 0"); end
      if (d.u_cov.t[1] != 15 || d.u_cov.dv[1] !== 4'd7) begin errs++; $error("cov edge 1"); end
      if (d.u_cov.t[2] != 35 || d.u_cov.dv[2] !== 4'd7) begin errs++; $error("cov edge 2"); end
    end
    if (d.u_chk.n != 4) begin errs++; $error("chk saw %0d edges, expected 4", d.u_chk.n); end
    else begin
      if (d.u_chk.tn[0] != 5 || d.u_chk.tn[1] != 15 || d.u_chk.tn[2] != 25 || d.u_chk.tn[3] != 35) begin errs++; $error("chk times"); end
      if (d.u_chk.dn[0] !== 1'b1 || d.u_chk.dn[1] !== 1'b1 || d.u_chk.dn[2] !== 1'b0 || d.u_chk.dn[3] !== 1'b1) begin errs++; $error("chk done values"); end
    end
    if (errs == 0) $display("PASSED");
  end
endmodule
