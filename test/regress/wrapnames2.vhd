library ieee;
use ieee.std_logic_1164.all;

-- VHDL entity instantiated by Verilog which instantiates Verilog again
entity wn2_ent is
    port ( d : in std_logic );
end entity;

architecture a of wn2_ent is
    component wn2_inner is
        port ( d : in std_logic );
    end component;
begin
    u_in : component wn2_inner port map ( d => d );

    process is
        constant p : string := wn2_ent'path_name;
        constant i : string := wn2_ent'instance_name;
    begin
        -- No level for the module wrapping the entity or for the entity
        assert p = ":wrapnames2:g(0):u_v:u_e:"
            or p = ":wrapnames2:g(1):u_v:u_e:"
            or p = ":wrapnames2:u_top:u_e:"
            report "bad path_name " & p severity failure;
        assert i = ":wrapnames2(test):g(0):u_v:u_e@wn2_ent(a):"
            or i = ":wrapnames2(test):g(1):u_v:u_e@wn2_ent(a):"
            or i = ":wrapnames2(test):u_top:u_e@wn2_ent(a):"
            report "bad instance_name " & i severity failure;
        wait;
    end process;
end architecture;

library ieee;
use ieee.std_logic_1164.all;

entity wrapnames2 is
end entity;

architecture test of wrapnames2 is
    component wn2_v is
        port ( d : in std_logic );
    end component;

    signal d : std_logic := '0';
begin
    g: for i in 0 to 1 generate
        u_v : component wn2_v port map ( d => d );
    end generate;

    u_top : component wn2_v port map ( d => d );

    process is
    begin
        wait for 3 ns;
        report "PASSED";
        wait;
    end process;
end architecture;
