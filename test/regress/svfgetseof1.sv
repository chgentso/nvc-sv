// $fgets into a string gives an empty string at the end of the file
module svfgetseof1;
  integer fd, code, n;
  string line, all;

  initial begin
    fd = $fopen("svfgetseof1.tmp", "w");
    $fwrite(fd, "one\ntwo\n");
    $fclose(fd);

    fd = $fopen("svfgetseof1.tmp", "r");
    all = "";
    n = 0;
    while (1) begin
      code = $fgets(line, fd);
      all = {all, line};
      if (line == "") break;
      if (++n > 10) $fatal(1, "no end of file");
    end
    $fclose(fd);

    if (n != 2 || all != "one\ntwo\n") $fatal(1, "n=%0d all='%s'", n, all);
    $display("PASSED");
  end
endmodule
