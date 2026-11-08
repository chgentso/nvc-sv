// Strings
module vlog58;
   string g = "global";
   initial begin
      string s, t;
      automatic bit [7:0] ch = "!";
      s = "abc";
      t = {s, "def", ch};
      if (t != "abcdef!") $display("FAILED concat <%s>", t);
      if (s == t) $display("FAILED neq");
      if (g != "global") $display("FAILED module <%s>", g);
      $display("string %s and %0s", s, g);
      $display("PASSED");
   end
endmodule
