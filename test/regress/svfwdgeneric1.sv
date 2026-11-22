package svfwdgeneric1_pkg;
  typedef class sub_scb;

  class base_scb #(type CFG_T = int);
    int alert_count[string];
  endclass

  class my_cfg;
    sub_scb scb_h;

    function void reset_counts();
      scb_h.alert_count["fatal"] = 7;
    endfunction
  endclass

  class sub_scb #(type CFG_T = my_cfg) extends base_scb #(CFG_T);
    CFG_T cfg;
    function int get_fatal();
      return alert_count["fatal"];
    endfunction
  endclass
endpackage

module svfwdgeneric1;
  import svfwdgeneric1_pkg::*;

  initial begin
    automatic my_cfg cfg = new();
    automatic sub_scb scb = new();
    cfg.scb_h = scb;
    scb.cfg = cfg;
    cfg.reset_counts();
    if (scb.get_fatal() !== 7) begin
      $display("FAIL: fatal=%0d", scb.get_fatal());
      $finish;
    end
    $display("PASSED");
  end
endmodule
