module svwildeq2;
   localparam string AB = "AB";
   string s1 = "AB", s2 = "CD";
   bit r;

   initial begin
      r = (AB ==? "AA");
      if (r !== 1'b0) $fatal(1, "AB ==? AA");
      r = (AB ==? "AB");
      if (r !== 1'b1) $fatal(1, "AB ==? AB");
      r = (AB !=? "AB");
      if (r !== 1'b0) $fatal(1, "AB !=? AB");
      r = (AB !=? "AA");
      if (r !== 1'b1) $fatal(1, "AB !=? AA");

      if ((s1 ==? "AB") !== 1'b1) $fatal(1, "s1 ==? AB");
      if ((s1 ==? s2) !== 1'b0) $fatal(1, "s1 ==? s2");
      if ((s1 !=? s2) !== 1'b1) $fatal(1, "s1 !=? s2");
      if (("AB" ==? s1) !== 1'b1) $fatal(1, "lit ==? s1");

      $display("PASSED");
   end
endmodule
