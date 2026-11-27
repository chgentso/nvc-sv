module vpi_fputc1;
  integer fd, rc, c1, c2;

  initial begin
    fd = $fopen("vpi_fputc1.tmp", "wb");
    if (fd == 0) $fatal(1, "fopen wb failed");
    rc = $fputc(8'h41, fd);
    if (rc !== 0) $fatal(1, "fputc 1 rc=%0d", rc);
    rc = $fputc(8'h7a, fd);
    if (rc !== 0) $fatal(1, "fputc 2 rc=%0d", rc);
    $fclose(fd);

    fd = $fopen("vpi_fputc1.tmp", "rb");
    if (fd == 0) $fatal(1, "fopen rb failed");
    c1 = $fgetc(fd);
    c2 = $fgetc(fd);
    $fclose(fd);

    if (c1 !== 8'h41 || c2 !== 8'h7a)
      $fatal(1, "expected 65/122, got %0d/%0d", c1, c2);
    $display("PASSED");
  end
endmodule
