entity m5vc is
  generic (A : integer := 1 + -- one
                          2;   -- two
           B : string := "x -- y";  -- not a comment
           C : character := '-';
           D : bit_vector(3 downto 0) := /* block */ "10" & "10");
end entity;
