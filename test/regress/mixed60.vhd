entity mixed60_e is
  generic (P : real := 2.5; Q : real := 1.0; S : real := 3.0);
end entity;

architecture test of mixed60_e is
begin
  process is
  begin
    wait for 1 ns;
    report real'image(P) & " " & real'image(Q) & " " & real'image(S);
    assert (P = 4.0 and Q = 1.25 and S = 2.5)
      or (P = 2.5 and Q = 1.0 and S = 0.5) severity failure;
    wait;
  end process;
end architecture;
