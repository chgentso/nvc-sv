package p1;
   int x = 1;
   int y = 2;
endpackage

package p_ok;
   import p1::x;
   export p1::x;           // OK
endpackage

package p_bad1;
   import p1::x;
   export p1::y;           // Error: p1::y not imported
endpackage

package p_bad2;
   import p_ok::*;
   export p_ok::y;         // Error: p_ok does not export y
endpackage

package p_bad3;
   import p1::*;
   export p1::x;           // Error: conflicts with local x
   int x = 3;
endpackage
