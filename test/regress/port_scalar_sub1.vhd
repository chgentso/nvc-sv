entity port_scalar_sub1_in is
    port (p : in natural);
end entity;

architecture rtl of port_scalar_sub1_in is
begin
end architecture;

entity port_scalar_sub1_out is
    port (p : out integer);
end entity;

architecture rtl of port_scalar_sub1_out is
begin
end architecture;

entity port_scalar_sub1_bad_in is
end entity;

architecture test of port_scalar_sub1_bad_in is
    signal s : integer := -5;
begin
    u : entity work.port_scalar_sub1_in port map (p => s);
end architecture;

entity port_scalar_sub1_bad_out is
end entity;

architecture test of port_scalar_sub1_bad_out is
    signal s : natural;
begin
    u : entity work.port_scalar_sub1_out port map (p => s);
end architecture;

entity port_scalar_sub1_ok is
end entity;

architecture test of port_scalar_sub1_ok is
    signal s : integer := 7;
begin
    u : entity work.port_scalar_sub1_in port map (p => s);
end architecture;
