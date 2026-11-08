// Procedural continuous assignment, force and release on hierarchical interface members
// Expected values: IEEE 1800-2017 10.6.1 (assign/deassign: a variable keeps its value after deassign),
// 10.6.2 (force/release: a released variable with an active procedural assign returns to it; a released net
// returns to its driven value, here undriven = z; force of a net with an expression follows the expression)
`timescale 1ns/1ns
interface bus_if;
  logic [3:0] d;
  wire  [3:0] w;
endinterface

module svpcahier1;
  bus_if m();
  bus_if s();
  logic [3:0] a;
  int errs = 0;
  initial begin
    assign s.d = m.d;
    assign a = m.d + 1;
  end
  initial begin
    m.d = 1;
    #1; if (s.d !== 4'd1 || a !== 4'd2) begin $error("1 s=%0d a=%0d", s.d, a); errs++; end
    m.d = 5;
    #1; if (s.d !== 4'd5 || a !== 4'd6) begin $error("2 s=%0d a=%0d", s.d, a); errs++; end
    force s.d = 9;
    m.d = 6;
    #1; if (s.d !== 4'd9 || a !== 4'd7) begin $error("3 s=%0d a=%0d", s.d, a); errs++; end
    release s.d;
    #1; if (s.d !== 4'd6 || a !== 4'd7) begin $error("4 s=%0d a=%0d", s.d, a); errs++; end
    deassign s.d;
    m.d = 7;
    #1; if (s.d !== 4'd6 || a !== 4'd8) begin $error("5 s=%0d a=%0d", s.d, a); errs++; end

    force s.w = m.d;
    m.d = 4'b0011;
    #1; if (s.w !== 4'b0011) begin $error("7 w=%b", s.w); errs++; end
    release s.w;
    #1; if (s.w !== 4'bzzzz) begin $error("8 w=%b", s.w); errs++; end
    if (errs == 0) $display("PASSED");
  end
endmodule
