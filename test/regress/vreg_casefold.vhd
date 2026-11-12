library ieee;
use ieee.numeric_std.all;

entity vreg_casefold is
end entity;

architecture test of vreg_casefold is
    -- The value is truncated, which makes to_unsigned report a warning
    -- while the constant is folded
    constant K34 : unsigned(4 downto 0) := to_unsigned(34, 5);
    constant K17 : unsigned(4 downto 0) := to_unsigned(17, 5);

    function sel (a : unsigned(4 downto 0)) return integer is
    begin
        case a is
            when K17 => return 1;
            when K34 => return 2;
            when to_unsigned(21, 5) => return 3;
            when others => return 0;
        end case;
    end function;
begin
    process is
    begin
        assert sel(to_unsigned(17, 5)) = 1;
        assert sel(to_unsigned(2, 5)) = 2;
        assert sel(to_unsigned(21, 5)) = 3;
        assert sel(to_unsigned(5, 5)) = 0;
        wait;
    end process;
end architecture;
