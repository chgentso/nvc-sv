package svfwdclass1_pkg;
  typedef class scoreboard_c;

  class env_cfg_c;
    scoreboard_c scb_h;

    function void clear_alerts();
      scb_h.alert_count["fatal"] = 0;
      scb_h.alert_count["recov"] = 5;
      scb_h.bump("recov");
    endfunction
  endclass

  class scoreboard_c;
    int alert_count[string];

    function void bump(string key);
      alert_count[key] += 2;
    endfunction
  endclass
endpackage

module svfwdclass1;
  import svfwdclass1_pkg::*;

  initial begin
    automatic env_cfg_c cfg = new();
    automatic scoreboard_c scb = new();
    cfg.scb_h = scb;
    cfg.clear_alerts();
    if (scb.alert_count["fatal"] !== 0 || scb.alert_count["recov"] !== 7)
      $fatal(1, "fatal=%0d recov=%0d", scb.alert_count["fatal"], scb.alert_count["recov"]);
    $display("PASSED");
    $finish;
  end
endmodule
