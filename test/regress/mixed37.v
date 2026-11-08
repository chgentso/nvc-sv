// Parameters set from generic map actuals that call user functions
module mixed37_sub #(parameter a = 0, parameter b = 0, parameter c = 0)
   (output [7:0] o);
   assign o = a * 16 + b + c;
endmodule
