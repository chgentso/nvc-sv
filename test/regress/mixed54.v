module sub;
  parameter FILENAME = "";
  parameter unused = 5;
  initial begin
    if (FILENAME != "") $fatal(1, "default not used");
    $display("sub %0d", unused);
  end
endmodule
