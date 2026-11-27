module svdelaytype1;
   real r, rsrc;
   string s, ssrc;
   reg [15:0] bits = 16'h4142;
   event ev;

   initial begin
      rsrc = 1.25;
      fork
         r = #2 rsrc;
         begin #1; rsrc = 9.5; end
      join
      if (r != 1.25) $fatal(1, "real delay got %f", r);

      ssrc = "AB";
      fork
         s = #2 ssrc;
         begin #1; ssrc = "ZZ"; end
      join
      if (s != "AB") $fatal(1, "string delay got '%s'", s);

      s = "";
      fork
         s = @(ev) string'(bits);
         begin #1; bits = 16'h5859; -> ev; end
      join
      if (s != "AB") $fatal(1, "string event got '%s'", s);

      $display("PASSED");
   end
endmodule
