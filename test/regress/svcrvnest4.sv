// Array of queues of unpacked structures with a dist whose weights depend
// on the position in the fixed size dimensions
typedef enum bit [3:0] { F = 4'h9, T = 4'h6 } mubi_t;
typedef struct { rand mubi_t en; rand mubi_t he; rand int v; int fixed; } cfg_t;

class c;
  int pc[2][3] = '{'{0, 50, 100}, '{100, 50, 0}};
  int sz[3] = '{1, 2, 3};
  rand cfg_t pages[2][3][$];
  constraint k {
    foreach (pages[i, j]) pages[i][j].size() == sz[j];
    foreach (pages[i, j, l]) {
      pages[i][j][l].en dist { F :/ (100 - pc[i][j]), T :/ pc[i][j] };
      pages[i][j][l].he == (i == 0 ? T : F);
      pages[i][j][l].v inside {[l * 10 : l * 10 + 5]};
    }
  }
endclass

module svcrvnest4;
  c o;
  int bad, nt;
  initial begin
    o = new;
    for (int n = 0; n < 100; n++) begin
      if (!o.randomize()) bad++;
      for (int i = 0; i < 2; i++)
        for (int j = 0; j < 3; j++) begin
          if (o.pages[i][j].size() != o.sz[j]) bad++;
          foreach (o.pages[i][j][l]) begin
            if (o.pages[i][j][l].en != F && o.pages[i][j][l].en != T) bad++;
            if (o.pc[i][j] == 0 && o.pages[i][j][l].en != F) bad++;
            if (o.pc[i][j] == 100 && o.pages[i][j][l].en != T) bad++;
            if (o.pages[i][j][l].he != (i == 0 ? T : F)) bad++;
            if (o.pages[i][j][l].v < l * 10 || o.pages[i][j][l].v > l * 10 + 5)
              bad++;
            nt += o.pages[i][j][l].en == T;
          end
        end
    end
    // The weighted choices between 0 and 100 percent
    if (nt < 400 || nt > 800) bad++;
    if (bad == 0) $display("PASSED");
    else $display("FAILED %0d", bad);
  end
endmodule
