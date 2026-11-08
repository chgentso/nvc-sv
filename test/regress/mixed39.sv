// Array of records whose bounds depend on a generic
module mixed39;
   import mixed39_pkg::*;
   t_rec o_v[2:0];
   t_rec o_w[1:0];

   mixed39_sub #(.n(3)) u1 (.o(o_v));
   mixed39_sub u2 (.o(o_w));

   initial begin
      #1;
      if (o_v[2].cyc !== 1'b1 || o_v[2].dat !== 4'hc
          || o_v[1].dat !== 4'hb || o_v[0].dat !== 4'ha
          || o_w[1].dat !== 4'hb || o_w[0].dat !== 4'ha)
        $display("FAILED %h %h %h", o_v[2].dat, o_v[1].dat, o_v[0].dat);
      else
        $display("PASSED");
      $finish;
   end
endmodule
