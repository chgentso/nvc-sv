// Unformatted %u (2-state) and %z (4-state) binary format specifiers
// IEEE 1800-2017 21.2.1.1
module svfmtbin1;
  logic [31:0] v32;
  logic [63:0] v64;
  int fd;
  int buf_u [0:11];
  int buf_z [0:7];

  initial begin
    v32 = 32'h1234_5678;
    v64 = 64'h89ab_cdef_0123_4567;

    fd = $fopen("svfmtbin1.bin", "wb");
    if (fd == 0) $fatal(1, "failed to open output");
    $fwrite(fd, "%u%u", v32, v64);
    $fclose(fd);

    fd = $fopen("svfmtbin1.bin", "rb");
    if (fd == 0) $fatal(1, "failed to open input");
    for (int i = 0; i < 12; i++)
      buf_u[i] = $fgetc(fd);
    if ($fgetc(fd) !== -1)
      $fatal(1, "expected EOF after 12 bytes");
    $fclose(fd);

    // Native little-endian 32-bit chunks, LSB word first
    if (buf_u[0] !== 8'h78 || buf_u[1] !== 8'h56 || buf_u[2] !== 8'h34 || buf_u[3] !== 8'h12)
      $fatal(1, "v32 mismatch: %h %h %h %h", buf_u[0], buf_u[1], buf_u[2], buf_u[3]);
    if (buf_u[4] !== 8'h67 || buf_u[5] !== 8'h45 || buf_u[6] !== 8'h23 || buf_u[7] !== 8'h01)
      $fatal(1, "v64 lo mismatch: %h %h %h %h", buf_u[4], buf_u[5], buf_u[6], buf_u[7]);
    if (buf_u[8] !== 8'hef || buf_u[9] !== 8'hcd || buf_u[10] !== 8'hab || buf_u[11] !== 8'h89)
      $fatal(1, "v64 hi mismatch: %h %h %h %h", buf_u[8], buf_u[9], buf_u[10], buf_u[11]);

    // %z writes aval then bval (32 bits each)
    v32 = 32'h0000_000x;
    fd = $fopen("svfmtbin1.bin", "wb");
    $fwrite(fd, "%z", v32);
    $fclose(fd);

    fd = $fopen("svfmtbin1.bin", "rb");
    for (int i = 0; i < 8; i++)
      buf_z[i] = $fgetc(fd);
    if ($fgetc(fd) !== -1)
      $fatal(1, "expected EOF after 8 bytes");
    $fclose(fd);

    if (buf_z[0] !== 8'h0f || buf_z[1] !== 8'h00 || buf_z[4] !== 8'h0f || buf_z[5] !== 8'h00)
      $fatal(1, "v32 %%z mismatch: a0=%h a1=%h b0=%h b1=%h",
             buf_z[0], buf_z[1], buf_z[4], buf_z[5]);

    $display("PASSED");
  end
endmodule
