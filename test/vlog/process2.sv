module process2;
  process p;
  int i;

  initial begin
    p.kill(1);
    p = 5;
    i = process::self();
    p = process::self();
    i = p.status();
  end
endmodule
