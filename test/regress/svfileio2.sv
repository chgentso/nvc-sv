// IEEE 1800-2017 21.3.1: $fopen without a type opens the file for writing
// and returns a multichannel descriptor with a single bit set in 1..30;
// bit 0 is standard output and OR-ed descriptors write to every channel.
// "w+" truncates or creates a file for update and "a+" appends to it.
// 21.3.7: $ferror writes a description of the error to its string.
// Where the LRM is silent: $feof is non-zero for an invalid descriptor
// (an unusable stream is at its end), $fclose of a closed descriptor
// warns, and descriptors are shown in hex as they are bit patterns.
module svfileio2;
  int m1, m2, fd, err = 0, e;
  string s, str;

  function automatic int count_bits(int v);
    return $countones(v);
  endfunction

  initial begin
    m1 = $fopen("svfileio2_a.txt");
    m2 = $fopen("svfileio2_b.txt");
    if (m1 == 0 || m2 == 0 || m1[31] || m2[31] || m1[0] || m2[0]
        || count_bits(m1) != 1 || count_bits(m2) != 1 || m1 == m2) err++;
    $fdisplay(m1 | m2, "both");
    $fwrite(m2, "b only\n");
    $fclose(m1 | m2);

    fd = $fopen("svfileio2_a.txt", "r");
    if (fd == 0 || !fd[31]) err++;
    if ($fgets(s, fd) == 0 || s != "both\n") err++;
    if ($fgets(s, fd) != 0) err++;
    if ($feof(fd) == 0) err++;
    $fclose(fd);

    fd = $fopen("svfileio2_b.txt", "r");
    if ($fgets(s, fd) == 0 || s != "both\n") err++;
    if ($fgets(s, fd) == 0 || s != "b only\n") err++;
    $fclose(fd);

    fd = $fopen("svfileio2_c.txt", "w+");
    if (fd == 0) err++;
    $fwrite(fd, "first\n");
    $rewind(fd);
    if ($fgets(s, fd) == 0 || s != "first\n") err++;
    $fclose(fd);

    fd = $fopen("svfileio2_c.txt", "a+");
    if (fd == 0) err++;
    $fwrite(fd, "second\n");
    $rewind(fd);
    if ($fgets(s, fd) == 0 || s != "first\n") err++;
    if ($fgets(s, fd) == 0 || s != "second\n") err++;
    $fclose(fd);

    // Closed descriptor
    $fclose(fd);
    if ($feof(fd) == 0) err++;
    e = $ferror(fd, str);
    if (e == 0 || str == "") err++;

    fd = $fopen("svfileio2_nonexistent/x.txt", "r");
    if (fd != 0) err++;
    e = $ferror(fd, str);
    if (e == 0 || str == "") err++;

    if (err == 0) $display("PASSED");
    else $display("FAILED %0d", err);
  end
endmodule
