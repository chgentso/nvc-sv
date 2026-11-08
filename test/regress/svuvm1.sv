// The patterns the UVM library depends on for its factory
package fact;
  class wrapper;
    virtual function string name();
      return "wrapper";
    endfunction
    virtual function int make();
      return -1;
    endfunction
  endclass

  class factory;
    static wrapper table_by_name[string];
    static function void register(wrapper w);
      table_by_name[w.name()] = w;
    endfunction
    static function int create(string n);
      if (!table_by_name.exists(n))
        return -2;
      return table_by_name[n].make();
    endfunction
  endclass

  class registry #(type T = int, string NAME = "<unknown>") extends wrapper;
    typedef registry #(T, NAME) this_type;
    local static this_type me = get();
    static function this_type get();
      if (me == null) begin
        me = new;
        factory::register(me);
      end
      return me;
    endfunction
    virtual function string name();
      return NAME;
    endfunction
    virtual function int make();
      T obj = new;
      return obj.id();
    endfunction
  endclass
endpackage

// Nothing in the design refers to this package by name other than the import
package user_pkg;
  import fact::*;

  class widget;
    typedef registry #(widget, "widget") type_id;
    function int id();
      return 42;
    endfunction
  endclass

  class gadget;
    typedef registry #(gadget, "gadget") type_id;
    static type_id unused = type_id::get();
    function int id();
      return 7;
    endfunction
  endclass
endpackage

module svuvm1;
  import fact::*;
  import user_pkg::*;

  int failed = 0;

  `define check(x, y) \
    if ((x) !== (y)) begin $display("FAILED %0d", (x)); failed++; end

  // Results of output and inout arguments are seen by the caller
  function void set_handle(inout wrapper w);
    w = new;
  endfunction

  initial begin
    automatic wrapper w;

    set_handle(w);
    `check(w == null, 0);

    // The specialisation is registered before the simulation starts even
    // though the name is only used here at run time
    `check(factory::create("gadget"), 7);
    `check(factory::create("widget"), 42);
    `check(factory::create("nothing"), -2);

    #1;

    // Package state is still valid after a process suspends
    `check(factory::create("gadget"), 7);

    if (failed == 0) $display("PASSED");
    $finish;
  end
endmodule
