module crv06;
  class C;
    rand bit [7:0] fa [6];
    rand int da [];
    rand byte data [];
    rand bit [7:0] u1, u2, u3;
    rand int m [3][2];
    constraint k1 { foreach (fa[i]) fa[i] inside {[10:20]}; }
    constraint k2 { da.size() inside {[3:8]}; foreach (da[i]) da[i] == i * 10; }
    constraint k3 { data.size() inside {[4:12]};
                    foreach (data[i]) if (i > 0) data[i] > data[i-1];
                    foreach (data[i]) data[i] >= 0; }
    constraint k4 { unique {u1, u2, u3}; u1 < 3; u2 < 3; u3 < 3; }
    constraint k5 { foreach (m[i,j]) m[i][j] == i * 10 + j; }
  endclass
  class U;
    rand bit [3:0] arr [5];
    constraint k { unique {arr}; foreach (arr[i]) arr[i] < 5; }
  endclass
  C o = new();
  U u = new();
  int errors = 0;
  int i, j;
  int sz_seen [16];
  int nsz;
  initial begin
    for (i = 0; i < 300; i++) begin
      if (o.randomize() != 1) begin errors++; $display("rand fail"); end
      else begin
        for (j = 0; j < 6; j++) if (o.fa[j] < 10 || o.fa[j] > 20) begin errors++; $display("fa"); end
        if (o.da.size() < 3 || o.da.size() > 8) begin errors++; $display("da size %0d", o.da.size()); end
        for (j = 0; j < o.da.size(); j++) if (o.da[j] != j * 10) begin errors++; $display("da val"); end
        if (o.data.size() < 16) sz_seen[o.data.size()] = 1;
        if (o.data.size() < 4 || o.data.size() > 12) begin errors++; $display("data size %0d", o.data.size()); end
        for (j = 1; j < o.data.size(); j++) if (!(o.data[j] > o.data[j-1])) begin errors++; $display("data order"); end
        for (j = 0; j < o.data.size(); j++) if (o.data[j] < 0) begin errors++; $display("data neg"); end
        if (o.u1 == o.u2 || o.u1 == o.u3 || o.u2 == o.u3) begin errors++; $display("unique"); end
        if (o.u1 > 2 || o.u2 > 2 || o.u3 > 2) begin errors++; $display("u range"); end
        for (j = 0; j < 3; j++) if (o.m[j][0] != j*10 || o.m[j][1] != j*10+1) begin errors++; $display("m"); end
      end
      if (u.randomize() != 1) begin errors++; $display("U rand fail"); end
      else begin
        for (j = 0; j < 5; j++) if (u.arr[j] > 4) begin errors++; $display("arr range"); end
        for (j = 0; j < 5; j++) if (u.arr[j] != 0 || j == 0) begin end
        if (u.arr[0] == u.arr[1] || u.arr[0] == u.arr[2] || u.arr[0] == u.arr[3] || u.arr[0] == u.arr[4] ||
            u.arr[1] == u.arr[2] || u.arr[1] == u.arr[3] || u.arr[1] == u.arr[4] ||
            u.arr[2] == u.arr[3] || u.arr[2] == u.arr[4] || u.arr[3] == u.arr[4]) begin errors++; $display("unique arr"); end
      end
    end
    nsz = 0;
    for (j = 0; j < 16; j++) if (sz_seen[j] != 0) nsz++;
    if (nsz < 4) begin errors++; $display("few sizes %0d", nsz); end
    if (errors == 0) $display("PASSED"); else $display("FAILED errors=%0d", errors);
    $finish;
  end
endmodule
