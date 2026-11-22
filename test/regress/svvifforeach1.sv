interface svvifforeach1_if #(parameter int Width = 1);
  logic [Width-1:0] pins;
endinterface

class svvifforeach1_drv;
  virtual svvifforeach1_if #(8) vif;

  function automatic int count_ones();
    int n = 0;
    foreach (vif.pins[i]) begin
      if (vif.pins[i] === 1'b1)
        n++;
    end
    return n;
  endfunction
endclass

module svvifforeach1;
  svvifforeach1_if #(8) u_if();

  initial begin
    automatic svvifforeach1_drv d = new();
    d.vif = u_if;
    u_if.pins = 8'b1011_0101;
    #1;
    if (d.count_ones() != 5)
      $fatal(1, "count=%0d exp=5", d.count_ones());
    if ($size(d.vif.pins) != 8)
      $fatal(1, "size=%0d exp=8", $size(d.vif.pins));
    $display("PASSED");
  end
endmodule
