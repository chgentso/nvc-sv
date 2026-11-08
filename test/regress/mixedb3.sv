// A typedef of an unpacked array connected to an array port of a VHDL entity
module mixedb3;
  typedef logic signed [7:0] arr_t [3:0];
  arr_t a;
  wire [7:0] o;
  mixedb3_e u (.c(a), .o(o));
  initial begin
    a[0] = 1; a[1] = 2; a[2] = 3; a[3] = 40;
    #1;
    if (o === 8'd41) $display("PASSED");
    else $display("FAILED %0d", o);
  end
endmodule
