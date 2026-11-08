// Expected values: IEEE 1800-2017 8.4 / 12.4 / 11.4.7: a class handle used as an operand of
// the logical operators (&&, ||, !), as an if condition and as the ?: condition is true when
// non-null and false when null; 11.4.5 (comparison with null); 12.7 (while loop).
class handler;
   int n = 3;
endclass

module svclasslogic1;
   handler h1, h2;
   bit irq = 1;
   bit z = 0;
   int cnt;
   int errors = 0;
   task automatic chk(input logic got, input logic exp, input string what);
     if (got !== exp) begin $error("%s = %b, expected %b", what, got, exp); errors++; end
   endtask
   initial begin
      h2 = new;
      chk(h1 && irq, 0, "h1 && irq");
      chk(irq && h1, 0, "irq && h1");
      chk(h2 && irq, 1, "h2 && irq");
      chk(irq && h2, 1, "irq && h2");
      chk(h1 || z, 0, "h1 || z");
      chk(z || h1, 0, "z || h1");
      chk(h2 || z, 1, "h2 || z");
      chk(z || h2, 1, "z || h2");
      chk(h1 && h2, 0, "h1 && h2");
      chk(h2 && h2, 1, "h2 && h2");
      chk(h1 || h2, 1, "h1 || h2");
      chk(!h1, 1, "!h1");
      chk(!h2, 0, "!h2");
      chk(!(h1 && h2), 1, "!(h1 && h2)");
      if (!(h1 || h2)) begin $error("if (h1 || h2) not taken"); errors++; end
      if (!(h2 && !h1)) begin $error("if (h2 && !h1) not taken"); errors++; end
      if (h1) begin $error("if (h1) taken for null handle"); errors++; end
      if ((h2 ? 1 : 2) !== 1) begin $error("h2 ? 1 : 2"); errors++; end
      if (((h1 && irq) ? 1 : 2) !== 2) begin $error("(h1 && irq) ? 1 : 2"); errors++; end
      while (irq && h2) begin
         h2 = null;
         cnt++;
      end
      if (cnt !== 1) begin $error("cnt = %0d, expected 1", cnt); errors++; end
      chk((h1 != null) && irq, 0, "(h1 != null) && irq");
      chk((h1 == null) || h2, 1, "(h1 == null) || h2");
      if (errors == 0) $display("PASSED");
   end
endmodule
