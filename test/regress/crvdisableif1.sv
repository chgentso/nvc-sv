// disable soft under a condition on solver variables
class C;
  rand bit c;
  rand bit [3:0] x;
  rand bit [3:0] y;
  constraint k { x < 2; y < 2; soft x == 1; soft y == 1; }
  constraint d1 { if (c) disable soft x; }
  constraint d2 { c -> disable soft y; }
endclass

module crvdisableif1;
  initial begin
    C o = new;
    int c0 = 0, c1 = 0, xmiss = 0, ymiss = 0;
    for (int i = 0; i < 300; i++) begin
      if (!o.randomize()) $fatal(1, "randomize failed");
      if (o.c) begin
        c1++;
        if (o.x != 1) xmiss++;
        if (o.y != 1) ymiss++;
      end
      else begin
        c0++;
        if (o.x != 1 || o.y != 1) $fatal(1, "soft lost with c == 0: %0d %0d", o.x, o.y);
      end
    end
    if (c0 < 20 || c1 < 50) $fatal(1, "c is not random: %0d %0d", c0, c1);
    if (xmiss == 0 || ymiss == 0) $fatal(1, "soft kept with c == 1");
    $display("PASSED");
  end
endmodule
