// Expected values: IEEE 1800-2017 6.16.6/11.4.5 (string equality compares contents; a string literal
// operand converts to string), 23.6/23.7 (hierarchical reference to a variable and a function of an
// instance), 11.4.12.2 (string concatenation), 11.4.11 (conditional operator), 12.4 (if)
module svstrhier1sub;
   string msg = "Test passed\n";
   function automatic string get(); return msg; endfunction
endmodule
module svstrhier1;
   svstrhier1sub DUT();
   string s2;
   int errs = 0;
   task automatic ck(string w, bit got, bit exp);
     if (got !== exp) begin $error("%s: got %b expected %b", w, got, exp); errs++; end
   endtask
   function automatic string loc(); return "abc"; endfunction
   function automatic string loc2(); return DUT.msg; endfunction
   initial begin
      #1;
      s2 = "Test passed\n";
      ck("1", DUT.get() == "Test passed\n", 1);
      ck("2", DUT.get() == s2, 1);
      ck("3", s2 == DUT.get(), 1);
      ck("4", loc() == "abc", 1);
      ck("5", loc2() == "Test passed\n", 1);
      ck("6", DUT.msg == "Test passed\n", 1);
      if (DUT.get() == "Test passed\n") ; else begin $error("if branch"); errs++; end
      ck("8", "Test passed\n" == DUT.get(), 1);
      ck("9", DUT.get() != "Test passed\n", 0);
      ck("10", {DUT.msg, "x"} == "Test passed\nx", 1);
      ck("11", (DUT.msg == "Test passed\n") ? DUT.get() == "Test passed\n" : 1'b0, 1);
      if (errs == 0) $display("PASSED");
   end
endmodule
