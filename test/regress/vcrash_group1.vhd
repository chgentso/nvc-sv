-- Attribute specification whose entity class is group
entity vcrash_group1 is
end entity;

architecture test of vcrash_group1 is
    signal s1, s2 : bit;
    group pair is (signal, signal);
    group g1 : pair (s1, s2);
    attribute tag : integer;
    attribute tag of g1 : group is 1;
begin
    process is
    begin
        wait;
    end process;
end architecture;
