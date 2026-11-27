module vlog_procstate1;
  process p1, p2;
  initial begin
    fork
      begin p1 = process::self(); #5; end
      begin p2 = process::self(); end
    join_none
    #1 $display("%s %s", p1.status().name(), p2.status().name());
    p1.suspend();
    #0 $display("%s", p1.status().name());
    p1.kill();
    #0 $display("%s", p1.status().name());
    begin
      automatic process me = process::self();
      $display("%s", me.status().name());
    end
  end
endmodule
