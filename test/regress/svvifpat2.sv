package svvifpat2_types_pkg;
  typedef struct packed {
    logic [3:0] done;
    logic [3:0] good;
  } rom_data_t;
endpackage

interface svvifpat2_if ();
  import svvifpat2_types_pkg::*;
  rom_data_t data[2];

  function automatic void update_data(rom_data_t val, int idx);
    data[idx] = val;
  endfunction
endinterface

package svvifpat2_pkg;
  import svvifpat2_types_pkg::*;

  class driver;
    virtual svvifpat2_if vif;

    function void run();
      vif.update_data('{done: 4'hA, good: 4'h5}, 0);
      vif.update_data('{default: 4'h3}, 1);
    endfunction
  endclass
endpackage

module svvifpat2;
  import svvifpat2_types_pkg::*;
  import svvifpat2_pkg::*;

  svvifpat2_if u_if();

  initial begin
    automatic driver d = new();
    d.vif = u_if;
    d.run();
    #1;
    if (u_if.data[0].done !== 4'hA || u_if.data[0].good !== 4'h5
        || u_if.data[1].done !== 4'h3 || u_if.data[1].good !== 4'h3) begin
      $display("FAIL: d0=%h d1=%h", u_if.data[0], u_if.data[1]);
      $finish;
    end
    $display("PASSED");
  end
endmodule
