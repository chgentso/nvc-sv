// Associative arrays with integral, string and enum indices
typedef enum { RED, GREEN, BLUE } color_t;
typedef struct { int x; string n; } rec_t;

class C;
   int m[string];
   function void add(string k, int v);
      m[k] = v;
   endfunction
endclass

module svassoc1;
   `define CHECK(x, y) if ((x) !== (y)) begin \
      $display("FAILED line %0d: %0d !== %0d", `__LINE__, (x), (y)); \
      $finish; end

   int a[string];
   int b[int];
   string s[int];
   int w[*];
   int nest[string][int];
   int q[string][$];
   rec_t rs[int];
   int bycolor[color_t];
   int lit[string] = '{"a": 1, "c": 3, "b": 2};
   int cp[int], cp2[int];
   byte sm[byte];
   color_t c;
   C obj;
   int k, r, n;
   bit [7:0] ku;
   string ks;

   task automatic fill(ref int arr[int]);
      arr[1] = 11;
      arr[2] = 22;
   endtask

   function automatic int total(int arr[string]);
      int t = 0;
      string key;
      if (arr.first(key))
        do t += arr[key]; while (arr.next(key));
      return t;
   endfunction

   initial begin
      `CHECK(a.size(), 0);
      a["one"] = 1; a["two"] = 2; a["three"] = 3;
      `CHECK(a.size(), 3);
      `CHECK(a.num(), 3);
      `CHECK(a["two"], 2);
      `CHECK(a.exists("two"), 1);
      `CHECK(a.exists("four"), 0);
      `CHECK(a.size(), 3);

      // Traversal is in sorted order
      `CHECK(a.first(ks), 1);
      `CHECK(ks, "one");
      `CHECK(a.next(ks), 1);
      `CHECK(ks, "three");
      `CHECK(a.next(ks), 1);
      `CHECK(ks, "two");
      `CHECK(a.next(ks), 0);
      `CHECK(ks, "two");
      `CHECK(a.last(ks), 1);
      `CHECK(ks, "two");
      `CHECK(a.prev(ks), 1);
      `CHECK(ks, "three");
      ks = "p";
      `CHECK(a.next(ks), 1);   // Next key above one that does not exist
      `CHECK(ks, "three");
      a.delete("two");
      a.delete("nothing");
      `CHECK(a.size(), 2);
      `CHECK(a.exists("two"), 0);
      n = 0;
      foreach (a[i]) n += a[i];
      `CHECK(n, 4);

      b[5] = 50; b[-3] = 30; b[100] = 1000; b[0] = 7;
      `CHECK(b.size(), 4);
      `CHECK(b.first(k), 1);
      `CHECK(k, -3);
      `CHECK(b.next(k), 1);
      `CHECK(k, 0);
      `CHECK(b.next(k), 1);
      `CHECK(k, 5);
      `CHECK(b.next(k), 1);
      `CHECK(k, 100);
      `CHECK(b.next(k), 0);
      `CHECK(b.last(k), 1);
      `CHECK(k, 100);
      `CHECK(b.prev(k), 1);
      `CHECK(k, 5);
      k = 1;
      `CHECK(b.prev(k), 1);
      `CHECK(k, 0);
      `CHECK(b[77], 0);   // Reading does not create the entry
      `CHECK(b.size(), 4);
      b[5]++;
      b[5] += 10;
      `CHECK(b[5], 61);
      n = 0;
      foreach (b[i]) n += i;
      `CHECK(n, 102);
      b.delete();
      `CHECK(b.size(), 0);
      `CHECK(b.first(k), 0);

      s[2] = "two"; s[1] = "one";
      `CHECK(s[1], "one");
      `CHECK(s[2], "two");
      `CHECK(s[3], "");

      w[3] = 4; w[1] = 2;
      `CHECK(w.first(k), 1);
      `CHECK(k, 1);
      `CHECK(w[3], 4);

      nest["x"][1] = 5; nest["x"][2] = 6; nest["y"][7] = 8;
      n = 0;
      foreach (nest[i, j]) n += nest[i][j] * j;
      `CHECK(n, 5 + 12 + 56);
      `CHECK(nest.size(), 2);
      `CHECK(nest["x"].size(), 2);

      q["z"].push_back(3); q["z"].push_back(4); q["w"].push_back(9);
      `CHECK(q["z"].size(), 2);
      `CHECK(q["z"][1], 4);
      `CHECK(q.size(), 2);

      rs[5].x = 10; rs[5].n = "five"; rs[2].x = 3;
      `CHECK(rs[5].x, 10);
      `CHECK(rs[5].n, "five");
      `CHECK(rs[2].x, 3);
      `CHECK(rs.size(), 2);

      bycolor[BLUE] = 3; bycolor[RED] = 1;
      `CHECK(bycolor.first(c), 1);
      `CHECK(c, RED);
      `CHECK(bycolor.next(c), 1);
      `CHECK(c, BLUE);

      `CHECK(lit.size(), 3);
      `CHECK(lit["a"] + lit["b"] + lit["c"], 6);
      `CHECK(total(lit), 6);

      fill(cp);
      `CHECK(cp.size(), 2);
      `CHECK(cp[2], 22);
      cp2 = cp;
      cp2[3] = 33;
      `CHECK(cp.size(), 2);
      `CHECK(cp2.size(), 3);
      cp2 = '{ 7: 70 };
      `CHECK(cp2.size(), 1);
      `CHECK(cp2[7], 70);

      obj = new;
      obj.add("hi", 5);
      obj.add("there", 6);
      `CHECK(obj.m.size(), 2);
      `CHECK(obj.m["there"], 6);

      // Enough entries to exercise the search
      for (int i = 0; i < 200; i++)
        cp[i * 7 % 101] = i;
      `CHECK(cp.size(), 101);
      `CHECK(cp.first(k), 1);
      `CHECK(k, 0);
      `CHECK(cp.last(k), 1);
      `CHECK(k, 100);
      `CHECK(cp[50], 137);   // Written at i = 137

      // The index is converted to the type of the index
      sm[8'd200] = 4;
      sm[-56] = 5;
      `CHECK(sm.size(), 1);
      `CHECK(sm[-56], 5);
      `CHECK(sm.first(ku), 1);
      `CHECK(ku, 200);
      $display("PASSED");
   end
endmodule
