// Non-blocking assignments to class properties and dynamic elements
class item;
  int        count;
  logic [7:0] data;
  bit [3:0]  flags;
  real       r;
  string     name;
  item       next;
  int        q[$];
  int        dyn[];
  static int total;
endclass

module svnba1;
  item a, b;
  int  q[$];
  int  dyn[];
  int  failed = 0;
  int  edges = 0;
  bit  clk = 0;

  `define check(x, y) \
    if ((x) !== (y)) begin \
      $display("FAILED %s: %0d", `"x`", (x)); failed++; \
    end

  initial begin
    wait (a != null);
    forever @(a.count) edges++;
  end

  initial begin
    #1;
    a = new;
    b = new;
    #1;

    // Values change after the active region and reads see the old value
    a.count = 1;
    a.count <= 2;
    `check(a.count, 1);
    #0;
    `check(a.count, 1);
    @(a.count);
    `check(a.count, 2);

    // Swap
    a.count = 10;
    b.count = 20;
    a.count <= b.count;
    b.count <= a.count;
    #1;
    `check(a.count, 20);
    `check(b.count, 10);

    // The last assignment wins
    a.count <= 3;
    a.count <= 4;
    #1;
    `check(a.count, 4);

    // Part selects merge and all are kept
    a.data = 8'h00;
    a.data[0] <= 1'b1;
    a.data[7] <= 1'b1;
    a.data[4:1] <= 4'b1010;
    #1;
    `check(a.data, 8'b1001_0101);
    a.data[3] <= 1'bx;
    #1;
    `check(a.data[3], 1'bx);
    `check(a.data[2:0], 3'b101);

    // Local variables of a process are static
    begin
      int x;
      x = 1;
      x <= 2;
      #1;
      `check(x, 2);
    end

    // Variable index evaluated at the time of the assignment
    begin
      static int i = 1;
      a.flags = 0;
      a.flags[i] <= 1;
      i = 2;
      #1;
      `check(a.flags, 4'b0010);
    end

    // Other types
    a.r <= 2.5;
    a.name <= "hello";
    a.next <= b;
    #1;
    if (a.r != 2.5) begin $display("FAILED r"); failed++; end
    if (a.name != "hello") begin $display("FAILED name"); failed++; end
    if (a.next != b) begin $display("FAILED next"); failed++; end

    // The right hand side is saved
    begin
      static string s = "first";
      a.name <= s;
      s = "second";
      #1;
      if (a.name != "first") begin $display("FAILED s"); failed++; end
    end

    // The target object is chosen at the time of the assignment
    begin
      automatic item keep = a;
      a.count = 0;
      a.count <= 99;
      a = b;
      #1;
      `check(keep.count, 99);
      a = keep;
    end

    // The object stays alive if nothing else refers to it
    begin
      automatic item tmp = new;
      a.next = tmp;
      tmp.count <= 5;
      tmp = null;
      a.next.count = 1;
      #1;
      `check(a.next.count, 5);
    end

    // Intra-assignment delay
    a.count = 0;
    a.count <= #5 7;
    #4;
    `check(a.count, 0);
    #2;
    `check(a.count, 7);

    // Pending assignments keep their order across delays
    a.count <= #2 1;
    a.count <= #2 2;
    #3;
    `check(a.count, 2);

    // Concatenation
    {a.flags, a.data} <= 12'habc;
    #1;
    `check(a.flags, 4'ha);
    `check(a.data, 8'hbc);

    // Queues, dynamic arrays and properties holding them
    q = {1, 2, 3};
    q[1] <= 20;
    `check(q[1], 2);
    #1;
    `check(q[1], 20);
    dyn = new[2];
    dyn[0] <= 5;
    #1;
    `check(dyn[0], 5);
    a.q <= {4, 5};
    #1;
    `check(a.q.size(), 2);
    `check(a.q[1], 5);
    a.dyn = new[3];
    a.dyn[2] <= 8;
    #1;
    `check(a.dyn[2], 8);

    // Static property
    item::total <= 11;
    #1;
    `check(item::total, 11);

    `check(edges > 0, 1);

    if (failed == 0) $display("PASSED");
    $finish;
  end
endmodule
