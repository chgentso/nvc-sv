module evnba1;
   class C;
      event e;
      static event se;
      event eq[$];
      static event seq[$];
   endclass

   event me;
   event fixed[2];
   event q[$];
   event dyn[];
   event assoc[int];
   C c = new;
   int i;

   task automatic tk();
      event ae;
      -> ae;          // OK: blocking trigger of automatic event
      ->> ae;         // Error: non-blocking trigger of automatic event
   endtask

   initial begin
      ->> me;         // OK
      ->> fixed[i];   // OK: fixed-size array
      ->> c.e;        // OK: class property
      ->> C::se;      // OK: static class property
      ->> q[i];       // Error: queue element
      ->> dyn[i];     // Error: dynamic array element
      ->> assoc[i];   // Error: associative array element
      ->> c.eq[i];    // Error: class queue element
      ->> C::seq[i];  // Error: static class queue element
   end
endmodule
