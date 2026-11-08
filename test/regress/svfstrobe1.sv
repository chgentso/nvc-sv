// IEEE 1800-2017 21.3.2: $fstrobe, $fstrobeb, $fstrobeh and $fstrobeo
// write to a file like $strobe (21.2.2), with the values at the end of
// the time step and the default radix of their suffix.
`timescale 1ns/1ns
module svfstrobe1;
  int fd, err = 0;
  logic [7:0] a = 1;
  string l;
  initial begin
    fd = $fopen("svfstrobe1.txt", "w");
    $fstrobe(fd, "d a=%0d", a);
    $fstrobeh(fd, "h ", a);
    $fstrobeb(fd, "b ", a);
    $fstrobeo(fd, "o ", a);
    a = 8'h2c;
    #1 $fclose(fd);
    fd = $fopen("svfstrobe1.txt", "r");
    if ($fgets(l, fd) == 0 || l != "d a=44\n") err++;
    if ($fgets(l, fd) == 0 || l != "h 2c\n") err++;
    if ($fgets(l, fd) == 0 || l != "b 00101100\n") err++;
    if ($fgets(l, fd) == 0 || l != "o 054\n") err++;
    if (err == 0) $display("PASSED");
    else $display("FAILED %0d", err);
  end
endmodule
