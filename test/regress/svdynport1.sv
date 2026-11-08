// A variable declared with a typedef of an unpacked array connected to a
// port, which must follow later changes to the elements

module svdynport1_ch(input signed [7:0] c [3:0], output [7:0] o);
  assign o = c[3];
endmodule

module svdynport1;
  typedef logic signed [7:0] arr_t [3:0];
  arr_t a;
  wire [7:0] o;
  svdynport1_ch u (.c(a), .o(o));
  initial begin
    a[0] = 1; a[1] = 2; a[2] = 3; a[3] = 4;
    #1;
    if (o !== 8'd4) $display("FAILED 1: %0d", o);
    a[3] = 9;
    #1;
    if (o !== 8'd9) $display("FAILED 2: %0d", o);
    else $display("PASSED");
  end
endmodule
