// A true combinational loop through two SV island instances oscillates
// once it is enabled and must stop at the delta cycle limit

module xo_inv (input  logic en_i,
               input  logic a_i,
               output logic y_o);
  assign y_o = en_i & ~a_i;
endmodule

module xo_buf (input  logic a_i,
               output logic y_o);
  assign y_o = a_i;
endmodule

module vlog_island_xcycle2;
  logic en = 1'b0, a, b;

  xo_inv ui (.en_i(en), .a_i(b), .y_o(a));
  xo_buf ub (.a_i(a), .y_o(b));

  initial begin
    #1 $display("enable");
    en = 1'b1;
    #1 $display("not reached");
    $finish;
  end
endmodule
