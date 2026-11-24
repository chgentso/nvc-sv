module dpichk1;
   export "DPI-C" task dup_t;
   export "DPI-C" dup_renamed = task dup_t;     // Error: already exported
   task dup_t();
   endtask

   import "DPI-C" c_fn = function int f1(input int a);
   import "DPI-C" c_fn = function int f2(input int a, input int b); // Error

   typedef union { int i; byte b; } u_t;
   typedef struct { int q[$]; } sq_t;
   typedef struct { int i; u_t u[2]; } su_t;
   typedef struct { event e; } se_t;

   import "DPI-C" function void bad_q(input sq_t s);  // Error: queue member
   import "DPI-C" function void bad_u(input su_t s);  // Error: union member
   import "DPI-C" function void bad_e(input se_t s);  // Error: event member
endmodule
