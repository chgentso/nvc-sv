entity case_static_rec is
end entity;

architecture test of case_static_rec is
    subtype idx is integer range 0 to 7;
    type arr is array (idx range <>) of character;
    subtype a2 is arr(0 to 1);
    type ur is record
        f : arr;
        n : integer;
    end record;
    subtype rs is ur(f(0 to 1));
    constant k : rs := (f => "ab", n => 3);
begin
    process is
        variable seen : integer := 0;
    begin
        -- The array subtype of the constant is locally static so the
        -- element of the record is a locally static choice
        for j in 2 to 3 loop
            case j is
                when a2'length => seen := seen + 1;
                when k.n       => seen := seen + 10;
                when others    => null;
            end case;
        end loop;
        assert seen = 11;
        wait;
    end process;
end architecture;
