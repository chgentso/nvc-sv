// Inputs left open take their VHDL default, array bounds from a record
// generic, and outputs connected to part of a variable
import mixed33_pkg::*;

module mixed33;
   t_rec in_rec;
   t_down d1, d2, d3;
   t_up u1, u2, u3;
   t_outer outer;
   t_rec r1, r2, r3;
   reg on = 1;

   // All defaults
   mixed33_sub u_default (.plain(on), .down(d1), .up(u1), .o_rec(r1), .o_arr());

   // Same entity with every input connected
   mixed33_sub u_full (.rst_n(2'b01), .en(1'b0), .r_in(in_rec), .plain(on),
                       .down(d2), .up(u2), .o_rec(r2), .o_arr());

   // Empty connections also take the default
   mixed33_sub u_empty (.rst_n(), .en(), .r_in(), .plain(on),
                        .down(d3), .up(u3),
                        .o_rec(outer.first), .o_arr(outer.items[1]));

   `define CHECK(x, y) if ((x) !== (y)) begin \
      $display("FAILED %b: %b", y, (x)); $finish; end

   initial begin
      in_rec.valid = 1'b0;
      in_rec.data = 4'h6;
      #1;

      `CHECK(d1[1].valid, 1'b1);
      `CHECK(d1[1].data, 4'ha);
      `CHECK(d1[0].valid, 1'b1);
      `CHECK(d1[0].data, 4'h5);
      `CHECK(u1[0].valid, 1'b1);
      `CHECK(u1[0].data, 4'h1);
      `CHECK(u1[1].valid, 1'b1);
      `CHECK(u1[1].data, 4'h2);
      `CHECK(r1.data, 4'hc);

      `CHECK(d2[1].valid, 1'b0);
      `CHECK(d2[0].valid, 1'b1);
      `CHECK(d2[0].data, 4'h6);
      `CHECK(u2[0].valid, 1'b0);
      `CHECK(u2[1].valid, 1'b0);

      `CHECK(d3[1].valid, 1'b1);
      `CHECK(d3[0].data, 4'h5);
      `CHECK(u3[0].valid, 1'b1);

      `CHECK(outer.first.valid, 1'b1);
      `CHECK(outer.first.data, 4'hc);
      `CHECK(outer.items[1].valid, 1'b1);
      `CHECK(outer.items[1].data, 4'hd);

      // Still following the input with no default
      on = 1'b0;
      #1;
      `CHECK(r1.valid, 1'b0);
      `CHECK(outer.first.valid, 1'b0);
      `CHECK(outer.items[1].valid, 1'b0);

      $display("PASSED");
      $finish;
   end
endmodule
