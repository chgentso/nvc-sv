// IEEE 1800-2017 7.4.2: the size of an unpacked array dimension is positive.
module dimsize1;
  int array_bad[0];                             // Error
  int array2_bad[-1];                           // Error
  int ok[1];                                    // OK
  int ok2[0:0];                                 // OK
endmodule
