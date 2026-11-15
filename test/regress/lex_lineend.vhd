entity lex_lineend is end entity;
architecture test of lex_lineend is
begin
  process
    variable n : integer := 0;
  begin
    n := n + 1; -- ends with LF
    n := n + 2; -- ends with CR    n := n + 4; -- ends with VT    n := n + 8; -- ends with FF    n := n + 16; -- ends with CR LF
    assert n = 31;
    wait;
  end process;
end architecture;
