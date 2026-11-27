module svfmtpct1;
   string s;

   initial begin
      s = $sformatf("trail:%");
      if (s != "trail:%")
         $fatal(1, "expected 'trail:%%', got '%s'", s);
      $display("ok:%");
      $display("PASSED");
   end
endmodule
