// Left justified display fields and $fclose of a descriptor that is not open

module svfmtleft1;
  int fd = -1;
  initial begin
    $display("[%-02d] [%-5d] [%-3d] [%-6s] [%-4h]", 2, 7, 12345, "ab", 8'h1f);
    $fclose(fd);
    $display("PASSED");
  end
endmodule
