module top;
  reg [7:0] a[3:0], b[3:0];

  always for (int i = 0; i < 4; i++) b[i] = a[i];

  always begin
    #1 a[0] = 1;
  end

  always if (a[0] == 2) a[1] = 3;
endmodule
