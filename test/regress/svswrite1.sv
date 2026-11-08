// IEEE 1800-2017 21.3.3: $swrite, $swriteb, $swriteh and $swriteo write
// to a string like $write, $writeb, $writeh and $writeo, so an argument
// without a format specification takes the default radix of the suffix.
module svswrite1;
  string s;
  int err = 0;
  initial begin
    $swriteb(s, 4'b0010); if (s != "0010") err++;
    $swriteh(s, 8'hab, " ", 8'h1); if (s != "ab 01") err++;
    $swriteo(s, 6'o17); if (s != "17") err++;
    $swriteh(s, "%0d ", 12, 8'hc); if (s != "12 0c") err++;
    $swrite(s, 8'd5); if (s != "  5") err++;
    if (err == 0) $display("PASSED");
    else $display("FAILED %0d [%s]", err, s);
  end
endmodule
