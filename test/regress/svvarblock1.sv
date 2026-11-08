// Expected values: IEEE 1800-2017 6.8 (var declarations; implicit type of `var [7:0]` is logic [7:0], plain `var x`
// is a 1-bit logic), 11.6 / 11.8 (assignment truncation, expression width), 6.21 (automatic variables), 9.3.2
module svvarblock1;
   int errs = 0;
   int tw, tn;
   function automatic int f(int n);
      var [7:0] a;
      var int b = 3;
      var logic [3:0] c, d;
      a = n; c = n + 1; d = 2;
      return a + b + c + d;
   endfunction
   task automatic t(int n);
      var [15:0] w;
      var bit [3:0] nib;
      w = n * 100; nib = n;
      tw = w; tn = nib;
   endtask
   initial begin
      var [7:0] a;
      var int b;
      var logic [3:0] c, d;
      var x;
      var automatic [3:0] au = 9;
      a = 255; b = 6; c = 7; d = 8; x = 1;
      a = a + 1;
      if (a !== 8'd0 || b !== 6 || c !== 4'd7 || d !== 4'd8 || x !== 1'b1 || au !== 4'd9)
         begin errs++; $error("%0d %0d %0d %0d %0d %0d, expected 0 6 7 8 1 9", a, b, c, d, x, au); end
      if (f(10) !== 26) begin errs++; $error("f(10)=%0d, expected 26", f(10)); end
      t(3);
      if (tw !== 300 || tn !== 3) begin errs++; $error("t: w=%0d nib=%0d, expected 300 3", tw, tn); end
      for (int i = 0; i < 2; i++) begin
         var [7:0] b0, b1;
         b0 = 8'h10 + i;
         b1 = 8'h20 + i;
         if (b0 !== 8'h10 + i || b1 !== 8'h20 + i) begin errs++; $error("i=%0d b0=%h b1=%h", i, b0, b1); end
      end
      fork
         begin var [3:0] q; q = 5; if (q !== 4'd5) begin errs++; $error("q=%0d", q); end end
      join
      if (errs == 0) $display("PASSED");
   end
endmodule
