entity pstring_quote1 is
end entity;

architecture test of pstring_quote1 is
    constant s : string := %illegal " quote%;
begin
end architecture;
