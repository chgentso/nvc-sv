// A cycle delay drive of a clocking block signal of an interface instance
// that is clocked by an event, through a hierarchical name
interface svclkdrive1_if;
  event ev;
  bit e;
  clocking ecb @(ev);
    output e;
  endclocking
endinterface

module svclkdrive1;
  svclkdrive1_if vbus ();

  initial forever #10 ->vbus.ev;

  initial begin
    #25 vbus.ecb.e <= ##2 1;
    #5 if (vbus.e !== 1'b0) $fatal(1, "driven too early");
    #20 if (vbus.e !== 1'b1) $fatal(1, "not driven: %b", vbus.e);
    $display("PASSED");
    $finish;
  end
endmodule
