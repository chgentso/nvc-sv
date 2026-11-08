// $sformatf, $fread, $rewind, $timeformat, and $realtime
module vlog63;
   initial begin
      int f, n;
      bit [15:0] d;
      string s;
      s = $sformatf("%0d-%s", 12, "x");
      if (s != "12-x") $display("FAILED sformatf <%s>", s);
      f = $fopen("vlog63.tmp", "w");
      $fwrite(f, "ABC");
      $fclose(f);
      f = $fopen("vlog63.tmp", "r");
      n = $fread(d, f);
      if (n != 2 || d !== 16'h4142) $display("FAILED fread %0d %x", n, d);
      n = $rewind(f);
      n = $fread(d, f);
      if (d !== 16'h4142) $display("FAILED rewind %x", d);
      n = $fread(d, f);
      if (n != 1 || !$feof(f) && n != 1) $display("FAILED short %0d", n);
      $fclose(f);
      $timeformat(-9, 2, "ns", 8);
      #5ns;
      s = $sformatf("%t", $realtime);
      if (s != "  5.00ns") $display("FAILED time <%s>", s);
      $display("PASSED");
   end
endmodule
