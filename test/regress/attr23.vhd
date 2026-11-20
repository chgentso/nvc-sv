entity attr23 is
    port (s : in bit := '0');
begin
    assert s'stable(5 ns) severity failure;
end entity;

architecture test of attr23 is
    signal t : bit := '0';
begin
    assert t'stable(10 ns) severity failure;
end architecture;
