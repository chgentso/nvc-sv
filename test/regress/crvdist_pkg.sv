// Helpers for tests of the distribution of randomize(): a histogram and a
// chi-square test against a bound.  The bound is the upper point of the
// chi-square distribution at p = 1e-4 by the Wilson-Hilferty formula, so a
// test with a fixed seed fails only for a distribution that is wrong, not by
// bad luck.  Cells with fewer than five expected values are pooled.
package crvdist;

   // Upper point of the chi-square distribution with df degrees of freedom
   function automatic real chi2_bound(int df);
      real t, u;
      t = 2.0 / (9.0 * df);
      u = 1.0 - t + 3.7190165 * $sqrt(t);
      return df * u * u * u;
   endfunction

   class hist;
      int          nbins;
      int unsigned bins[];
      int unsigned total;
      int unsigned outside;

      function new(int n);
         nbins = n;
         bins = new[n];
         foreach (bins[i])
            bins[i] = 0;
         total = 0;
         outside = 0;
      endfunction

      function void add(int b);
         total++;
         if (b >= 0 && b < nbins)
            bins[b]++;
         else
            outside++;
      endfunction

      // Chi-square against expected weights, which need not add up to one;
      // sets df and returns the statistic.  A value in a bin of weight zero
      // makes it infinite.
      function real chi2(real w[], output int df);
         real sum, e, d, x, pooled_e, pooled_o;
         int cells;

         sum = 0.0;
         foreach (w[i])
            sum += w[i];

         x = 0.0;
         pooled_e = 0.0;
         pooled_o = 0.0;
         cells = 0;
         foreach (w[i]) begin
            e = total * w[i] / sum;
            if (w[i] == 0.0) begin
               if (bins[i] != 0)
                  x += 1.0e9;
            end
            else if (e < 5.0) begin
               pooled_e += e;
               pooled_o += bins[i];
            end
            else begin
               d = bins[i] - e;
               x += d * d / e;
               cells++;
            end
         end

         if (pooled_e >= 5.0) begin
            d = pooled_o - pooled_e;
            x += d * d / pooled_e;
            cells++;
         end

         df = cells > 1 ? cells - 1 : 1;
         return x;
      endfunction

      function real chi2_uniform(int cells, output int df);
         real w[];
         w = new[cells];
         foreach (w[i])
            w[i] = 1.0;
         return chi2(w, df);
      endfunction

      // The test: report and return whether the statistic is under the bound
      function bit check(string name, real w[]);
         int df;
         real x, bound;
         bit ok;
         x = chi2(w, df);
         bound = chi2_bound(df);
         ok = x <= bound && outside == 0;
         $display("%s: %s chi2 %0.1f df %0d bound %0.1f", ok ? "PASS" : "FAIL",
                  name, x, df, bound);
         return ok;
      endfunction

      function bit check_uniform(string name, int cells);
         real w[];
         w = new[cells];
         foreach (w[i])
            w[i] = 1.0;
         return check(name, w);
      endfunction

      // Same statistic against expected weights, only printed: for a
      // distribution that is known not to be what it should be yet
      function void report_weights(string name, real w[]);
         int df;
         real x;
         x = chi2(w, df);
         $display("REPORT: %s chi2/df %0.2f (df %0d, bound %0.1f)", name,
                  x / df, df, chi2_bound(df));
      endfunction

      function void report(string name, int cells);
         real w[];
         w = new[cells];
         foreach (w[i])
            w[i] = 1.0;
         report_weights(name, w);
      endfunction
   endclass

endpackage
