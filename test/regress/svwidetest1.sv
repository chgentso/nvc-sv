// A two-state vector wider than 64 bits used as a condition is true only
// if some bit is set
module svwidetest1;
   function automatic bit pick(bit [127:0] x);
      if (x) return 1;
      return 0;
   endfunction

   function automatic int dflt(bit [383:0] a = '0);
      if (a) return 1;
      return 0;
   endfunction

   initial begin
      bit [64:0]  z65 = '0;
      bit [127:0] z128 = '0, hi = '0;
      bit [383:0] z384 = '0, lo = 384'h1;
      int n = 0;

      hi[127] = 1;

      if (z65) $fatal(1, "z65 true");
      if (z128) $fatal(1, "z128 true");
      if (z384) $fatal(1, "z384 true");
      if (!hi) $fatal(1, "hi false");
      if (!lo) $fatal(1, "lo false");

      if ((z128 ? 1 : 0) != 0) $fatal(1, "cond zero");
      if ((hi ? 1 : 0) != 1) $fatal(1, "cond hi");
      if (z128 && 1'b1) $fatal(1, "and zero");
      if (!(hi || z128)) $fatal(1, "or hi");
      if (pick(z128) !== 0 || pick(hi) !== 1) $fatal(1, "pick");
      if (dflt() !== 0 || dflt(lo) !== 1) $fatal(1, "default");

      while (z384) n++;
      if (n != 0) $fatal(1, "while ran");

      $display("PASSED");
   end
endmodule
