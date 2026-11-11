package p;
  typedef enum {A, B} e_t;
  class base;
    int x;
    function void f(int a); x = a; endfunction
  endclass
endpackage

interface bus_if(input logic clk);
  logic [7:0] data;
  modport m(input data);
endinterface

module leaf #(parameter W = 4) (input logic clk, output logic [W-1:0] q);
  reg [W-1:0] cnt;
  wire w;
  localparam K = 2;
  function automatic int g(int v);
    int t;
    t = v + K;
    return t;
  endfunction
  always @(posedge clk) begin
    cnt <= cnt + 1;
    q <= g(cnt);
  end
endmodule

module top;
  import p::*;
  logic clk;
  logic [3:0] o;
  bus_if bi(clk);
  leaf #(.W(4)) u (.clk(clk), .q(o));
endmodule
