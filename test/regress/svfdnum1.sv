// File descriptors returned by $fopen with a type argument, reuse after $fclose, access
// through a descriptor, standard output descriptor, and failed open.
// Expected values: IEEE 1800-2017 21.3.1 ($fopen with a type returns a file descriptor
// with bit 31 set, 32'h8000_0001 is standard output; a failed open returns 0), 21.3.4.1
// ($fgetc), 21.3.2 ($fdisplay), 21.3.1 ($fclose).  The numeric values of the descriptors
// and whether a closed descriptor is reused are implementation defined and not checked.
`define CHK(c, m) if (!(c)) begin err++; $error(m); end
module svfdnum1;
   integer f1, f2, f3, c, f4;
   int err = 0;
   initial begin
      f1 = $fopen("svfdnum11.tmp", "w");
      f2 = $fopen("svfdnum12.tmp", "w");
      `CHK(f1 != 0 && f2 != 0, "open succeeded")
      `CHK(f1[31] === 1'b1 && f2[31] === 1'b1, "descriptors have bit 31 set")
      `CHK(f1 !== f2, "open files have different descriptors")
      $fclose(f1);
      f3 = $fopen("svfdnum13.tmp", "w");
      `CHK(f3 != 0 && f3[31] === 1'b1, "third open")
      `CHK(f3 !== f2, "f3 differs from the still open f2")
      $fdisplay(f2, "x"); $fdisplay(f3, "y");
      $fclose(f2); $fclose(f3);
      f1 = $fopen("svfdnum12.tmp", "r");
      `CHK(f1 != 0, "reopen for reading")
      c = $fgetc(f1);
      `CHK(c == 120, "first byte of file 2 is x")
      $fclose(f1);
      f4 = $fopen("svfdnum13.tmp", "r");
      c = $fgetc(f4);
      `CHK(c == 121, "first byte of file 3 is y")
      $fclose(f4);
      $fdisplay(32'h80000001, "to stdout");
      `CHK($fopen("nonexistent/none", "r") == 0, "failed open returns 0")
      if (err == 0) $display("PASSED");
   end
endmodule
