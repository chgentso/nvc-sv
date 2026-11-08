// A small utility library of parameterised classes: containers, comparator
// policy classes, iterators through interface classes, static factory
// methods and generic algorithms
// Expected values: IEEE 1800-2017 8.25 (parameterised classes; each specialisation has its own
// static members), 8.26 (interface classes), 6.19/20.6.1 ($typename; whitespace is not
// compared), 8.7, 7.10 (queues), 7.5 (dynamic arrays), 6.16 (string compare: only the sign is
// defined), 21.2 ($display of reals). All results are derived by hand from the algorithms
// (binary heap order, FIFO ring, stack LIFO order).
package util_pkg;
  interface class comparable #(type T = int);
    pure virtual function int compare(T a, T b);
  endclass

  class int_asc implements comparable #(int);
    virtual function int compare(int a, int b); return a < b ? -1 : a > b ? 1 : 0; endfunction
  endclass
  class int_desc implements comparable #(int);
    virtual function int compare(int a, int b); return b < a ? -1 : b > a ? 1 : 0; endfunction
  endclass
  class str_len implements comparable #(string);
    virtual function int compare(string a, string b);
      return a.len() < b.len() ? -1 : a.len() > b.len() ? 1 : a.compare(b);
    endfunction
  endclass

  interface class iterator #(type T = int);
    pure virtual function bit has_next();
    pure virtual function T next();
  endclass

  class stack #(type T = int, int DEPTH = 8);
    T items[$];
    static int instances;
    function new(); instances++; endfunction
    function bit push(T v);
      if (items.size() == DEPTH) return 0;
      items.push_back(v);
      return 1;
    endfunction
    function T pop(); return items.pop_back(); endfunction
    function T peek(); return items[$]; endfunction
    function int size(); return items.size(); endfunction
    static function string describe();
      return $sformatf("stack#(%s,%0d)", $typename(T), DEPTH);
    endfunction

    class iter implements iterator #(T);
      local stack #(T, DEPTH) s;
      local int pos;
      function new(stack #(T, DEPTH) s); this.s = s; pos = s.items.size() - 1; endfunction
      virtual function bit has_next(); return pos >= 0; endfunction
      virtual function T next(); T v = s.items[pos]; pos--; return v; endfunction
    endclass
    function iter get_iter(); iter it = new(this); return it; endfunction
  endclass

  class heap #(type T = int, type CMP = int_asc);
    local T data[$];
    local CMP cmp;
    function new(); cmp = new; endfunction
    local function void swap(int i, int j); T t = data[i]; data[i] = data[j]; data[j] = t; endfunction
    function void insert(T v);
      int i;
      data.push_back(v);
      i = data.size() - 1;
      while (i > 0 && cmp.compare(data[i], data[(i - 1) / 2]) < 0) begin
        swap(i, (i - 1) / 2);
        i = (i - 1) / 2;
      end
    endfunction
    function T extract();
      T top = data[0];
      int i = 0;
      data[0] = data[data.size() - 1];
      void'(data.pop_back());
      forever begin
        int l = 2 * i + 1, r = 2 * i + 2, m = i;
        if (l < data.size() && cmp.compare(data[l], data[m]) < 0) m = l;
        if (r < data.size() && cmp.compare(data[r], data[m]) < 0) m = r;
        if (m == i) break;
        swap(i, m);
        i = m;
      end
      return top;
    endfunction
    function int size(); return data.size(); endfunction
  endclass

  class ring #(type T = byte, int N = 4);
    protected T buf_[N];
    protected int head, count;
    function bit put(T v);
      if (count == N) return 0;
      buf_[(head + count) % N] = v;
      count++;
      return 1;
    endfunction
    function bit get(output T v);
      if (count == 0) return 0;
      v = buf_[head];
      head = (head + 1) % N;
      count--;
      return 1;
    endfunction
  endclass

  class pair #(type A = int, type B = A);
    A first; B second;
    function new(A a, B b); first = a; second = b; endfunction
    function pair #(B, A) swap(); pair #(B, A) p = new(second, first); return p; endfunction
  endclass

  class algo #(type T = int);
    typedef T arr_t[];
    static function T max_of(arr_t a);
      T m = a[0];
      foreach (a[i]) if (a[i] > m) m = a[i];
      return m;
    endfunction
    static function arr_t map_add(arr_t a, T k);
      arr_t r = new[a.size()];
      foreach (a[i]) r[i] = a[i] + k;
      return r;
    endfunction
  endclass

  class fact #(int N = 5);
    static function int value();
      int r = 1;
      for (int i = 2; i <= N; i++) r *= i;
      return r;
    endfunction
  endclass
endpackage

module svsyn7;
  import util_pkg::*;
  int errs;
  task automatic ck(int got, int exp, string what);
    if (got !== exp) begin $error("%s: got %0d expected %0d", what, got, exp); errs++; end
  endtask
  function automatic string no_ws(string x);
    string r = "";
    for (int i = 0; i < x.len(); i++) if (x[i] != " ") r = {r, string'(x[i])};
    return r;
  endfunction
  typedef stack #(int, 3) stack3_t;
  initial begin
    static stack3_t s3 = new;
    automatic stack #(string) ss = new;
    automatic stack #(bit [7:0], 2) sb = new;
    automatic heap #(int) h1 = new;
    automatic heap #(int, int_desc) h2 = new;
    automatic heap #(string, str_len) h3 = new;
    automatic ring #(int, 3) r = new;
    automatic pair #(int, string) p = new(7, "seven");
    automatic pair #(string, int) q = p.swap();
    iterator #(int) it;
    int v;
    automatic int ra[] = '{3, 9, 2, 7};
    int mapped[];
    int order[$], asc[$], desc[$], rest[$];
    string lens[$];
    string s1, s2, s3d, pa, pb;
    automatic real rr[] = '{1.5, 0.5, 2.25};

    // $typename text is checked without whitespace
    s1 = no_ws(stack3_t::describe()); s2 = no_ws(stack #(string)::describe()); s3d = no_ws(stack #(bit [7:0], 2)::describe());
    if (s1 != "stack#(int,3)" || s2 != "stack#(string,8)" || s3d != "stack#(bit[7:0],2)") begin
      $error("describe: '%s' '%s' '%s'", s1, s2, s3d); errs++;
    end
    // stack of depth 3
    ck(s3.push(1), 1, "push 1"); ck(s3.push(2), 1, "push 2"); ck(s3.push(3), 1, "push 3");
    ck(s3.push(4), 0, "push 4 (full)");
    it = s3.get_iter();
    while (it.has_next()) order.push_back(it.next());
    if (order.size() != 3 || order[0] != 3 || order[1] != 2 || order[2] != 1) begin $error("iteration order"); errs++; end
    ck(s3.peek(), 3, "top"); ck(s3.size(), 3, "size");
    void'(ss.push("a")); void'(ss.push("bb"));
    pa = ss.pop(); pb = ss.pop();
    if (pa != "bb" || pb != "a") begin $error("string stack pops '%s' '%s'", pa, pb); errs++; end
    ck(stack3_t::instances, 1, "instances int,3"); ck(stack #(string)::instances, 1, "instances string");
    void'(sb.push(8'hab)); void'(sb.push(8'hcd));
    ck(sb.push(8'hef), 0, "byte stack full");
    ck(sb.pop(), 8'hcd, "byte stack pop");

    foreach (ra[i]) begin h1.insert(ra[i]); h2.insert(ra[i]); end
    while (h1.size() > 0) asc.push_back(h1.extract());
    while (h2.size() > 0) desc.push_back(h2.extract());
    if (asc.size() != 4 || asc[0] != 2 || asc[1] != 3 || asc[2] != 7 || asc[3] != 9) begin $error("ascending heap"); errs++; end
    if (desc.size() != 4 || desc[0] != 9 || desc[1] != 7 || desc[2] != 3 || desc[3] != 2) begin $error("descending heap"); errs++; end
    h3.insert("pear"); h3.insert("fig"); h3.insert("banana"); h3.insert("kiwi"); h3.insert("apple");
    while (h3.size() > 0) lens.push_back(h3.extract());
    begin
      automatic string exp_l[$] = '{"fig", "kiwi", "pear", "apple", "banana"};
      if (lens.size() != 5) begin $error("length heap size"); errs++; end
      else foreach (lens[i]) if (lens[i] != exp_l[i]) begin $error("length heap[%0d] = %s", i, lens[i]); errs++; end
    end

    ck(r.put(1), 1, "ring put 1"); ck(r.put(2), 1, "ring put 2"); ck(r.put(3), 1, "ring put 3");
    ck(r.put(4), 0, "ring full");
    void'(r.get(v)); ck(v, 1, "ring first get");
    void'(r.put(5));
    while (r.get(v)) rest.push_back(v);
    if (rest.size() != 3 || rest[0] != 2 || rest[1] != 3 || rest[2] != 5) begin $error("ring remaining"); errs++; end

    if (p.first != 7 || p.second != "seven" || q.first != "seven" || q.second != 7) begin $error("pair swap"); errs++; end
    mapped = algo #(int)::map_add(ra, 10);
    if (mapped.size() != 4 || mapped[0] != 13 || mapped[1] != 19 || mapped[2] != 12 || mapped[3] != 17) begin $error("map_add"); errs++; end
    ck(algo #(int)::max_of(ra), 9, "max int");
    if (!(algo #(real)::max_of(rr) == 2.25)) begin $error("max real"); errs++; end
    ck(fact#()::value(), 120, "fact 5"); ck(fact#(6)::value(), 720, "fact 6");
    if (errs == 0) $display("PASSED");
  end
endmodule
