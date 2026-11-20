module svprockill1;
  process fin;
  bit orphan_ran = 0;

  initial begin
    fork
      begin
        fin = process::self();
        fork
          begin
            #10;
            orphan_ran = 1;
          end
        join_none
      end
    join_none

    #1;
    if (fin.status() != process::FINISHED) $fatal(1, "expected FINISHED");
    fin.kill();
    if (fin.status() != process::FINISHED) $fatal(1, "status changed after kill");

    #20;
    if (orphan_ran !== 0) $fatal(1, "orphan descendant was not killed");
    $display("PASSED");
    $finish;
  end
endmodule
