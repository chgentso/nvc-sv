// IEEE 1800-2017 16.2, 23.9: the label of an assertion is declared in the
// scope that contains it, so it cannot be used for another item there.
module assertlabel1(input clk);
  int cyc;
  covlabel: cover property (@(posedge clk) cyc == 5);
  covlabel: cover property (@(posedge clk) cyc == 6);   // Error
  other: cover property (@(posedge clk) cyc == 7);      // OK
  initial begin
    CHECK: assert (cyc == 0);
    CHECK: assert (cyc == 1);                   // Error
    begin : inner
      CHECK: assert (cyc == 2);                 // OK
    end
  end
  if (1) begin : g
    covlabel: cover property (@(posedge clk) cyc == 8); // OK
  end
endmodule
