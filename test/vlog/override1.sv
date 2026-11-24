package override1;
   class base;
      function :initial int m_i(); return 1; endfunction
      function :final int m_f(); return 2; endfunction
      function :initial :final int m_if(); return 3; endfunction
      function :extends int bad_root(); return 4; endfunction  // Error
   endclass

   class child extends base;
      function :extends int m_i(); return 10; endfunction      // OK
      function :extends :final int m_ok(); return 0; endfunction // Error
      function :initial int bad_init(); return 0; endfunction  // OK
      function int m_f(); return 20; endfunction               // Error
      function :extends int m_if(); return 30; endfunction     // Error
   endclass

   class grand extends child;
      function :initial int m_i(); return 100; endfunction     // Error
   endclass

   class :final leaf;
   endclass

   class bad_sub extends leaf;                                 // Error
   endclass
endpackage
