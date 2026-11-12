module top (input toggle, input [31:0] cyc);
  covergroup g @(posedge cyc);
    toggle : coverpoint (toggle) {
      bins off = {0};
    }
  endgroup
  g c1 = new();
endmodule
