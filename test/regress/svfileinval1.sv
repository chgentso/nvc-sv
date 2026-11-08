// File operations on a descriptor that is zero, not open, or closed.
// Expected values: IEEE 1800-2017 21.3.1 ($fopen returns 0 on failure), 21.3.4.1 ($fgetc
// returns EOF = -1 on error; $ungetc returns EOF on error), 21.3.4.2 / 21.3.4.3 ($fgets
// and $fread return 0 on error), 21.3.5 ($ftell returns -1 on error; $fseek and $rewind
// return -1 on error), 21.3.4.3 ($fscanf returns EOF on error), 21.3.6 ($feof: an unusable
// stream is at its end, non-zero, so a
// while (!$feof(fd)) loop ends).  $fflush, $fdisplay, $fwrite and
// $fclose on such a descriptor must not stop the simulation.  Diagnostics go to stderr
// and are not checked.
`define CHK(c, m) if (!(c)) begin err++; $error(m); end
module svfileinval1;
   integer f, n, r, c;
   bit [31:0] word;
   reg [7:0] b;
   string s;
   int x;
   int err = 0;
   initial begin
      f = $fopen("does_not_exist.bin", "r");
      `CHK(f == 0, "failed open returns 0")
      r = $feof(f); `CHK(r != 0, "feof on fd 0")
      n = $fread(word, f); `CHK(n == 0, "fread on fd 0")
      c = $fgetc(f); `CHK(c == -1, "fgetc on fd 0")
      c = $fgets(s, f); `CHK(c == 0, "fgets on fd 0")
      c = $fscanf(f, "%d", x); `CHK(c == -1, "fscanf on fd 0")
      c = $ungetc(65, f); `CHK(c == -1, "ungetc on fd 0")
      c = $ftell(f); `CHK(c == -1, "ftell on fd 0")
      c = $fseek(f, 0, 0); `CHK(c == -1, "fseek on fd 0")
      c = $rewind(f); `CHK(c == -1, "rewind on fd 0")
      $fflush(f);
      $fdisplay(f, "hello");
      $fwrite(f, "hello");
      $fclose(f);
      // 12345 is a descriptor that was never returned by $fopen
      r = $feof(12345); `CHK(r != 0, "feof on a bad number")
      n = $fread(word, 12345); `CHK(n == 0, "fread on a bad number")
      c = $fgetc(12345); `CHK(c == -1, "fgetc on a bad number")
      $fclose(12345);
      f = $fopen("svfileinval1.tmp", "w"); $fwrite(f, "ab"); $fclose(f);
      f = $fopen("svfileinval1.tmp", "r"); $fclose(f);
      r = $feof(f); `CHK(r != 0, "feof on a closed descriptor")
      c = $fgetc(f); `CHK(c == -1, "fgetc on a closed descriptor")
      n = $fread(word, f); `CHK(n == 0, "fread on a closed descriptor")
      $fclose(f);
      if (err == 0) $display("PASSED");
   end
endmodule
