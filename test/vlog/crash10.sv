module top;
  string str = "Hello";
  byte chr1 = str.getc();
  byte chr3 = str.getc(1, 2);
  int n = str.len(1);
endmodule
