// Specify items which are parsed but not modelled: skew and nochange
// timing checks, $width without a threshold, showcancelled and pulse
// style declarations (IEEE 1364-2005 14.6, 15.3, 15.4), and edge control
// specifiers in timing check events (15.5.1)
`timescale 1ns/1ns

module specify9;
  reg d = 0, clk = 0, en = 0;

  sub i(.d(d), .clk(clk), .en(en), .y());

  initial begin
    #5 clk = 1;            // Rising edge at 5
    #1 d = 1;              // Hold violation for edge [01]
    #4 clk = 0;            // Falling edge at 10
    #1 d = 0;              // Violation only for edge [10, x0]
    #5;
    if (i.nh !== 1'b0)
      $display("FAILED: rising notifier %b", i.nh);
    else if (i.nf !== 1'b1)
      $display("FAILED: falling notifier %b", i.nf);
    else
      $display("PASSED");
  end
endmodule

module sub(input d, input clk, input en, output y);
  reg nh, nf = 0, n1, n2, n3, n4, n5;

  buf (y, d);

  specify
    (d => y) = (2, 3);
    showcancelled y;
    noshowcancelled y;
    pulsestyle_onevent y;
    pulsestyle_ondetect y;
    $hold(edge [01, 0x] clk, d, 2, nh);
    $hold(edge [10, x0] clk, d, 2, nf);
    $skew(posedge clk, posedge en, 3, n1);
    $timeskew(posedge clk, posedge en, 2, n2, 1, 0);
    $fullskew(posedge clk, negedge en, 2, 2, n3,,);
    $nochange(posedge clk, d, 0, 0, n4);
    $width(negedge clk, 4);
    $width(posedge clk, 4, 1, n5);
  endspecify
endmodule
