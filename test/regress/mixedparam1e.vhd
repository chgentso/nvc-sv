library ieee; use ieee.std_logic_1164.all;
entity mixedparam1e is
end entity;

architecture test of mixedparam1e is
    signal o : std_logic_vector(7 downto 0);
begin
    u: entity work.mixedparam1v_m
        generic map (g_sim => 2)
        port map (o);
end architecture;
