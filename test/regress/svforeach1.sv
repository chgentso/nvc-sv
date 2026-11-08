module svforeach1;

  class holder;
    int data[];
    function new();
      data = new [3];
    endfunction
  endclass

  int fa[4];
  int down[7:4];
  int up[2:5];
  int m[3][2];
  bit [3:0] pk;
  bit [0:3] pu;
  int dyn[];
  int q[$];
  int errors = 0;
  int seq, count, total;
  holder h = new();

  task automatic check(string what, int got, int expected);
    if (got != expected) begin
      $display("%s: got %0d expected %0d", what, got, expected);
      errors++;
    end
  endtask

  initial begin
    // Declared direction is followed
    seq = 0;
    foreach (fa[i]) seq = seq * 10 + i;
    check("fixed", seq, 123);

    seq = 0;
    foreach (down[i]) seq = seq * 10 + i;
    check("descending", seq, 7654);

    seq = 0;
    foreach (up[i]) seq = seq * 10 + i;
    check("ascending", seq, 2345);

    seq = 0;
    foreach (pk[i]) seq = seq * 10 + i;
    check("packed descending", seq, 3210);

    seq = 0;
    foreach (pu[i]) seq = seq * 10 + i;
    check("packed ascending", seq, 123);

    // The body can use the array
    foreach (fa[i]) fa[i] = i * 3;
    total = 0;
    foreach (fa[i]) total += fa[i];
    check("sum", total, 18);

    // More than one dimension, outermost loop first
    seq = 0;
    foreach (m[i, j]) seq = seq * 10 + i * 2 + j;
    check("two dimensions", seq, 12345);

    total = 0;
    foreach (m[i, j]) total += i * 10 + j;
    check("two dimension sum", total, 63);

    // Skipped dimensions do not get a loop
    count = 0;
    foreach (m[, j]) count++;
    check("skip first", count, 2);

    count = 0;
    foreach (m[i]) count++;
    check("first only", count, 3);

    count = 0;
    foreach (m[i, ]) count++;
    check("skip second", count, 3);

    // Dynamic arrays and queues use the current size
    count = 0;
    foreach (dyn[i]) count++;
    check("empty dynamic", count, 0);

    dyn = new [5];
    foreach (dyn[i]) dyn[i] = i + 1;
    seq = 0;
    foreach (dyn[i]) seq = seq * 10 + dyn[i];
    check("dynamic", seq, 12345);

    q.push_back(7);
    q.push_back(8);
    q.push_back(9);
    seq = 0;
    foreach (q[i]) seq = seq * 10 + q[i] + i;
    check("queue", seq, 801);

    q.delete();
    count = 0;
    foreach (q[i]) count++;
    check("empty queue", count, 0);

    // Through a class handle
    foreach (h.data[i]) h.data[i] = 5 - i;
    seq = 0;
    foreach (h.data[i]) seq = seq * 10 + h.data[i];
    check("member", seq, 543);

    // Index variables are local to the loop
    foreach (fa[i]) begin
      foreach (fa[j]) begin
        if (j > i) break;
      end
    end

    if (errors == 0)
      $display("PASSED");
    else
      $display("FAILED");
  end

endmodule
