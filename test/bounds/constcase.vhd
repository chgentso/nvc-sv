entity constcase is
end entity;

architecture test of constcase is
    signal b : bit;
begin
    -- The selector is constant so the statement is simplified
    with true select b <=
        '0' when false | true,          -- Error
        '1' when true;

    p1: process is
        constant k : integer := 2;
        constant kk : boolean := k = 1;
    begin
        case k is
            when 0 to 2 => b <= '0';
            when 2 to 3 => b <= '1';    -- Error
            when others => null;
        end case;
        case k is
            when 1 => b <= '0';         -- OK
            when others => null;
        end case;
        case true is
            when kk => null;            -- OK, choice is not folded
            when others => null;
        end case;
        wait;
    end process;
end architecture;
