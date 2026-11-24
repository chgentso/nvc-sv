package puremeth1;
  virtual class base;
    pure virtual function void f();
    pure virtual task t();
  endclass

  virtual class mid extends base;
    // OK: still virtual
  endclass

  class ok extends base;
    virtual function void f(); endfunction
    virtual task t(); endtask
  endclass

  class bad extends base;   // Error: missing f and t
  endclass

  class bad2 extends mid;   // Error: missing f and t
    virtual function void f(); endfunction
  endclass
endpackage
