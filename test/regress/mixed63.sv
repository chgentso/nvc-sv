module mixed63;
  typedef logic signed [15:0] s16_arr_t [0:3];
  s16_arr_t din;
  s16_arr_t dout;

  mixed63_sub dut (.din(din), .dout(dout));

  initial begin
    din[0] = -16'sd10;
    din[1] = 16'sd20;
    din[2] = -16'sd30;
    din[3] = 16'sd40;
    #2;
    if (dout[0] !== 16'sd41)  $fatal(1, "dout[0]=%0d exp=41", dout[0]);
    if (dout[1] !== -16'sd28) $fatal(1, "dout[1]=%0d exp=-28", dout[1]);
    if (dout[2] !== 16'sd23)  $fatal(1, "dout[2]=%0d exp=23", dout[2]);
    if (dout[3] !== -16'sd6)  $fatal(1, "dout[3]=%0d exp=-6", dout[3]);
    $display("PASSED");
    $finish;
  end
endmodule
