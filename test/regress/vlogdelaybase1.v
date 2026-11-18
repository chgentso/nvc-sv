// A delay is not the size of a following unsized based literal
`timescale 1ns/1ns
`define M 1
module vlogdelaybase1;
  reg q, r, s, t;
  reg [3:0] v, u;
  initial begin
    q = 1; r = 0; s = 1; t = 1; v = 0; u = 0;
    q <= #`M 'b0;
    r <= #`M 1'b1;
    s <= #1 'b0;
    v <= #`M 'h5;
    u <= #1 4 'hc;      // size separated from the base is still sized
    #0 if (q !== 1 || r !== 0 || v !== 0) begin
      $display("FAIL early"); $finish;
    end
    #2;
    if (q !== 0 || r !== 1 || s !== 0 || v !== 5 || u !== 12)
      $display("FAIL %b %b %b %h %h", q, r, s, v, u);
    else
      $display("PASSED");
  end
endmodule
