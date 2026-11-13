entity inv is
    port ( a : in bit; y : out bit );
end entity;

architecture test of inv is
begin
    y <= not a;
end architecture;

entity buffer93 is
    port ( clk : in bit; q : buffer bit );
end entity;

architecture test of buffer93 is
    signal qn : bit;
begin
    u1: entity work.inv port map ( a => q, y => qn );  -- Error in VHDL-93
    q <= qn when clk = '1';
end architecture;
