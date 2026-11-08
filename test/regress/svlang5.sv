// Language features found while bringing up the UVM library
package p;
  typedef enum { RED, GREEN } colour_t;

  int order[$];

  // Calls a function that writes a variable declared later
  int first = set_later();
  int later;

  function int set_later();
    later = 42;
    return 1;
  endfunction

  class base;
    virtual function string name();
      return "base";
    endfunction
  endclass

  class child extends base;
    virtual function string name();
      return "child";
    endfunction
  endclass

  class other;
  endclass

  // Calls a method that is declared later in the class
  class early;
    static int count = get_count();
    static function int get_count();
      return 7;
    endfunction
  endclass

  class reg_t #(type T = int, string NAME = "<unknown>");
    typedef reg_t #(T, NAME) this_type;
    const static string type_name = NAME;
    static function string get_name();
      return type_name;
    endfunction
  endclass

  class named;
    int a, b, c;
    function new(int a = 1, int b = 2, int c = 3);
      this.a = a;
      this.b = b;
      this.c = c;
    endfunction
    function int sum(int x, int y = 10);
      return a + b + c + x + y;
    endfunction
  endclass

  class items;
    int vals[$];
    function int total();
      int t = 0;
      foreach (this.vals[i])
        t += this.vals[i];
      return t;
    endfunction
  endclass

  class holder;
    int inner[$];
  endclass
endpackage

module svlang5;
  import p::*;

  int failed = 0;

  `define check(x, y) \
    if ((x) !== (y)) begin $display("FAILED %s", `"x`"); failed++; end

  function int minus_one();
    return -1;
  endfunction

  function int zero();
    return 0;
  endfunction

  function int deref(base b);
    // Only one side of the conditional is evaluated
    return b == null ? 0 : (b.name() == "child" ? 2 : 1);
  endfunction

  event ev;
  semaphore sem;
  string str;
  colour_t col = GREEN;
  int qsum = 0;
  process proc;
  typedef struct { int offset; string name; } setting_t;
  setting_t settings[$];

  initial begin
    automatic base b1 = new, b2;
    automatic child c1 = new;
    automatic other o1 = new, o2;
    automatic named n;
    automatic items it = new;
    automatic holder hs[$];
    automatic string joined[$];

    // Function results keep their signedness
    `check(minus_one() >= zero(), 0);

    `check(deref(null), 0);
    `check(deref(b1), 1);
    `check(deref(c1), 2);

    // Handles of different classes can be compared
    b2 = c1;
    `check(b2 == c1, 1);
    `check(b1 == c1, 0);

    // Casting to an unrelated class is not an error
    `check($cast(o2, o1), 1);

    // Static variables
    `check(later, 42);
    `check(early::count, 7);

    // Value parameters holding strings
    if (reg_t #(int, "hello")::get_name() != "hello") begin
      $display("FAILED string parameter");
      failed++;
    end
    if (reg_t #(real)::get_name() != "<unknown>") begin
      $display("FAILED string parameter default");
      failed++;
    end

    // Named arguments
    n = new(.c(30), .a(10));
    `check(n.a, 10);
    `check(n.b, 2);
    `check(n.c, 30);
    `check(n.sum(.y(1), .x(2)), 45);
    `check(n.sum(100), 152);

    // Characters of a string
    str = "abc";
    `check(str[1], 8'h62);
    str[1] = "X";
    if (str != "aXc") begin $display("FAILED putc"); failed++; end
    if (str[0] == "a") qsum++;
    `check(qsum, 1);

    qsum = 0;
    foreach (str[i])
      qsum += str[i];
    `check(qsum, 8'h61 + 8'h58 + 8'h63);

    // Non-constant number of repeats of a string
    begin
      automatic int m = 3;
      str = {m{"-"}};
      if (str != "---") begin $display("FAILED repeat"); failed++; end
    end

    // Case on strings and handles
    str = "two";
    case (str)
      "one": qsum = 1;
      "two", "2": qsum = 2;
      default: qsum = 3;
    endcase
    `check(qsum, 2);

    case (b2)
      b1: qsum = 1;
      c1: qsum = 4;
      default: qsum = 5;
    endcase
    `check(qsum, 4);

    // Without parentheses
    if (col.name != "GREEN") begin $display("FAILED enum name"); failed++; end

    // for loop with a user type
    begin
      automatic int total = 0;
      for (colour_t c = RED; c != GREEN; c = c.next)
        total++;
      `check(total, 1);
    end

    // foreach through this and an element of a queue
    it.vals = {1, 2, 3};
    `check(it.total(), 6);
    begin
      automatic holder h = new;
      h.inner = {5, 6};
      hs.push_back(h);
      qsum = 0;
      foreach (hs[0].inner[i])
        qsum += hs[0].inner[i];
      `check(qsum, 11);
    end

    // $bits of a type
    `check($bits(byte), 8);
    `check($bits(int), 32);

    // Streaming a queue of strings
    joined = {"ab", "cd"};
    str = {>>{joined}};
    if (str != "abcd") begin $display("FAILED join"); failed++; end

    // Sorting a queue of structures
    begin
      automatic setting_t s;
      s.offset = 5;
      s.name = "five";
      settings.push_back(s);
      s.offset = 1;
      s.name = "one";
      settings.push_back(s);
      settings.sort() with (item.offset);
      `check(settings[0].offset, 1);
      `check(settings[1].offset, 5);
    end

    // Variable written by a process forked with join_none
    fork
      begin
        proc = process::self();
      end
    join_none
    #0;
    `check(proc == null, 0);

    // Triggered state lasts until the end of the time step
    fork
      begin
        #1 ->ev;
      end
      begin
        #1 #0;
        `check(ev.triggered, 1);
        #1;
        `check(ev.triggered, 0);
      end
    join

    // Semaphores
    sem = new(2);
    sem.get(2);
    `check(sem.try_get(), 0);
    fork
      begin
        sem.get(1);
        qsum = 99;
      end
      begin
        #2;
        `check(qsum == 99, 0);
        sem.put(1);
      end
    join
    `check(qsum, 99);
    `check(sem.try_get(1), 0);
    sem.put(3);
    `check(sem.try_get(2), 1);

    if (failed == 0) $display("PASSED");
    $finish;
  end
endmodule
