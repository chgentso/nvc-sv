// Class variables and objects seen through VPI: null and non-null
// variables, the dynamic class of an object held in a base class variable,
// inherited, redeclared and static properties, strings, 4-state and wide
// values, specialisations and two variables that refer to one object.  The C
// side is test/vpi/vpicls1.c.

package vpicls1_pkg;
   typedef enum logic [1:0] { RED, GREEN, BLUE } color_t;

   class shape;
      int          id;
      string       name;
      color_t      col;
      logic [7:0]  lg;
      bit [99:0]   wide;
      real         area;
      static int   count = 0;

      function new(string n);
         name = n;
         id = ++count;
         col = GREEN;
         lg = 8'b1x0z1010;
         wide = 100'h1_2345_6789_abcd_ef01_2345;
      endfunction

      virtual function string kind();
         return "shape";
      endfunction
   endclass

   class circle extends shape;
      string name;           // Redeclared: another property
      real   radius;
      shape  peer;

      function new(string n);
         super.new(n);
         name = {n, "!"};
         radius = 2.5;
         area = 19.5;
      endfunction

      function string kind();
         return "circle";
      endfunction
   endclass

   shape pkg_shape;    // Variable of a package
   shape pkg_null;
endpackage

class pcls #(type T = int, int N = 4);
   T v;
   bit [N-1:0] bits;
   static int made;

   function new();
      made++;
   endfunction
endclass

module vpicls1;
   import vpicls1_pkg::*;

   shape  sh;            // Base handle with a derived object
   shape  other;         // The same object through another handle
   shape  nullh;         // Never assigned
   circle ci;
   pcls #(real, 8) pr;
   pcls #(string, 4) ps;
   pcls #() pi;

   initial begin
      static circle local_c;     // Variable of a process
      ci = new("c0");
      sh = ci;
      other = ci;
      ci.peer = ci;
      pkg_shape = new("pk");
      local_c = ci;
      pr = new;
      pr.v = 2.5;
      ps = new;
      ps.v = "str";
      pi = new;
      pi.v = 42;
      pi.bits = 4'b1001;
      #5;
      $display("%s %s", sh.kind(), pkg_shape.kind());
      #5;
   end
endmodule
