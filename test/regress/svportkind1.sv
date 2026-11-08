// IEEE 1800-2017 23.2.2.3, 23.3.3: a module or interface port may be a
// variable of any data type, here event, string, class handle, dynamic
// array and queue.  An input variable port is continuously assigned from
// its actual (23.3.3.2), an output variable port drives the actual, and a
// ref port shares the actual (23.3.3.3).
class svportkind1_c;
  int id;
  function new(int i); id = i; endfunction
endclass

interface svportkind1_if(input string tag, ref event ev);
  int hits = 0;
  always @(ev) hits++;
endinterface

module svportkind1_sub(input string s, input svportkind1_c h,
                       input int d[], input int q[$], ref event ev,
                       output string so, output int qo[$],
                       output svportkind1_c ho);
  int n_ev = 0;
  always @(ev) n_ev++;
  assign so = {s, "!"};
  always @* begin
    qo = q;
    qo.push_front(d.size());
  end
  always @(h) if (h != null) ho = new(h.id + 1);
endmodule

module svportkind1;
  string s = "abc", so;
  svportkind1_c h, ho;
  int d[];
  int q[$], qo[$];
  event e;
  int exp1[$] = '{3, 5, 6};
  int exp2[$] = '{3, 5, 6, 7};

  svportkind1_sub u(.s(s), .h(h), .d(d), .q(q), .ev(e), .so(so), .qo(qo),
                    .ho(ho));
  svportkind1_if bi(.tag(s), .ev(e));

  initial begin
    #1;
    if (u.s != "abc" || so != "abc!")
      $fatal(1, "string port: %s %s", u.s, so);
    s = "xy";
    #1;
    if (u.s != "xy" || so != "xy!" || bi.tag != "xy")
      $fatal(1, "string port update: %s %s %s", u.s, so, bi.tag);

    h = new(41);
    #1;
    if (u.h != h || ho == null || ho.id != 42)
      $fatal(1, "class handle port");

    d = new[3];
    q = '{5, 6};
    #1;
    if (u.d.size() != 3 || u.q.size() != 2 || u.q[1] != 6)
      $fatal(1, "dynamic array/queue input: %0d %0d", u.d.size(), u.q.size());
    if (qo != exp1)
      $fatal(1, "queue output %p", qo);
    q.push_back(7);
    #1;
    if (qo != exp2)
      $fatal(1, "queue output after push %p", qo);

    -> e;
    #1;
    -> u.ev;                    // Triggering through the ref port
    #1;
    if (u.n_ev != 2 || bi.hits != 2)
      $fatal(1, "event port: %0d %0d", u.n_ev, bi.hits);

    $display("PASSED");
  end
endmodule
