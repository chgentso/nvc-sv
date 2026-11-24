-- LRM 93 section 1.3.2: a component configuration without an explicit
-- binding indication cannot contain a block configuration before VHDL-2008.
entity conf_default93_ch is
    port (a : out bit);
end entity;

architecture rtl of conf_default93_ch is
begin
    a <= '1';
end architecture;

entity conf_default93 is
    port (b : out bit);
end entity;

architecture rtl of conf_default93 is
    component conf_default93_ch is
        port (a : out bit);
    end component;
begin
    u1: conf_default93_ch port map (a => b);
end architecture;

configuration conf_default93_cfg of conf_default93 is
    for rtl
        for u1 : conf_default93_ch
            for rtl                     -- Error before VHDL-2008
            end for;
        end for;
    end for;
end configuration;
