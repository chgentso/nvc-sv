entity latch1 is
    port (a, b, c, clk : in bit;
          q : out bit);
end entity;

architecture test of latch1 is
    signal l1, l2, l3, l4, l5, l6 : bit;
begin
    p1: process (a, b) is              -- l1 is not assigned in all paths
    begin
        if a = '1' then
            l1 <= b;
        end if;
    end process;

    p2: process (all) is               -- default assignment first
    begin
        l2 <= '0';
        if a = '1' then
            l2 <= b;
        end if;
    end process;

    p3: process (a, b, c) is           -- a case is complete
    begin
        case a is
            when '0' => l3 <= b;
            when others => l3 <= c;
        end case;
    end process;

    p4: process (clk) is               -- clocked
    begin
        if rising_edge(clk) then
            l4 <= a;
        end if;
    end process;

    p5: process (a, b, c) is           -- l5 missing in the elsif
    begin
        if a = '1' then
            l5 <= b;
        elsif b = '1' then
            l5 <= c;
        end if;
    end process;

    p6: process (a, b) is              -- nvc lint_off latch
    begin
        if a = '1' then
            l6 <= b;
        end if;
    end process;

    p7: process is                     -- not combinational
    begin
        wait until a = '1';
        if b = '1' then
            l5 <= c;
        end if;
    end process;

    q <= l1 or l2 or l3 or l4 or l5 or l6;
end architecture;
