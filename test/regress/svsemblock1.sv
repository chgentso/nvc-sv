module svsemblock1;
  function automatic int check_block_decl();
    begin
      semaphore sem = new(2);
      mailbox #(int) mb = new();
      int v;
      if (!sem.try_get(1))
        return 1;
      if (!sem.try_get(1))
        return 2;
      if (sem.try_get(1))
        return 3;
      sem.put(1);
      if (!sem.try_get(1))
        return 4;
      if (!mb.try_put(42))
        return 5;
      if (!mb.try_get(v) || v !== 42)
        return 6;
      return 0;
    end
  endfunction

  initial begin
    automatic int rc = check_block_decl();
    if (rc !== 0)
      $fatal(1, "check_block_decl failed rc=%0d", rc);
    $display("PASSED");
    $finish;
  end
endmodule
