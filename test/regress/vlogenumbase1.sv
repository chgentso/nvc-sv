// The base type of an enumeration may be a type declared in a package or
// class (IEEE 1800-2017 6.19).

package pkg1;
   typedef logic [7:0] uint8_t;
endpackage

package pkg2;
   typedef enum pkg1::uint8_t { A = 8'd1, B = 8'd2 } opts;
endpackage

class Cls;
   typedef bit [3:0] nibble_t;
endclass

module vlogenumbase1;
   typedef enum Cls::nibble_t { X = 4'd3, Y = 4'd9 } nib_e;
   pkg2::opts o;
   nib_e n;

   initial begin
      o = pkg2::B;
      n = Y;
      if ($bits(o) != 8 || o != 2) $fatal(1, "o %0d %0d", $bits(o), o);
      if ($bits(n) != 4 || n != 9 || pkg2::A != 1) $fatal(1, "n");
      $display("PASSED");
   end
endmodule
