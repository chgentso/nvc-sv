// Expected values: IEEE 1800-2017 21.2.1.6 (%m prints the hierarchical name of the current scope), 23.6
// (hierarchical names: instance names, named blocks, subroutine names), 27.? (generate blocks appear as
// name[index]), 26.2 (package subroutines). Width flags on %m and the scope name of unnamed blocks are
// implementation-defined (the name must merely start with the module path).
`timescale 1ns/1ns
package svhierm_pkg;
   function string pf;
      return $sformatf("%m");
   endfunction
endpackage

module svhierm_sub #(parameter D = 0);
   string got_f, got_t, got_blk, got_inner, got_w, got_unnamed, got_unnamed_w;
   function void f;
      got_f = $sformatf("%m");
   endfunction

   task automatic t;
      got_t = $sformatf("%m");
   endtask

   initial begin
      #(10 * D);
      #1 f();
      #1 t();
      #1 begin : blk
         string s;
         got_w = $sformatf("%m|%0d|%%m", 5);
         begin : inner
            got_inner = $sformatf("%m");
         end
         got_blk = $sformatf("%m");
      end
      #1 begin
         got_unnamed = $sformatf("%m");
         got_unnamed_w = $sformatf("%-20m");
      end
   end
endmodule

module svhierm;
   svhierm_sub s1();
   svhierm_sub #(1) s2();
   string gen_m[2];
   string top_m, pkg_m;
   int err = 0;
   for (genvar g = 0; g < 2; g++) begin : gg
      svhierm_sub #(g + 2) s3();
      initial #(100 + g) gen_m[g] = $sformatf("%m");
   end
   initial #200 begin
      top_m = $sformatf("%m");
      pkg_m = svhierm_pkg::pf();
   end
   task automatic chk(string what, string got, string exp);
      if (got != exp) begin err++; $error("%s: got '%s' expected '%s'", what, got, exp); end
   endtask
   task automatic chk_inst(string path, string got_f, string got_t, string got_blk, string got_inner, string got_w,
                           string got_un, string got_unw);
      chk({path, " f"}, got_f, {path, ".f"});
      chk({path, " t"}, got_t, {path, ".t"});
      chk({path, " blk"}, got_blk, {path, ".blk"});
      chk({path, " inner"}, got_inner, {path, ".blk.inner"});
      chk({path, " %m|%0d|%%m"}, got_w, {path, ".blk|5|%m"});
      if (got_un.substr(0, path.len() - 1) != path) begin err++; $error("%s unnamed: '%s'", path, got_un); end
      if (got_unw.substr(0, path.len() - 1) != path) begin
        err++; $error("%s %%-20m: '%s'", path, got_unw);
      end
   endtask
   initial begin
      #300;
      chk_inst("svhierm.s1", s1.got_f, s1.got_t, s1.got_blk, s1.got_inner, s1.got_w, s1.got_unnamed, s1.got_unnamed_w);
      chk_inst("svhierm.s2", s2.got_f, s2.got_t, s2.got_blk, s2.got_inner, s2.got_w, s2.got_unnamed, s2.got_unnamed_w);
      chk_inst("svhierm.gg[0].s3", gg[0].s3.got_f, gg[0].s3.got_t, gg[0].s3.got_blk, gg[0].s3.got_inner,
               gg[0].s3.got_w, gg[0].s3.got_unnamed, gg[0].s3.got_unnamed_w);
      chk_inst("svhierm.gg[1].s3", gg[1].s3.got_f, gg[1].s3.got_t, gg[1].s3.got_blk, gg[1].s3.got_inner,
               gg[1].s3.got_w, gg[1].s3.got_unnamed, gg[1].s3.got_unnamed_w);
      chk("gen0", gen_m[0], "svhierm.gg[0]");
      chk("gen1", gen_m[1], "svhierm.gg[1]");
      chk("top", top_m, "svhierm");
      if (pkg_m.len() < 2 || pkg_m.substr(pkg_m.len() - 2, pkg_m.len() - 1) != "pf") begin
        err++; $error("package function %%m: '%s'", pkg_m);
      end
      if (err == 0) $display("PASSED");
   end
endmodule
