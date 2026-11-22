// Constraints on a member of the elements of a two level array of queues
// of packed structures
typedef struct packed { bit [1:0] en; bit rd; } cfg_t;
class c;
  rand cfg_t pages[2][3][$];
  constraint e {
    foreach (pages[i, j]) pages[i][j].size() == j + 1;
    foreach (pages[i, j, k]) {
      pages[i][j][k].en == 2'(i + 1);
      pages[i][j][k].rd == (j == 2);
    }
  }
endclass

module svcrvnest2;
  c o;
  int bad;
  initial begin
    o = new;
    for (int n = 0; n < 20; n++) begin
      if (!o.randomize()) bad++;
      for (int i = 0; i < 2; i++)
        for (int j = 0; j < 3; j++) begin
          if (o.pages[i][j].size() != j + 1) bad++;
          foreach (o.pages[i][j][k]) begin
            if (o.pages[i][j][k].en != 2'(i + 1)) bad++;
            if (o.pages[i][j][k].rd != (j == 2)) bad++;
          end
        end
    end
    if (bad == 0) $display("PASSED");
    else $display("FAILED %0d", bad);
  end
endmodule
