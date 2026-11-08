// Self-checking: indexed part-select (+: and -:) constraints.
// Expected values: IEEE 1800-2017 18.5 (constraints), 11.5.1 (indexed part-selects).
module svcrvpart1;
  class c;
    rand bit [31:0] d;
    rand bit [15:0] e;
    bit [3:0] s;
  endclass
  initial begin
    automatic c o = new;
    automatic int bad = 0;
    for (int i = 0; i < 4; i++) begin
      if (!o.randomize() with { d[8*i+7 -: 8] == 8'h5a; e[i*4 +: 4] == 4'hc; }) bad++;
      else if (o.d[8*i +: 8] != 8'h5a || o.e[i*4+3 -: 4] != 4'hc) bad++;
    end
    for (int i = 0; i < 8; i++) begin
      if (!o.randomize() with { d[8*(i%4)+7 -: 8] != 0; d[8*(i%4)+7 -: 8] < 3; }) bad++;
      else if (o.d[8*(i%4)+7 -: 8] == 0 || o.d[8*(i%4)+7 -: 8] >= 3) bad++;
    end
    if (bad != 0) $error("bad=%0d", bad);
    else $display("PASSED");
  end
endmodule
