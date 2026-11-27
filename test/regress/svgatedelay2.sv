// Gates with delays driving the same net: each outputs x until its first
// update (IEEE 1364-2005 7.14) and the net is resolved from all of them
module svgatedelay2;
  reg d = 1, en = 1, en_n = 0;
  wire y, c;

  bufif1 #2 (y, d, en);
  bufif0 #3 (y, d, en_n);
  cmos #1 (c, d, en, en_n);

  initial begin
    #0 $display("%0t %b %b", $time, y, c);
    #1 $display("%0t %b %b", $time, y, c);
    #1 $display("%0t %b %b", $time, y, c);
    #1 $display("%0t %b %b", $time, y, c);
    d = 0;
    #2 $display("%0t %b %b", $time, y, c);
    #1 $display("%0t %b %b", $time, y, c);
  end
endmodule
