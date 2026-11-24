-- Primary binding indication may leave a formal IN port without a default
-- value associated with OPEN if an incremental binding indication binds it
-- (LRM 93 section 5.2.1.2 / LRM 08 section 7.3.2.2).

package config_open1_pkg is
    signal tie_one : bit := '1';
end package;

entity config_open1_and is
    port (
        a : in  bit;
        b : in  bit;
        y : out bit
    );
end entity;

architecture rtl of config_open1_and is
begin
    y <= a and b;
end architecture;

entity config_open1_top is
end entity;

architecture struct of config_open1_top is
    component config_open1_and is
        port (
            a : in  bit;
            b : in  bit;
            y : out bit
        );
    end component;

    for u1 : config_open1_and use entity work.config_open1_and(rtl)
        port map (a => a, b => open, y => y);

    signal sa, sb, sy : bit := '0';
begin
    u1 : config_open1_and
        port map (a => sa, b => sb, y => sy);

    process is
    begin
        sa <= '0';
        wait for 1 ns;
        assert sy = '0';
        sa <= '1';
        wait for 1 ns;
        assert sy = '1';
        wait;
    end process;
end architecture;

use work.config_open1_pkg.all;

configuration config_open1 of config_open1_top is
    for struct
        for u1 : config_open1_and
            port map (b => tie_one);
        end for;
    end for;
end configuration;
