module process1;
  process p;
  process q = new;
  int i;

  initial begin
    p.frobnicate();
    i = process::bogus;
  end
endmodule
