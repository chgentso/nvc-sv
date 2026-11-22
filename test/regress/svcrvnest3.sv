// A two level fixed size array of dynamic arrays of integers with an
// element constraint that relates neighbours and a conditional size
class c;
  rand int unsigned a[2][2][];
  rand bit big;
  constraint sz {
    foreach (a[i, j]) a[i][j].size() == (big ? 4 : 2) + i + j;
    foreach (a[i, j, k]) {
      a[i][j][k] < 1000;
      if (k > 0) a[i][j][k] > a[i][j][k - 1];
    }
  }
endclass

module svcrvnest3;
  c o;
  int bad, nbig;
  initial begin
    o = new;
    for (int n = 0; n < 40; n++) begin
      if (!o.randomize()) bad++;
      nbig += o.big;
      for (int i = 0; i < 2; i++)
        for (int j = 0; j < 2; j++) begin
          if (o.a[i][j].size() != (o.big ? 4 : 2) + i + j) bad++;
          foreach (o.a[i][j][k]) begin
            if (o.a[i][j][k] >= 1000) bad++;
            if (k > 0 && o.a[i][j][k] <= o.a[i][j][k - 1]) bad++;
          end
        end
    end
    if (nbig == 0 || nbig == 40) bad++;
    if (bad == 0) $display("PASSED");
    else $display("FAILED %0d", bad);
  end
endmodule
