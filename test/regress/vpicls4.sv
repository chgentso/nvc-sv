// A component tree in the style of a verification library: a root object
// held by a constant of a package and by a static property of its class, and
// children in an associative array by name.  The C side is
// test/vpi/vpicls4.c.

package vpicls4_pkg;
   class comp;
      string m_name;
      comp   m_parent;
      comp   m_children[string];
      int    level;

      function new(string name, comp parent);
         m_name = name;
         m_parent = parent;
         if (parent != null) begin
            parent.m_children[name] = this;
            level = parent.level + 1;
         end
      endfunction
   endclass

   class root extends comp;
      static root m_inst;

      function new();
         super.new("", null);
      endfunction

      static function root get();
         if (m_inst == null)
            m_inst = new;
         return m_inst;
      endfunction
   endclass

   class env extends comp;
      int checks;
      function new(string name, comp parent);
         super.new(name, parent);
         checks = 7;
      endfunction
   endclass

   const root top = root::get();
endpackage

module vpicls4;
   import vpicls4_pkg::*;

   env e;
   comp agent, mon, drv;

   initial begin
      e = new("env", top);
      agent = new("agent", e);
      mon = new("mon", agent);
      drv = new("drv", agent);
      #5;
   end
endmodule
