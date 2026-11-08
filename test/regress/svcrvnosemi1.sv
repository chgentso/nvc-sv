module svcrvnosemi1;
  class c;
    rand bit [3:0] a[3];
    bit [3:0] b[3] = '{1, 2, 3};
    constraint k { foreach(a[i]) { a[i] == b[i]};
                   foreach(a[i]) { a[i] < 9 }; }
  endclass
  initial begin
    automatic c o = new;
    if (o.randomize() && o.a[0] == 1 && o.a[1] == 2 && o.a[2] == 3)
      $display("PASSED");
    else
      $display("FAILED");
    if (o.randomize() with { foreach(a[i]) { a[i] > 0}; foreach(a[i]) { a[i] < 4 }; }
        && o.a[0] == 1 && o.a[1] == 2 && o.a[2] == 3)
      $display("PASSED");
    else
      $display("FAILED");
  end
endmodule
