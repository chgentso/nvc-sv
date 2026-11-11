// A field of a structure member of a virtual interface referenced by a
// class which is analysed before the interface is declared
package svvifstruct1_pkg;
   typedef struct packed {
      logic       valid;
      logic [3:0] data;
   } key_t;
endpackage

package svvifstruct1_cls;
   import svvifstruct1_pkg::*;

   class c;
      virtual svvifstruct1_if vif;

      function void check(input bit exp_valid, input bit [3:0] exp_data);
         if (vif.key.valid !== exp_valid) $fatal(1, "valid %b", vif.key.valid);
         if (vif.key.data !== exp_data) $fatal(1, "data %h", vif.key.data);
         if (vif.mp.sig !== 1'b1) $fatal(1, "sig");
      endfunction

      function void set(input bit [3:0] d);
         vif.key.data = d;
         vif.key.valid = 1'b1;
      endfunction

      function bit [4:0] both();
         return {vif.key.valid, vif.key.data};
      endfunction
   endclass
endpackage

interface svvifstruct1_if;
   import svvifstruct1_pkg::*;
   key_t key;
   logic sig = 1'b1;
   modport mp (input sig);
endinterface

module svvifstruct1;
   import svvifstruct1_pkg::*;
   svvifstruct1_if i();

   initial begin
      automatic svvifstruct1_cls::c o = new;
      o.vif = i;
      i.key = '{valid: 1'b0, data: 4'h3};
      #1 o.check(1'b0, 4'h3);
      o.set(4'ha);
      #1 o.check(1'b1, 4'ha);
      if (o.both() !== 5'b1_1010) $fatal(1, "both %b", o.both());
      $display("PASSED");
   end
endmodule
