library ieee;
use ieee.std_logic_1164.all;

entity wn1_leaf is
    generic ( ID : integer );
    port ( d : in std_logic );
end entity;

architecture a of wn1_leaf is
    signal s : std_logic;

    function scope (id : integer) return string is
    begin
        case id is
            when 0 => return ":wrapnames1:u0";
            when 1 => return ":wrapnames1:ua[1]";
            when 2 => return ":wrapnames1:g[0]:u";
            when others => return ":wrapnames1:g[1]:u";
        end case;
    end function;
begin
    process is
        -- ID 1 is also the instance array element ua[0]
        constant p : string := s'path_name;
    begin
        assert p = scope(ID) & ":s" or p = ":wrapnames1:ua[0]:s"
            report "bad path_name " & p severity failure;
        assert s'instance_name = p(p'left to p'right - 2) & "@wn1_leaf(a):s"
            report "bad instance_name " & s'instance_name severity failure;
        assert wn1_leaf'path_name = p(p'left to p'right - 1)
            report "bad entity path_name " & wn1_leaf'path_name
            severity failure;
        assert wn1_leaf'instance_name = p(p'left to p'right - 2) & "@wn1_leaf(a):"
            report "bad entity instance_name " & wn1_leaf'instance_name
            severity failure;
        assert s'simple_name = "s" severity failure;
        wait;
    end process;
end architecture;

library ieee;
use ieee.std_logic_1164.all;

entity wn1_mid is
    port ( d : in std_logic );
end entity;

architecture a of wn1_mid is
    component wn1_vleaf is
        port ( clk : in std_logic );
    end component;
begin
    u_v : component wn1_vleaf port map ( clk => d );
    process is
    begin
        assert wn1_mid'path_name = ":wrapnames1:u_mid:"
            report "bad path_name " & wn1_mid'path_name severity failure;
        assert wn1_mid'instance_name = ":wrapnames1:u_mid@wn1_mid(a):"
            report "bad instance_name " & wn1_mid'instance_name
            severity failure;
        wait;
    end process;
end architecture;
