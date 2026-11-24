// Virtual dispatch, $cast and randomize() dispatch across deep inheritance,
// diamond interface classes and parameterised class specialisations.

package ch1_pkg;
   interface class IRoot;
      pure virtual function int tag();
   endclass

   interface class ILeft extends IRoot;
      pure virtual function int left_val();
   endclass

   interface class IRight extends IRoot;
      pure virtual function int right_val();
   endclass

   virtual class Base implements ILeft;
      rand int x;
      constraint c_base { x >= 10 && x <= 20; }
      pure virtual function int id();
      virtual function int tag(); return 1; endfunction
      virtual function int left_val(); return 10; endfunction
   endclass

   class Mid extends Base implements IRight;
      rand int y;
      constraint c_mid { y == x + 1; }
      virtual function int id(); return 2; endfunction
      virtual function int tag(); return 2; endfunction
      virtual function int right_val(); return 20; endfunction
   endclass

   class Leaf extends Mid;
      constraint c_leaf { x == 15; }
      virtual function int id(); return 3; endfunction
      virtual function int tag(); return 3; endfunction
   endclass

   class Param #(int W = 4);
      virtual function int width(); return W; endfunction
   endclass

   class SubParam #(int W = 4) extends Param#(W);
      virtual function int width(); return W * 2; endfunction
   endclass
endpackage

module vlog_class_hier1;
   import ch1_pkg::*;

   initial begin
      Leaf lf = new;
      Base b = lf;
      Mid  m;
      IRoot ir;
      ILeft il;
      IRight ir2;

      if (b.id() != 3) $fatal(1, "virtual id %0d", b.id());
      if (b.tag() != 3) $fatal(1, "virtual tag %0d", b.tag());

      if (!$cast(m, b)) $fatal(1, "cast Base->Mid failed");
      if (m.right_val() != 20) $fatal(1, "right_val %0d", m.right_val());

      ir = lf;
      if (ir.tag() != 3) $fatal(1, "IRoot tag %0d", ir.tag());
      if (!$cast(il, ir) || il.left_val() != 10) $fatal(1, "ILeft cast");
      if (!$cast(ir2, ir) || ir2.right_val() != 20) $fatal(1, "IRight cast");

      if (!b.randomize()) $fatal(1, "randomize failed");
      if (lf.x != 15 || lf.y != 16)
         $fatal(1, "randomize mismatch x=%0d y=%0d", lf.x, lf.y);

      begin
         SubParam#(8) sp = new;
         Param#(8) p = sp;
         SubParam#(8) sp2;
         if (p.width() != 16) $fatal(1, "param width %0d", p.width());
         if (!$cast(sp2, p) || sp2.width() != 16) $fatal(1, "param cast");
      end

      $display("PASSED");
   end
endmodule
