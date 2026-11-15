entity prot_alias_own is
end entity;

architecture test of prot_alias_own is
    type stats is protected
        procedure own;
        impure function own_count return integer;
        alias own_alias is own [];
        alias count_alias is own_count [return integer];
    end protected;

    type stats is protected body
        variable n : integer := 0;
        procedure own is
        begin
            n := n + 1;
        end procedure;
        impure function own_count return integer is
        begin
            return n;
        end function;
    end protected body;
begin
    process is
        variable s : stats;
    begin
        s.own_alias;
        s.own_alias;
        assert s.count_alias = 2;
        assert s.own_count = 2;
        wait;
    end process;
end architecture;
