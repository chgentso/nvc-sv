// An unpacked array parameter connected to an input port
parameter int Attr[1] = '{1};

module svparamport1_sub (input int a[1], input int b[2]);
  initial begin
    #1;
    if (a[0] != 1 || b[0] != 3 || b[1] != 4)
      $fatal(1, "a=%0d b=%0d %0d", a[0], b[0], b[1]);
    $display("PASSED");
  end
endmodule

module svparamport1;
  parameter int P[2] = '{3, 4};
  svparamport1_sub u (.a(Attr), .b(P));
endmodule
