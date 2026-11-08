// Class handles as the index of an associative array
class k;
  int id;
  function new(int i);
    id = i;
  endfunction
endclass

module svassoc3;
  int failed = 0;

  `define check(x, y) \
    if ((x) !== (y)) begin $display("FAILED %s", `"x`"); failed++; end

  int m[k];
  k   n[k];

  initial begin
    automatic k a = new(1), b = new(2), c = new(3), i;
    automatic int sum = 0;

    m[a] = 10;
    m[b] = 20;
    n[a] = b;

    `check(m.size(), 2);
    `check(m[a], 10);
    `check(m[b], 20);
    `check(m.exists(c), 0);
    `check(m.exists(a), 1);
    `check(n[a].id, 2);

    foreach (m[j]) begin
      sum += m[j] + j.id;
    end
    `check(sum, 33);

    sum = 0;
    if (m.first(i)) begin
      do
        sum += i.id;
      while (m.next(i));
    end
    `check(sum, 3);

    m.delete(a);
    `check(m.size(), 1);
    `check(m.exists(a), 0);

    if (failed == 0) $display("PASSED");
    $finish;
  end
endmodule
