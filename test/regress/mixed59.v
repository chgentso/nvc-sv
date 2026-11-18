// Real generics of a VHDL component bound to Verilog parameters
module mixed59_v #(parameter real P = 1.5, parameter realtime T = 2.5,
                 parameter Q = 1.0, parameter integer I = 3,
                 parameter real R = 0.0);
  initial begin
    #1;
    $display("P=%f T=%f Q=%f I=%0d R=%f", P, T, Q, I, R);
    if (P != 2.25 || T != 4.5 || Q != 7.5 || I != 4 || R != 6.0)
      $fatal(1, "wrong values");
  end
endmodule
