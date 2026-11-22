interface svvifuninst1_opt_if ();
  logic clk;
  logic sig;
  clocking cb @(posedge clk);
    input sig;
  endclocking
  modport mp (clocking cb);
  function logic read_sig();
    return sig;
  endfunction
endinterface

interface svvifuninst1_main_if ();
  logic clk;
  logic sig;
endinterface

package svvifuninst1_pkg;
  class monitor;
    virtual svvifuninst1_main_if main_vif;
    virtual svvifuninst1_opt_if opt_vif;

    function logic sample();
      return (opt_vif == null) ? main_vif.sig : opt_vif.mp.cb.sig;
    endfunction

    function logic call_opt();
      return (opt_vif == null) ? main_vif.sig : opt_vif.read_sig();
    endfunction

    task wait_edge();
      if (opt_vif != null)
        @(opt_vif.mp.cb);
      else
        #1;
    endtask
  endclass
endpackage

module svvifuninst1;
  import svvifuninst1_pkg::*;

  svvifuninst1_main_if u_main();

  initial begin
    automatic monitor m = new();
    m.main_vif = u_main;
    u_main.sig = 1'b1;
    m.wait_edge();
    if (m.sample() !== 1'b1 || m.call_opt() !== 1'b1) begin
      $display("FAIL: sample=%b call_opt=%b", m.sample(), m.call_opt());
      $finish;
    end
    $display("PASSED");
  end
endmodule
