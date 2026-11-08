module svcast2;
   class A; endclass
   class B extends A; endclass

   A a;
   B b;

   initial begin
      a = new;
      $display("before");
      $cast(b, a);
      $display("after");
   end

   // Expected values: IEEE 1800-2017 8.16 and 6.24.2 ($cast): called as a task, a cast
   // that cannot succeed is a run-time error and the destination variable is left
   // unchanged; casting a handle whose dynamic type is the destination class succeeds.
   // This test must fail (run-time error from the failed $cast); the testlist keeps `fail`.
   B b2, b3;
   A a2;
   initial begin
      b2 = new;
      a2 = b2;
      $cast(b3, a2);
      if (b3 != b2) $error("successful $cast did not assign");
      #1;
      if (b != null) $error("failed $cast changed the destination");
   end
endmodule
