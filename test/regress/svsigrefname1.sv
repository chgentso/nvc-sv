package svsigrefname1_pkg;
  class sram_ctrl_like #(parameter int Depth = 4);
    int total;
    task automatic add_named(int a, string label, int b, string tag);
      #1;
      if (label == "ok" && tag == "go")
        total += a + b;
    endtask
  endclass
endpackage

module svsigrefname1;
  import svsigrefname1_pkg::*;

  initial begin
    automatic sram_ctrl_like #(8) obj = new();
    obj.add_named(10, "ok", 32, "go");
    if (obj.total !== 42) begin
      $display("FAIL: total=%0d", obj.total);
      $finish;
    end
    $display("PASSED");
  end
endmodule
