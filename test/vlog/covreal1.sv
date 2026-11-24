module covreal1;
   real r;
   string s;
   bit [3:0] v;

   covergroup cg;
      cp_ok: coverpoint v {
         wildcard bins w = {4'b1??0};
         bins t = (1 => 2);
         bins f = {[0:7]} with (item > 1);
      }
      cp_rw: coverpoint r {
         wildcard bins one = {1};
      }
      cp_rt: coverpoint r {
         bins tr = (1 => 2);
      }
      cp_rwith: coverpoint r {
         bins b = {[0:3]} with (item > 1);
      }
      cp_swith: coverpoint s {
         bins b = {"a"} with (1);
      }
   endgroup
endmodule
