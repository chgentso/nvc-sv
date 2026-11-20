module svtrigfcall1;
  logic arr[0:1];
  logic sig;

  initial begin
    arr[0] = 1'b0;
    arr[1] = 1'b0;
  end

  initial assign sig = ($sformatf("%p", arr) != "");

  initial begin
    #1;
    if (sig !== 1'b1) $fatal(1, "expected sig=1");
    force arr[0] = 1'b1;
    #1;
    if (arr[0] !== 1'b1 || sig !== 1'b1) $fatal(1, "after force");
    release arr[0];
    #1;
    $display("PASSED");
    $finish;
  end
endmodule
