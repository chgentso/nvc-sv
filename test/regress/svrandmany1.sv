// A class may have more than 64 rand properties; all of them are
// randomised and constrained (IEEE 1800-2017 18.4).
class svrandmany1_c;
  rand bit [7:0] p0;
  rand bit [7:0] p1;
  rand bit [7:0] p2;
  rand bit [7:0] p3;
  rand bit [7:0] p4;
  rand bit [7:0] p5;
  rand bit [7:0] p6;
  rand bit [7:0] p7;
  rand bit [7:0] p8;
  rand bit [7:0] p9;
  rand bit [7:0] p10;
  rand bit [7:0] p11;
  rand bit [7:0] p12;
  rand bit [7:0] p13;
  rand bit [7:0] p14;
  rand bit [7:0] p15;
  rand bit [7:0] p16;
  rand bit [7:0] p17;
  rand bit [7:0] p18;
  rand bit [7:0] p19;
  rand bit [7:0] p20;
  rand bit [7:0] p21;
  rand bit [7:0] p22;
  rand bit [7:0] p23;
  rand bit [7:0] p24;
  rand bit [7:0] p25;
  rand bit [7:0] p26;
  rand bit [7:0] p27;
  rand bit [7:0] p28;
  rand bit [7:0] p29;
  rand bit [7:0] p30;
  rand bit [7:0] p31;
  rand bit [7:0] p32;
  rand bit [7:0] p33;
  rand bit [7:0] p34;
  rand bit [7:0] p35;
  rand bit [7:0] p36;
  rand bit [7:0] p37;
  rand bit [7:0] p38;
  rand bit [7:0] p39;
  rand bit [7:0] p40;
  rand bit [7:0] p41;
  rand bit [7:0] p42;
  rand bit [7:0] p43;
  rand bit [7:0] p44;
  rand bit [7:0] p45;
  rand bit [7:0] p46;
  rand bit [7:0] p47;
  rand bit [7:0] p48;
  rand bit [7:0] p49;
  rand bit [7:0] p50;
  rand bit [7:0] p51;
  rand bit [7:0] p52;
  rand bit [7:0] p53;
  rand bit [7:0] p54;
  rand bit [7:0] p55;
  rand bit [7:0] p56;
  rand bit [7:0] p57;
  rand bit [7:0] p58;
  rand bit [7:0] p59;
  rand bit [7:0] p60;
  rand bit [7:0] p61;
  rand bit [7:0] p62;
  rand bit [7:0] p63;
  rand bit [7:0] p64;
  rand bit [7:0] p65;
  rand bit [7:0] p66;
  rand bit [7:0] p67;
  rand bit [7:0] p68;
  rand bit [7:0] p69;
  constraint c_all {
    p0 <= 200; p33 <= 200; p64 <= 200; p65 <= 200; p69 <= 200;
  }
endclass

module svrandmany1;
  initial begin
    automatic svrandmany1_c o = new;
    automatic int errs = 0;
    automatic bit [7:0] seen64 = 0;
    for (int it = 0; it < 20; it++) begin
      if (!o.randomize()) $fatal(1, "randomize failed");
      if (o.p0 > 8'd200 || o.p33 > 8'd200 || o.p64 > 8'd200 || o.p65 > 8'd200 || o.p69 > 8'd200)
        errs++;
      seen64 |= o.p64 ^ o.p69;
    end
    if (seen64 == 0) begin errs++; $error("property 64 or 69 never changed"); end
    if (errs == 0) $display("PASSED");
    else $fatal(1, "%0d errors", errs);
  end
endmodule
