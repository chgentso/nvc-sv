entity tostring1 is
end entity;

architecture test of tostring1 is
    signal r : real;
begin
    process is
        variable s1 : string(1 to to_string(r, 8)'length);
        variable s2 : string(1 to to_string(r, "%f")'length);
        variable s3 : string(1 to to_string(1.5, 40)'length);
    begin
        s1 := to_string(r, 8);
        assert s1(1) = '-' severity failure;
        assert s1'length > 300 severity failure;

        s2 := to_string(r, "%f");
        assert s2(1) = '-' severity failure;
        assert s2'length > 300 severity failure;

        s3 := to_string(1.5, 40);
        assert s3 = "1.5000000000000000000000000000000000000000"
            report s3 severity failure;

        assert to_string(123.5, 2) = "123.50" severity failure;
        assert to_string(0.25, 0) = "0.25" severity failure;
        wait;
    end process;
end architecture;
