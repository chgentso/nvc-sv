// Reading a member of a null virtual interface is a run time error
interface svnullvif1_if;
  logic sig;
endinterface

module svnullvif1;
  svnullvif1_if intf ();
  virtual svnullvif1_if vif;
  logic x;
  initial begin
    #1 x = vif.sig;
    $display("not reached");
  end
endmodule
