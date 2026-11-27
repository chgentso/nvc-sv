// cmos and rcmos switches: an nmos and a pmos switch with a shared input
// and output (IEEE 1364-2005 7.8)
module vlog_cmos1;
  reg d, n, p;
  wire y1, y2, y3;
  wire [1:0] y4;

  cmos c1 (y1, d, n, p);
  rcmos (y2, d, n, p);
  cmos c3 (y3, d, n, p), c4 (y4[0], d, n, p);

  initial begin
    d = 1; n = 1; p = 0;     // Both on
    #5 $display("%b %b %b %b", y1, y2, y3, y4[0]);
    n = 0; p = 1;            // Both off
    #5 $display("%b %b %b %b", y1, y2, y3, y4[0]);
    n = 1;                   // nmos on
    d = 0;
    #5 $display("%b %b %b %b", y1, y2, y3, y4[0]);
    n = 0; p = 0;            // pmos on
    d = 1;
    #3 $display("%b %b %b %b", y1, y2, y3, y4[0]);
  end
endmodule
