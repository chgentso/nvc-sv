// Self-checking: the package declares a virtual interface type for an interface which is declared after it
// and which uses the types of the package (a cycle through the virtual interface), and selects a member
// of an element of an array member of the interface (vif.slots[i].valid).
// Expected values: IEEE 1800-2017 25.9 (virtual interfaces), 7.2 (structures), 26.3 (package references).
package env_pkg;
  typedef struct packed { logic valid; logic [7:0] data; } slot_t;
  typedef logic [3:0][7:0] shares_t;

  class cfg_c;
    virtual ifc vif;

    function bit slot_valid(int slot);
      return vif.slots[slot].valid;
    endfunction

    function logic [7:0] slot_data(int slot);
      return vif.slots[slot].data;
    endfunction

    function void set_data(int slot, logic [7:0] d);
      vif.slots[slot].data = d;
    endfunction

    function shares_t get_shares();
      return vif.shares;
    endfunction
  endclass
endpackage

interface ifc;
  import env_pkg::*;
  slot_t slots [4];
  shares_t shares = 32'hdeadbeef;
  initial begin
    for (int i = 0; i < 4; i++) slots[i] = '{valid: i[0], data: 8'(i * 16 + 3)};
  end
endinterface

module svvifelem1;
  ifc u();
  env_pkg::cfg_c c;
  int bad;
  initial begin
    c = new;
    c.vif = u;
    #1;
    for (int i = 0; i < 4; i++) begin
      if (c.slot_valid(i) !== i[0]) begin $error("valid %0d", i); bad++; end
      if (c.slot_data(i) !== 8'(i * 16 + 3)) begin $error("data %0d", i); bad++; end
    end
    c.set_data(2, 8'h77);
    #1;
    if (u.slots[2].data !== 8'h77 || c.slot_data(2) !== 8'h77 || c.slot_valid(2) !== 1'b0) begin
      $error("write through the virtual interface: %h", u.slots[2]); bad++;
    end
    if (c.get_shares() !== 32'hdeadbeef) begin $error("shares %h", c.get_shares()); bad++; end
    if (bad == 0) $display("PASSED");
  end
endmodule
