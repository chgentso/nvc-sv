// Division by zero in a constraint expression gives X (IEEE 1800-2017 11.4.2)
module crv13;
   class c1;
      rand bit [3:0] a, b;
      constraint k { !((a / b) == 1); }
   endclass
   class c2;
      rand bit [3:0] a, b;
      constraint k { b == 0; (a == 3) || ((a / b) == 1); }
   endclass
   class c3;
      rand bit [3:0] a, b;
      constraint k { b == 0; ((a / b) == 1) || (a == 3); }
   endclass
   class c4;
      rand bit [3:0] a, b;
      constraint k { b == 0; !((a / b) == 1); }
   endclass
   class c5;
      rand bit [3:0] a, b;
      constraint k { b == 0; (a / b) != 1; }
   endclass
   class c6;
      rand bit [3:0] a, b;
      constraint k { b == 0; ((a / b) == 1) -> (a == 5); a == 4; }
   endclass
   initial begin
      automatic c1 o1 = new;
      automatic c2 o2 = new;
      automatic c3 o3 = new;
      automatic c4 o4 = new;
      automatic c5 o5 = new;
      automatic c6 o6 = new;
      automatic int errors = 0, r;
      repeat (300) begin
         r = o1.randomize();
         if (r != 1 || o1.b == 0) errors++;
      end
      r = o2.randomize();
      if (r != 1 || o2.a != 3) begin errors++; $display("or"); end
      r = o3.randomize();
      if (r != 1 || o3.a != 3) begin errors++; $display("or swapped"); end
      r = o4.randomize();
      if (r != 0) begin errors++; $display("not"); end
      r = o5.randomize();
      if (r != 0) begin errors++; $display("ne"); end
      r = o6.randomize();
      $display("implies %0d", r);
      if (errors == 0) $display("PASSED"); else $display("FAILED %0d", errors);
      $finish;
   end
endmodule
