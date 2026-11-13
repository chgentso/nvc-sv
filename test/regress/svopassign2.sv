// Self-checking. IEEE 1800-2017 11.4.1: a compound assignment evaluates the
// indices of its target once.
module svopassign2;
  reg [7:0] words[0:1];
  integer word, base, word_calls, base_calls;
  int q[$];

  function integer choose_word(input integer unused);
    word_calls = word_calls + 1;
    choose_word = word;
  endfunction

  function integer choose_base(input integer unused);
    base_calls = base_calls + 1;
    choose_base = base;
  endfunction

  function [3:0] rhs(input integer unused);
    word = 1;
    base = 0;
    rhs = 4'h1;
  endfunction

  initial begin
    words[0] = 8'h10;
    words[1] = 8'h20;
    word = 0;
    base = 4;
    word_calls = 0;
    base_calls = 0;
    words[choose_word(0)][choose_base(0) +: 4] += rhs(0);
    if (words[0] !== 8'h20 || words[1] !== 8'h20)
      $error("FAIL words %h %h", words[0], words[1]);
    if (word_calls != 1 || base_calls != 1)
      $error("FAIL calls %0d %0d", word_calls, base_calls);

    q = '{1, 2, 3};
    q[$] += 1;
    if (q[2] != 4) $error("FAIL queue %0d", q[2]);
    $display("PASSED");
  end
endmodule
