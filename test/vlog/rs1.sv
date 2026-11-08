module rs1;
  int v;
  initial begin
    randsequence(main)
      main : first nothere;
      first : { v = 1; };
    endsequence

    randsequence(main)
      main : a;
      a : b a | b;
      b : { v = 2; };
    endsequence

    randsequence(main)
      main : add(1, 2) add;
      void add(int y) : { v = v + y; };
    endsequence

    randsequence(missing)
      main : first;
      first : { v = 1; };
    endsequence

    randsequence(main)
      main : rand join a b c d;
      a : { v = 1; };
      b : { v = 1; };
      c : { v = 1; };
      d : { v = 1; };
    endsequence

    randsequence(main)
      main : { v = zzz; };
    endsequence
  end
endmodule
