// Strength resolution: strong drive vs pull1 continuous assign vs weak1 pullup.
// Expected values: IEEE 1800-2017 28.12 (strength resolution: stronger driver wins),
// 28.8/28.9 (pullup with strength), 10.3.4 (drive strength on continuous assign; highz0 drives
// nothing for 0), 6.6.1 (wire resolution of z), 10.3 (assign to net)
`timescale 1ns/1ns
module svpullstrength1;
  logic [3:0] en = 4'b0000, o = 4'b0000;
  wire [3:0] sio, si_pulldown;
  int errs = 0;
  for (genvar i = 0; i < 4; i++) begin : g
    pullup (weak1) pd_in_i (si_pulldown[i]);
    assign sio[i] = en[i] ? o[i] : 'z;
    assign (highz0, pull1) sio[i] = !en[i];
    assign si_pulldown[i] = sio[i];
  end
  task automatic chk(string n, logic [3:0] a, logic [3:0] b, logic [3:0] exp);
    if (a !== exp || b !== exp) begin errs++; $error("%s: sio=%b pd=%b expected %b", n, a, b, exp); end
  endtask
  initial begin
    #1 chk("idle", sio, si_pulldown, 4'b1111);   // all strong z: pull1 and weak1 give 1
    en = 4'b0101; o = 4'b0001;
    #1 chk("drive", sio, si_pulldown, 4'b1011);  // enabled bits: strong o beats pull1; others 1
    en = 4'b1111; o = 4'b1010;
    #1 chk("all", sio, si_pulldown, 4'b1010);
    en = 4'b0000;
    #1 chk("off", sio, si_pulldown, 4'b1111);
    if (errs == 0) $display("PASSED");
  end
endmodule
