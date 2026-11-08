// $printtimescale with and without a hierarchical name argument
// IEEE 1800-2017 20.4.2: with no argument the time unit and precision of the calling
// module are shown, otherwise those of the module named by the argument, in the format
// "Time scale of (name) is unit / precision"; 23.8: a hierarchical name is searched for
// upwards from the calling scope; 27.6: a generate block has the time unit of its module.
// 20.4.1: $timeunit and $timeprecision take the same argument and give the power of ten.
`timescale 1ms/1us
module svprinttimescale2;
  initial begin
    $printtimescale;
    $printtimescale(u);           // Declared below
    $printtimescale(u.v);
    $printtimescale(arr[1]);
    $printtimescale(g.w);
    $printtimescale(g);
    $printtimescale($root.svprinttimescale2.u);
    if ($timeunit(u) != -11 || $timeprecision(u) != -15 || $timeunit(g.w) != -6
        || $timeprecision(u.v) != -7 || $timeunit != -3)
      $display("FAILED");
    #2 $display("PASSED");
  end
  svprinttimescale2_sub u();
  svprinttimescale2_leaf arr[0:1]();
  if (1) begin : g svprinttimescale2_leaf w(); end
endmodule
`timescale 10ps/1fs
module svprinttimescale2_sub;
  svprinttimescale2_leaf v();
  initial #1 $printtimescale(v);   // Declared before
endmodule
`timescale 1us/100ns
module svprinttimescale2_leaf;
endmodule
