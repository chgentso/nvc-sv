// Virtual interface members compared and used in expressions from a package class.
// Expected values: IEEE 1800-2017 25.9 (virtual interface member access), 11.4.5 (equality),
// 11.4.3 (arithmetic), 6.8 (bit initial value 0).
package agp;
  class agcfg;
    virtual ifc vif;
  endclass
endpackage

interface ifc;
  logic scl_i = 1;
  logic [3:0] bus = 4'd9;
endinterface

package envp;
  import agp::*;
  class c;
    agcfg m_ag;
    bit sv;
    int n;
    int errors;
    task run();
      bit a_taken, b_taken;
      // sv starts at 0 (bit), scl_i is 1
      if (m_ag.vif.scl_i == sv) a_taken = 1;
      if (sv != m_ag.vif.scl_i) b_taken = 1;
      if (a_taken !== 1'b0 || b_taken !== 1'b1) begin $error("a=%b b=%b", a_taken, b_taken); errors++; end
      sv = m_ag.vif.scl_i;
      n = m_ag.vif.bus + 1;
      if (sv !== 1'b1 || n != 10) begin $error("sv=%b n=%0d", sv, n); errors++; end
    endtask
  endclass
endpackage

module svvifpending1;
  ifc i();
  envp::c o = new;
  initial begin
    o.m_ag = new;
    o.m_ag.vif = i;
    o.run();
    if (o.errors == 0) $display("PASSED");
  end
endmodule
