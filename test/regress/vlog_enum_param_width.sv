// The names of an enumeration whose base type width is a parameter must
// have the values of the instance
module dut #(parameter int W = 5, parameter int V = 8) (output logic [31:0] o);
  typedef enum logic [W-1:0] { A = 'h0, B = 'h1, C = 'h11 } e_t;
  typedef enum logic [V-1:0] { X = 8'hf0, Y } f_t;
  localparam int N = $clog2(V + 3);
  typedef enum logic [N-1:0] { P, Q = N'(V - 4), R } g_t;
  e_t q;
  f_t r;
  g_t s;
  always_comb begin
    q = C;
    r = Y;
    s = R;
    o = {q == C ? 8'd1 : 8'd0, 8'(B), 8'(C), 8'(r)};
    if (int'(Q) != V - 4 || int'(R) != V - 3 || s != R) o = 0;
  end
endmodule

module vlog_enum_param_width;
  logic [31:0] o5, o6;
  dut u5(o5);
  dut #(.W(6)) u6(o6);
  initial begin
    #1;
    if (o5 !== 32'h010111f1 || o6 !== 32'h010111f1) begin
      $display("FAILED %h %h", o5, o6);
      $finish;
    end
    $display("PASSED");
  end
endmodule
