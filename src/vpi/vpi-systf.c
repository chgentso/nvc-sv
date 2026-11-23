//
//  Copyright (C) 2024-2026  Nick Gasson
//
//  This program is free software: you can redistribute it and/or modify
//  it under the terms of the GNU General Public License as published by
//  the Free Software Foundation, either version 3 of the License, or
//  (at your option) any later version.
//
//  This program is distributed in the hope that it will be useful,
//  but WITHOUT ANY WARRANTY; without even the implied warranty of
//  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
//  GNU General Public License for more details.
//
//  You should have received a copy of the GNU General Public License
//  along with this program.  If not, see <http://www.gnu.org/licenses/>.
//

#include "util.h"
#include "array.h"
#include "diag.h"
#include "jit/jit.h"
#include "jit/jit-exits.h"
#include "rt/assert.h"
#include "rt/fileio.h"
#include "option.h"
#include "printf.h"
#include "rt/model.h"
#include "svrand.h"
#include "thread.h"
#include "vpi/vpi-model.h"
#include "vpi/vpi-priv.h"

#include <assert.h>
#include <ctype.h>
#include <errno.h>
#include <inttypes.h>
#include <math.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>

typedef struct {
   FILE       *file;
   const char *str;
   size_t      pos;
   bool        eof;
} scan_src_t;

static int    nplusargs = 0;
static char **plusargs = NULL;

void vpi_set_plusargs(int argc, char **argv)
{
   nplusargs = argc;
   plusargs = argv;
}

const char *vpi_get_plusarg(int nth)
{
   return nth < nplusargs ? plusargs[nth] : NULL;
}

// Set while formatting into a string variable which keeps the null
// characters of its string arguments
static bool keep_nul = false;

static void format_string(ostream_t *out, vpiHandle it, int fwidth)
{
   vpiHandle arg = vpi_scan(it);
   if (arg == NULL)
      return;

   const int size = vpi_get(vpiSize, arg);
   const int slen = vpi_string_length(arg);

   if (size > 0) {
      // A null character in a vector is shown as a space except in the
      // leading ones if there is no minimum field width
      s_vpi_value bits = { .format = vpiBinStrVal };
      vpi_get_value(arg, &bits);

      if (strspn(bits.value.str, "01") == strlen(bits.value.str)) {
         const int nbytes = (size + 7) / 8;
         char *buf LOCAL = xmalloc(nbytes + 1);
         int n = 0;
         bool leading = true;
         for (int i = 0; i < nbytes; i++) {
            unsigned ch = 0;
            for (int j = 0; j < 8; j++) {
               const int bit = i * 8 + j - (nbytes * 8 - size);
               ch = (ch << 1) | (bit >= 0 && bits.value.str[bit] == '1');
            }

            if (ch == 0 && leading && fwidth == 0)
               continue;

            leading &= ch == 0;
            buf[n++] = ch ?: ' ';
         }
         buf[n] = '\0';

         if (fwidth >= 0)
            nvc_fprintf(out, "%*s", fwidth, buf);
         else
            ostream_puts(out, buf);

         vpi_release_handle(arg);
         return;
      }
   }

   s_vpi_value argval = { .format = vpiStringVal };
   vpi_get_value(arg, &argval);

   if (slen >= 0 && keep_nul) {
      for (int i = fwidth - slen; i > 0; i--)
         ostream_putc(out, ' ');
      ostream_write(out, argval.value.str, slen);
   }
   else if (fwidth >= 0)
      nvc_fprintf(out, "%*s", fwidth, argval.value.str);
   else
      ostream_puts(out, argval.value.str);

   vpi_release_handle(arg);
}

static int calc_dec_size(int nr_bits, bool is_signed)
{
   // From Icarus Verilog vpi/sys_display.c
   if (is_signed) --nr_bits;
   int r = (nr_bits * 146L + 484) / 485;
   if (is_signed) ++r;
   return r;
}

static void format_radix_string(ostream_t *out, const char *str, int fwidth)
{
   // With an explicit width the digits are zero padded to exactly this
   // size unless the value needs more

   if (strcmp(str, "null") == 0) {
      // A null class handle is padded with spaces as for %d
      nvc_fprintf(out, "%*s", fwidth, str);
      return;
   }
   else if (fwidth >= 0) {
      if (strpbrk(str, "xXzZ") != NULL) {
         // A leading run of the same digit is shortened to one digit
         while (str[0] != '\0' && str[0] == str[1])
            str++;
      }
      else {
         while (str[0] == '0' && str[1] != '\0')
            str++;
      }

      for (int i = fwidth - (int)strlen(str); i > 0; i--)
         ostream_putc(out, '0');
   }

   ostream_puts(out, str);
}

static struct {
   bool  valid;
   int   units;
   int   precision;
   int   width;
   char  suffix[16];
} time_format;

static void format_radix(ostream_t *out, vpiHandle arg, char radix, int fwidth,
                         int precision, char pad)
{
   if (radix == 't' && time_format.valid) {
      s_vpi_value argval = { .format = vpiRealVal };
      vpi_get_value(arg, &argval);

      if (!vpi_chk_error(NULL)) {
         // The argument is in time units of the module doing the printing
         const double value = argval.value.real * vpi_caller_timescale()
            * pow(10.0, -15 - time_format.units);

         // The minimum field width includes the suffix
         const int prec = precision >= 0 ? precision : time_format.precision;
         LOCAL_TEXT_BUF tb = tb_new();
         tb_printf(tb, "%.*f%s", prec, value, time_format.suffix);
         nvc_fprintf(out, "%*s", fwidth >= 0 ? fwidth : time_format.width,
                     tb_get(tb));
      }

      return;
   }

   if (radix == 't' && vpi_caller_timescale() > 1) {
      // With no $timeformat display in the simulator precision
      s_vpi_value argval = { .format = vpiRealVal };
      vpi_get_value(arg, &argval);

      if (!vpi_chk_error(NULL))
         nvc_fprintf(out, "%*"PRIi64, fwidth >= 0 ? fwidth : 20,
                     (int64_t)llround(argval.value.real
                                      * vpi_caller_timescale()
                                      / vpi_caller_design_precision()));

      return;
   }

   switch (radix) {
   case 'd':
   case 't':
   case 'p':
      {
         s_vpi_value argval = { .format = vpiDecStrVal };
         vpi_get_value(arg, &argval);

         if (!vpi_chk_error(NULL)) {
            const int nbits = vpi_get(vpiSize, arg);
            const bool is_signed = vpi_get(vpiSigned, arg);
            // With %p an integral value is padded as for %d but a
            // handle is not integral and has the fewest digits
            const int dmax = radix == 'p' && vpi_is_class_handle(arg)
               ? 0 : calc_dec_size(nbits, is_signed);
            const int len = strlen(argval.value.str);

            if (fwidth >= 0) {
               // A leading zero in the field width zero fills a decimal
               // number after the sign as in C; a value with x or z bits
               // is padded with spaces
               const char *str = argval.value.str;
               const bool zero = pad == '0' && radix == 'd'
                  && strpbrk(str, "xXzZ") == NULL;
               if (zero && str[0] == '-') {
                  ostream_putc(out, '-');
                  str++;
               }

               for (int i = 0; i < fwidth - len; i++)
                  ostream_putc(out, zero ? '0' : ' ');
               ostream_puts(out, str);
            }
            else if (dmax > len)
               nvc_fprintf(out, "%*s", dmax, argval.value.str);
            else
               ostream_puts(out, argval.value.str);
         }
      }
      break;
   case 'x':
   case 'h':
      {
         s_vpi_value argval = { .format = vpiHexStrVal };
         vpi_get_value(arg, &argval);

         if (!vpi_chk_error(NULL))
            format_radix_string(out, argval.value.str, fwidth);
      }
      break;
   case 'b':
      {
         s_vpi_value argval = { .format = vpiBinStrVal };
         vpi_get_value(arg, &argval);

         if (!vpi_chk_error(NULL))
            format_radix_string(out, argval.value.str, fwidth);
      }
      break;
   case 'o':
      {
         s_vpi_value argval = { .format = vpiOctStrVal };
         vpi_get_value(arg, &argval);

         if (!vpi_chk_error(NULL))
            format_radix_string(out, argval.value.str, fwidth);
      }
      break;
   case 'e':
   case 'f':
   case 'g':
      {
         s_vpi_value argval = { .format = vpiRealVal };
         vpi_get_value(arg, &argval);

         if (!vpi_chk_error(NULL)) {
            char fmt[16];
            checked_sprintf(fmt, sizeof(fmt), "%%%s*.*%c",
                            pad == '0' ? "0" : "", radix);
            nvc_fprintf(out, fmt, fwidth < 0 ? 0 : fwidth,
                        precision >= 0 ? precision : 6,
                        argval.value.real);
         }
      }
      break;
   }
}

static void format_number(ostream_t *out, vpiHandle it, char radix, int fwidth,
                          int precision, char pad)
{
   vpiHandle arg = vpi_scan(it);
   if (arg == NULL)
      return;

   format_radix(out, arg, radix, fwidth, precision, pad);
   vpi_release_handle(arg);
}

static void format_char(ostream_t *out, vpiHandle it, int fwidth)
{
   // The low eight bits of the value as an ASCII character, right
   // justified in the field width if one is given (21.2.1.2, 21.2.1.3)

   vpiHandle arg = vpi_scan(it);
   if (arg == NULL)
      return;

   char ch = '\0';
   bool have = true;

   const bool is_string_var =
      vpi_get(vpiType, arg) == vpiOperation && vpi_get(vpiSize, arg) == -2;
   const bool is_string_lit = vpi_get(vpiType, arg) == vpiConstant
      && vpi_get(vpiConstType, arg) == vpiStringConst;

   if (is_string_var || is_string_lit) {
      // A string is a sequence of bytes so its value as a number has the
      // last character in the low byte
      s_vpi_value str = { .format = vpiStringVal };
      vpi_get_value(arg, &str);
      const size_t len = str.value.str != NULL ? strlen(str.value.str) : 0;
      if (len > 0)
         ch = str.value.str[len - 1];
      else
         have = false;
   }
   else {
      s_vpi_value argval = { .format = vpiDecStrVal };
      vpi_get_value(arg, &argval);

      s_vpi_error_info ei;
      vpi_chk_error(&ei);
      ch = atoi(argval.value.str);
   }

   for (int i = 1; i < fwidth; i++)
      ostream_putc(out, ' ');

   if (have)
      ostream_putc(out, ch);
   else if (fwidth > 0)
      ostream_putc(out, ' ');

   vpi_release_handle(arg);
}

static __thread void *hier_context = NULL;

void x_sv_hier_scope(void *context)
{
   hier_context = context;
}

static void format_hier_name(ostream_t *out, const char *suffix, int len)
{
   rt_model_t *m = get_model_or_null();
   rt_scope_t *scope = m != NULL ? get_active_scope(m) : NULL;

   if (m != NULL && hier_context != NULL) {
      // The subroutine may have been called from another instance
      rt_scope_t *s = scope_for_context(root_scope(m), hier_context);
      if (s != NULL)
         scope = s;
   }

   LOCAL_TEXT_BUF tb = tb_new();
   if (suffix != NULL && suffix[0] == '=')
      tb_catn(tb, suffix + 1, len - 1);
   else {
      if (scope != NULL)
         get_verilog_path_name(scope, tb);
      if (suffix != NULL)
         tb_catn(tb, suffix, len);
   }
   ostream_write(out, tb_get(tb), tb_len(tb));
}

// Internal conversion produced by the simplifier for %p
#define FORMAT_QUOTE '\x02'

static void format_quoted(ostream_t *out, vpiHandle it, int fwidth)
{
   // A string for %p in double quotes with special characters escaped
   // so that it reads as a string literal (5.9)

   vpiHandle arg = vpi_scan(it);
   if (arg == NULL)
      return;

   s_vpi_value argval = { .format = vpiStringVal };
   vpi_get_value(arg, &argval);

   LOCAL_TEXT_BUF tb = tb_new();
   tb_append(tb, '"');
   for (const char *p = argval.value.str; *p; p++) {
      const unsigned char ch = *p;
      switch (ch) {
      case '\n': tb_cat(tb, "\\n"); break;
      case '\t': tb_cat(tb, "\\t"); break;
      case '"': tb_cat(tb, "\\\""); break;
      case '\\': tb_cat(tb, "\\\\"); break;
      default:
         if (ch < 0x20 || ch == 0x7f)
            tb_printf(tb, "\\%03o", ch);
         else
            tb_append(tb, ch);
      }
   }
   tb_append(tb, '"');

   for (int i = tb_len(tb); i < fwidth; i++)
      ostream_putc(out, ' ');
   ostream_write(out, tb_get(tb), tb_len(tb));

   vpi_release_handle(arg);
}

static void interpret_format(ostream_t *out, const char *fmt, vpiHandle it)
{
   const char *start = fmt, *p = fmt;

   for (; *p; p++) {
      if (*p == '%') {
         if (start < p)
            ostream_write(out, start, p - start);

         p++;   // Skip over '%'

         const bool left = *p == '-';
         if (left)
            p++;

         char pad = ' ';
         if (*p == '0' && isdigit_iso88591(*(p + 1)))
            pad = *p++;

         int fwidth = -1;
         if (isdigit_iso88591(*p))
            fwidth = strtol(p, (char **)&p, 10);

         int precision = -1;
         if (*p == '.') {
            p++;
            precision = strtol(p, (char **)&p, 10);
         }

         const char *suffix = NULL;
         int suffix_len = 0;
         if (*p == '{') {
            suffix = ++p;
            while (*p != '}' && *p != '\0')
               p++;
            suffix_len = p - suffix;
            if (*p == '}')
               p++;
         }

         const char spec = tolower_iso88591(*p);

         // A left justified field is formatted without padding first
         LOCAL_TEXT_BUF field = left ? tb_new() : NULL;
         ostream_t tmp = { tb_ostream_write, field, CHARSET_ISO88591 };
         ostream_t *dst = left ? &tmp : out;
         const int wanted = spec == 'm' ? 0 : fwidth;
         if (left && (spec == 'd' || fwidth >= 0))
            fwidth = 0;
         else if (left)
            fwidth = -1;

         switch (spec) {
         case 'm':
            format_hier_name(dst, suffix, suffix_len);
            break;
         case 's':
            format_string(dst, it, fwidth);
            break;
         case 'd':
         case 'b':
         case 'x':
         case 'h':
         case 'o':
         case 't':
         case 'e':
         case 'f':
         case 'g':
            if (isupper_iso88591(*p) && (spec == 'e' || spec == 'g')) {
               // The exponent is shown with a capital letter
               LOCAL_TEXT_BUF digits = tb_new();
               ostream_t buf = { tb_ostream_write, digits, CHARSET_ISO88591 };
               format_number(&buf, it, spec, fwidth, precision, pad);
               const char *str = tb_get(digits);
               for (size_t i = 0; i < tb_len(digits); i++)
                  ostream_putc(dst, toupper_iso88591(str[i]));
            }
            else
               format_number(dst, it, spec, fwidth, precision, pad);
            break;
         case 'p':
            format_number(dst, it, 'p', fwidth, precision, pad);
            break;
         case FORMAT_QUOTE:
            format_quoted(dst, it, fwidth);
            break;
         case 'c':
            format_char(dst, it, fwidth);
            break;
         case '%':
            ostream_putc(dst, '%');
            break;
         default:
            jit_msg(NULL, DIAG_WARN, "unknown format specifier '%c'", *p);
         }

         if (left) {
            ostream_write(out, tb_get(field), tb_len(field));
            for (int i = tb_len(field); i < wanted; i++)
               ostream_putc(out, ' ');
         }

         start = p + 1;
      }
   }

   if (start < p)
      ostream_write(out, start, p - start);
}

static void verilog_printf(ostream_t *out, char default_radix, vpiHandle it)
{
   vpiHandle arg = vpi_scan(it);
   bool first = true;
   while (arg != NULL) {
      const bool is_null = vpi_get(vpiType, arg) == vpiOperation
         && vpi_get(vpiOpType, arg) == vpiNullOp;
      const bool is_string_expr = first && !is_null
         && vpi_get(vpiType, arg) == vpiOperation
         && vpi_get(vpiSize, arg) == -2;
      const bool has_format =
         is_string_expr || (vpi_get(vpiType, arg) == vpiConstant
                            && vpi_get(vpiConstType, arg) == vpiStringConst);
      first = false;

      if (is_null)
         ostream_putc(out, ' ');
      else if (has_format) {
         s_vpi_value argval = { .format = vpiStringVal };
         vpi_get_value(arg, &argval);

         char *copy = xstrdup(argval.value.str);
         interpret_format(out, copy, it);
         free(copy);
      }
      else
         format_radix(out, arg, default_radix, -1, -1, ' ');

      vpi_release_handle(arg);
      arg = vpi_scan(it);
   }

   hier_context = NULL;
}

static PLI_INT32 display_tf(PLI_BYTE8 *userdata)
{
   vpiHandle call = vpi_handle(vpiSysTfCall, NULL);
   assert(call != NULL);

   vpiHandle it = vpi_iterate(vpiArgument, call);

   ostream_t *out = nvc_stdout();
   verilog_printf(out, *(char *)userdata, it);
   ostream_putc(out, '\n');
   return 0;
}

static PLI_INT32 write_tf(PLI_BYTE8 *userdata)
{
   vpiHandle call = vpi_handle(vpiSysTfCall, NULL);
   assert(call != NULL);

   vpiHandle it = vpi_iterate(vpiArgument, call);

   verilog_printf(nvc_stdout(), *(char *)userdata, it);
   return 0;
}

static void put_int_result(PLI_INT32 value)
{
   s_vpi_value result = {
      .format = vpiIntVal,
      .value = { .integer = value },
   };

   vpiHandle call = vpi_handle(vpiSysTfCall, NULL);
   assert(call != NULL);

   vpi_put_value(call, &result, NULL, 0);
   vpi_release_handle(call);
}

static PLI_INT32 get_int_arg(vpiHandle arg)
{
   s_vpi_value value = { .format = vpiIntVal };
   vpi_get_value(arg, &value);
   return value.value.integer;
}

#define SV_FD_BASE 0x80000000u

static A(file_handle_t) sv_files = AINIT;

static file_handle_t sv_fd_handle(int64_t fd)
{
   // Descriptors count from 3 with the most significant bit set

   const uint32_t bits = fd;
   if (bits < SV_FD_BASE + 3)
      return 0;

   const uint32_t slot = bits - SV_FD_BASE - 3;
   return slot < sv_files.count ? sv_files.items[slot] : 0;
}

static int64_t sv_fd_alloc(file_handle_t fh)
{
   unsigned slot = 0;
   while (slot < sv_files.count && sv_files.items[slot] != 0)
      slot++;

   if (slot == sv_files.count)
      APUSH(sv_files, fh);
   else
      sv_files.items[slot] = fh;

   return (int32_t)(SV_FD_BASE + 3 + slot);
}

// Files opened with a multichannel descriptor, one per bit from 1 to 30
static file_handle_t sv_mcd_files[31];

static int64_t sv_mcd_alloc(file_handle_t fh)
{
   for (int bit = 1; bit < 31; bit++) {
      if (sv_mcd_files[bit] == 0) {
         sv_mcd_files[bit] = fh;
         return 1 << bit;
      }
   }

   file_close(fh);
   jit_msg(NULL, DIAG_WARN, "$fopen: all 30 channels of a multichannel "
           "descriptor are open");
   return 0;
}

static void sv_fd_free(int64_t fd)
{
   const uint32_t bits = fd;
   if (bits == 0)
      return;   // The result of a failed $fopen
   else if (bits & SV_FD_BASE) {
      if (bits >= SV_FD_BASE + 3 && bits - SV_FD_BASE - 3 < sv_files.count) {
         file_handle_t *slot = &(sv_files.items[bits - SV_FD_BASE - 3]);
         if (*slot != 0) {
            file_close(*slot);
            *slot = 0;
            return;
         }
      }
      else if (bits < SV_FD_BASE + 3)
         return;   // The standard streams stay open
   }
   else {
      bool closed = (bits & 1) != 0;   // Standard output stays open
      for (int bit = 1; bit < 31; bit++) {
         if ((bits & (1u << bit)) && sv_mcd_files[bit] != 0) {
            file_close(sv_mcd_files[bit]);
            sv_mcd_files[bit] = 0;
            closed = true;
         }
      }

      if (closed)
         return;
   }

   jit_msg(NULL, DIAG_WARN, "$fclose: file descriptor 0x%08x is not open",
           bits);
}

static void sv_bad_fd(PLI_INT32 fd)
{
   jit_msg(NULL, DIAG_ERROR, "%s: argument is not a valid file "
           "descriptor 0x%08x", vpi_caller_name(), (uint32_t)fd);
}

static FILE *sv_fd_stream(PLI_INT32 fd)
{
   switch ((uint32_t)fd) {
   case 1:
   case SV_FD_BASE + 1: return stdout;
   case SV_FD_BASE + 2: return stderr;
   case SV_FD_BASE: return stdin;
   default:
      if ((uint32_t)fd & SV_FD_BASE)
         return file_stream(sv_fd_handle(fd));

      // A multichannel descriptor with a single channel
      for (int bit = 1; bit < 31; bit++) {
         if ((uint32_t)fd == 1u << bit)
            return file_stream(sv_mcd_files[bit]);
      }

      return NULL;
   }
}

static FILE *file_for_fd(PLI_INT32 fd, bool quiet)
{
   // An invalid descriptor, such as the result of a failed $fopen, is
   // reported and the caller returns a failure value

   FILE *f = sv_fd_stream(fd);
   if (f == NULL && !(quiet && fd == 0))
      sv_bad_fd(fd);

   return f;
}

typedef struct {
   FILE *files[32];
   int   count;
} sv_outputs_t;

static bool sv_outputs(PLI_INT32 fd, sv_outputs_t *out, bool quiet)
{
   // The streams written by an output task: one for a file descriptor
   // and one for each open channel of a multichannel descriptor

   out->count = 0;

   const uint32_t bits = fd;
   if (bits == 0 || (bits & SV_FD_BASE)) {
      FILE *f = quiet ? sv_fd_stream(fd) : file_for_fd(fd, true);
      if (f != NULL)
         out->files[out->count++] = f;
      return f != NULL;
   }

   if (bits & 1)
      out->files[out->count++] = stdout;

   for (int bit = 1; bit < 31; bit++) {
      if (bits & (1u << bit)) {
         FILE *f = file_stream(sv_mcd_files[bit]);
         if (f != NULL)
            out->files[out->count++] = f;
      }
   }

   if (out->count == 0 && !quiet)
      sv_bad_fd(fd);

   return out->count > 0;
}

static void sv_outputs_write(const sv_outputs_t *out, const char *buf,
                             size_t len)
{
   for (int i = 0; i < out->count; i++) {
      if (out->files[i] == stdout)
         ostream_write(nvc_stdout(), buf, len);
      else
         fwrite(buf, 1, len, out->files[i]);
   }
}

static FILE *get_file_stream(vpiHandle arg, bool quiet)
{
   s_vpi_value value = { .format = vpiIntVal };
   vpi_get_value(arg, &value);

   return file_for_fd(value.value.integer, quiet);
}

static PLI_INT32 sformatf_tf(PLI_BYTE8 *userdata)
{
   vpiHandle call = vpi_handle(vpiSysTfCall, NULL);
   assert(call != NULL);

   vpiHandle it = vpi_iterate(vpiArgument, call);

   LOCAL_TEXT_BUF tb = tb_new();
   ostream_t os = {
      .callback = tb_ostream_write,
      .context  = tb,
      .charset  = CHARSET_ISO88591,
   };
   keep_nul = true;
   verilog_printf(&os, userdata != NULL ? *(char *)userdata : 'd', it);
   keep_nul = false;

   vpi_return_string(tb_get(tb), tb_len(tb));

   vpi_release_handle(call);
   return 0;
}

static PLI_INT32 rewind_tf(PLI_BYTE8 *userdata)
{
   vpiHandle call = vpi_handle(vpiSysTfCall, NULL);
   assert(call != NULL);

   vpiHandle it = vpi_iterate(vpiArgument, call);
   vpiHandle fd_arg = vpi_scan(it);

   FILE *f = get_file_stream(fd_arg, false);
   put_int_result(f != NULL ? fseek(f, 0, SEEK_SET) : -1);

   vpi_release_handle(fd_arg);
   vpi_release_handle(it);
   vpi_release_handle(call);
   return 0;
}

static PLI_INT32 timeformat_tf(PLI_BYTE8 *userdata)
{
   vpiHandle call = vpi_handle(vpiSysTfCall, NULL);
   assert(call != NULL);

   vpiHandle it = vpi_iterate(vpiArgument, call);
   vpiHandle units = vpi_scan(it);
   vpiHandle precision = vpi_scan(it);
   vpiHandle suffix = vpi_scan(it);
   vpiHandle width = vpi_scan(it);

   if (width != NULL) {
      s_vpi_value str = { .format = vpiStringVal };
      vpi_get_value(suffix, &str);
      checked_sprintf(time_format.suffix, sizeof(time_format.suffix), "%s",
                      str.value.str);

      time_format.valid     = true;
      time_format.units     = get_int_arg(units);
      time_format.precision = get_int_arg(precision);
      time_format.width     = get_int_arg(width);

      vpi_release_handle(width);
      vpi_release_handle(suffix);
      vpi_release_handle(precision);
      vpi_release_handle(units);
      vpi_release_handle(it);
   }

   vpi_release_handle(call);
   return 0;
}

static PLI_INT32 dist_uniform_tf(PLI_BYTE8 *userdata)
{
   vpiHandle call = vpi_handle(vpiSysTfCall, NULL);
   assert(call != NULL);

   vpiHandle it = vpi_iterate(vpiArgument, call);
   vpiHandle seed = vpi_scan(it);
   vpiHandle start = vpi_scan(it);
   vpiHandle end = vpi_scan(it);

   const PLI_INT32 lo = get_int_arg(start), hi = get_int_arg(end);

   s_vpi_value cur = { .format = vpiIntVal };
   vpi_get_value(seed, &cur);

   int32_t a_seed = cur.value.integer;
   put_int_result(rtl_dist_uniform(&a_seed, lo, hi));

   s_vpi_value next = { .format = vpiIntVal, .value = { .integer = a_seed } };
   vpi_put_value(seed, &next, NULL, 0);

   vpi_release_handle(seed);
   vpi_release_handle(start);
   vpi_release_handle(end);
   vpi_release_handle(it);
   vpi_release_handle(call);
   return 0;
}

static PLI_INT32 realtime_tf(PLI_BYTE8 *userdata)
{
   rt_model_t *m = get_model();

   s_vpi_value result = {
      .format = vpiRealVal,
      .value  = { .real = (double)model_now(m, NULL)
                  / (double)vpi_caller_timescale() },
   };

   vpiHandle call = vpi_handle(vpiSysTfCall, NULL);
   assert(call != NULL);

   vpi_put_value(call, &result, NULL, 0);

   vpi_release_handle(call);
   return 0;
}

static int64_t sv_fread_last = 0;

int64_t x_sv_fread(int64_t fd, int64_t nbytes)
{
   FILE *f = file_stream(sv_fd_handle(fd));
   if (f == NULL) {
      jit_msg(NULL, DIAG_ERROR, "$fread: argument is not a valid file "
              "descriptor 0x%08x", (uint32_t)fd);
      sv_fread_last = 0;
      return 0;
   }

   // The first byte read goes in the most significant position
   int64_t result = 0, count = 0;
   for (int i = 0; i < nbytes; i++) {
      int ch = fgetc(f);
      if (ch == EOF)
         ch = 0;
      else
         count++;

      result = (result << 8) | (ch & 0xff);
   }

   sv_fread_last = count;
   return result;
}

int64_t x_sv_fread_count(void)
{
   return sv_fread_last;
}

int64_t x_sv_class_oid(void)
{
   // Objects are numbered from one in order of creation

   static int64_t serial = 0;
   return ++serial;
}

static PLI_INT32 fopen_tf(PLI_BYTE8 *userdata)
{
   vpiHandle call = vpi_handle(vpiSysTfCall, NULL);
   assert(call != NULL);

   vpiHandle argv = vpi_iterate(vpiArgument, call);
   vpiHandle file_arg = vpi_scan(argv);
   if (file_arg == NULL) {
      put_int_result(0);
      vpi_release_handle(argv);
      vpi_release_handle(call);
      return 0;
   }

   s_vpi_value file = { .format = vpiStringVal };
   vpi_get_value(file_arg, &file);
   char *file_name LOCAL = xstrdup(file.value.str);

   const char *mode_str = "w";
   char *mode_copy LOCAL = NULL;
   vpiHandle mode_arg = vpi_scan(argv);
   const bool mcd = mode_arg == NULL;
   if (mode_arg != NULL) {
      s_vpi_value mode = { .format = vpiStringVal };
      vpi_get_value(mode_arg, &mode);
      mode_copy = xstrdup(mode.value.str);
      mode_str = mode_copy;
      vpi_release_handle(mode_arg);
   }

   // The b in a mode such as "rb" or "r+b" makes no difference
   const bool has_plus = strchr(mode_str, '+') != NULL;
   file_mode_t fmode = FILE_READ;
   switch (mode_str[0]) {
   case 'r': fmode = has_plus ? FILE_READ_WRITE : FILE_READ; break;
   case 'w': fmode = has_plus ? FILE_TRUNC_READ_WRITE : FILE_WRITE; break;
   case 'a': fmode = has_plus ? FILE_APPEND_READ_WRITE : FILE_APPEND; break;
   default:
      jit_msg(NULL, DIAG_FATAL, "invalid file mode \"%s\"", mode_str);
   }

   file_handle_t fh = file_open(file_name, fmode);
   if (fh == 0)
      put_int_result(0);
   else if (mcd)
      put_int_result(sv_mcd_alloc(fh));
   else
      put_int_result(sv_fd_alloc(fh));

   vpi_release_handle(file_arg);
   vpi_release_handle(argv);
   vpi_release_handle(call);
   return 0;
}

static PLI_INT32 fclose_tf(PLI_BYTE8 *userdata)
{
   vpiHandle call = vpi_handle(vpiSysTfCall, NULL);
   assert(call != NULL);

   vpiHandle argv = vpi_iterate(vpiArgument, call);
   vpiHandle fd_arg = vpi_scan(argv);
   if (fd_arg != NULL) {
      sv_fd_free(get_int_arg(fd_arg));
      vpi_release_handle(fd_arg);
   }

   vpi_release_handle(argv);
   vpi_release_handle(call);
   return 0;
}

static PLI_INT32 fflush_tf(PLI_BYTE8 *userdata)
{
   vpiHandle call = vpi_handle(vpiSysTfCall, NULL);
   assert(call != NULL);

   vpiHandle argv = vpi_iterate(vpiArgument, call);
   vpiHandle fd_arg = vpi_scan(argv);
   if (fd_arg == NULL)
      fflush(NULL);
   else {
      sv_outputs_t out;
      if (sv_outputs(get_int_arg(fd_arg), &out, false)) {
         for (int i = 0; i < out.count; i++)
            fflush(out.files[i]);
      }
      vpi_release_handle(fd_arg);
   }

   vpi_release_handle(argv);
   vpi_release_handle(call);
   return 0;
}

static PLI_INT32 feof_tf(PLI_BYTE8 *userdata)
{
   vpiHandle call = vpi_handle(vpiSysTfCall, NULL);
   assert(call != NULL);

   vpiHandle argv = vpi_iterate(vpiArgument, call);
   vpiHandle fd_arg = vpi_scan(argv);
   PLI_INT32 result = 1;
   if (fd_arg != NULL) {
      // A descriptor which cannot be read is at its end
      FILE *f = get_file_stream(fd_arg, false);
      result = f != NULL ? feof(f) : 1;
      vpi_release_handle(fd_arg);
   }

   put_int_result(result);
   vpi_release_handle(argv);
   vpi_release_handle(call);
   return 0;
}

static PLI_INT32 system_tf(PLI_BYTE8 *userdata)
{
   vpiHandle call = vpi_handle(vpiSysTfCall, NULL);
   assert(call != NULL);

   vpiHandle argv = vpi_iterate(vpiArgument, call);
   vpiHandle cmd_arg = argv != NULL ? vpi_scan(argv) : NULL;

   PLI_INT32 result = 0;
   if (cmd_arg != NULL) {
      s_vpi_value cmd = { .format = vpiStringVal };
      vpi_get_value(cmd_arg, &cmd);
      char *command LOCAL = xstrdup(cmd.value.str);

      fflush(stdout);
      result = system(command);
#ifdef __MINGW32__
      // The exit status is not in bits 15:8 as for the POSIX wait status
      if (result != -1)
         result = (result & 0xff) << 8;
#endif

      vpi_release_handle(cmd_arg);
      vpi_release_handle(argv);
   }
   else if (argv != NULL)
      vpi_release_handle(argv);

   put_int_result(result);
   vpi_release_handle(call);
   return 0;
}

static PLI_INT32 fdisplay_tf(PLI_BYTE8 *userdata)
{
   const char *fmt = (const char *)userdata;
   const bool newline = fmt[1] == 'n';

   vpiHandle call = vpi_handle(vpiSysTfCall, NULL);
   assert(call != NULL);

   vpiHandle argv = vpi_iterate(vpiArgument, call);
   vpiHandle fd_arg = vpi_scan(argv);
   if (fd_arg != NULL) {
      sv_outputs_t out;
      if (sv_outputs(get_int_arg(fd_arg), &out, false)) {
         LOCAL_TEXT_BUF tb = tb_new();
         ostream_t os = {
            .callback = tb_ostream_write,
            .context  = tb,
            .charset  = CHARSET_ISO88591,
         };
         verilog_printf(&os, fmt[0], argv);
         if (newline)
            ostream_putc(&os, '\n');
         sv_outputs_write(&out, tb_get(tb), tb_len(tb));
      }
      vpi_release_handle(fd_arg);
   }

   vpi_release_handle(argv);
   vpi_release_handle(call);
   return 0;
}

static int scan_getc(scan_src_t *src)
{
   int ch;
   if (src->file != NULL)
      ch = fgetc(src->file);
   else
      ch = src->str[src->pos] ? src->str[src->pos++] : EOF;

   if (ch == EOF)
      src->eof = true;

   return ch;
}

static void scan_ungetc(scan_src_t *src, int ch)
{
   if (ch == EOF)
      return;

   src->eof = false;
   if (src->file != NULL)
      ungetc(ch, src->file);
   else
      src->pos--;
}

static void scan_skip_ws(scan_src_t *src)
{
   int ch;
   while ((ch = scan_getc(src)) != EOF) {
      if (!isspace_iso88591(ch)) {
         scan_ungetc(src, ch);
         break;
      }
   }
}

static bool scan_token(scan_src_t *src, char *buf, size_t size)
{
   scan_skip_ws(src);

   size_t len = 0;
   int ch;
   while ((ch = scan_getc(src)) != EOF) {
      if (isspace_iso88591(ch)) {
         scan_ungetc(src, ch);
         break;
      }
      else if (len + 1 < size)
         buf[len++] = ch;
   }

   buf[len] = '\0';
   return len > 0;
}

static bool scan_digits(scan_src_t *src, char conv, char *buf, size_t size)
{
   // The longest prefix which is valid for the conversion

   scan_skip_ws(src);

   size_t len = 0;
   int ch;
   while ((ch = scan_getc(src)) != EOF) {
      bool valid;
      switch (conv) {
      case 'd':
         valid = isdigit_iso88591(ch) || ch == '_'
            || (len == 0 && (ch == '-' || ch == '+'))
            || (len == 0 && strchr("xXzZ?", ch) != NULL);
         break;
      case 'b':
         valid = strchr("01xXzZ?_", ch) != NULL;
         break;
      case 'o':
         valid = (ch >= '0' && ch <= '7') || strchr("xXzZ?_", ch) != NULL;
         break;
      case 'f':
      case 'e':
      case 'g':
         valid = isdigit_iso88591(ch) || ch == '.'
            || (len > 0 && (ch == 'e' || ch == 'E'))
            || ((len == 0 || tolower(buf[len - 1]) == 'e')
                && (ch == '-' || ch == '+'));
         break;
      default:
         valid = isxdigit(ch) || strchr("xXzZ?_", ch) != NULL;
         break;
      }

      if (!valid) {
         scan_ungetc(src, ch);
         break;
      }
      else if (len + 1 < size)
         buf[len++] = ch;
   }

   buf[len] = '\0';
   return len > 0;
}

static bool scan_assign(vpiHandle arg, char conv, const char *tok)
{
   s_vpi_value value = { .value = { .str = (PLI_BYTE8 *)tok } };

   // Underscores are ignored in numbers
   char clean[1024];
   if (conv != 's' && conv != 'c') {
      size_t n = 0;
      for (const char *p = tok; *p != '\0' && n + 1 < sizeof(clean); p++) {
         if (*p != '_')
            clean[n++] = *p;
      }

      clean[n] = '\0';
      value.value.str = clean;
   }

   switch (conv) {
   case 'd': value.format = vpiDecStrVal; break;
   case 'b': value.format = vpiBinStrVal; break;
   case 'o': value.format = vpiOctStrVal; break;
   case 'h':
   case 'x': value.format = vpiHexStrVal; break;
   case 's': value.format = vpiStringVal; break;
   case 'f':
   case 'e':
   case 'g':
      value.format = vpiRealVal;
      value.value.real = strtod(clean, NULL);
      break;
   case 'c':
      value.format = vpiIntVal;
      value.value.integer = (unsigned char)tok[0];
      break;
   default:
      return false;
   }

   vpi_put_value(arg, &value, NULL, vpiNoDelay);
   return !vpi_chk_error(NULL);
}

static PLI_INT32 scan_format(scan_src_t *src, const char *fmt, vpiHandle argv)
{
   int assigned = 0;

   for (const char *p = fmt; *p; p++) {
      if (isspace_iso88591(*p)) {
         scan_skip_ws(src);
         continue;
      }

      if (*p != '%') {
         const int ch = scan_getc(src);
         if (ch == EOF)
            break;
         else if (ch != *p) {
            scan_ungetc(src, ch);
            break;
         }
         continue;
      }

      p++;
      if (*p == '%') {
         const int ch = scan_getc(src);
         if (ch != '%') {
            scan_ungetc(src, ch);
            break;
         }
         continue;
      }

      while (isdigit_iso88591(*p))
         p++;

      char tok[1024];
      if (*p == 'c') {
         const int ch = scan_getc(src);
         if (ch == EOF)
            break;
         tok[0] = ch;
         tok[1] = '\0';
      }
      else if (*p == 's') {
         if (!scan_token(src, tok, sizeof(tok)))
            break;
      }
      else if (*p == 'd' || *p == 'b' || *p == 'o' || *p == 'h'
               || *p == 'x' || *p == 'f' || *p == 'e' || *p == 'g') {
         if (!scan_digits(src, *p, tok, sizeof(tok)))
            break;
      }
      else
         jit_msg(NULL, DIAG_WARN, "unknown scanf format specifier '%c'", *p);

      vpiHandle arg = vpi_scan(argv);
      if (arg == NULL)
         break;

      if (scan_assign(arg, *p, tok))
         assigned++;

      vpi_release_handle(arg);
   }

   return assigned == 0 && src->eof ? -1 : assigned;
}

static PLI_INT32 fscanf_tf(PLI_BYTE8 *userdata)
{
   vpiHandle call = vpi_handle(vpiSysTfCall, NULL);
   assert(call != NULL);

   vpiHandle argv = vpi_iterate(vpiArgument, call);
   vpiHandle fd_arg = vpi_scan(argv);
   vpiHandle fmt_arg = vpi_scan(argv);

   PLI_INT32 result = -1;
   if (fd_arg != NULL && fmt_arg != NULL) {
      FILE *f = get_file_stream(fd_arg, false);
      if (f != NULL) {
         s_vpi_value fmt = { .format = vpiStringVal };
         vpi_get_value(fmt_arg, &fmt);

         scan_src_t src = { .file = f };
         result = scan_format(&src, fmt.value.str, argv);
      }
   }

   put_int_result(result);

   if (fmt_arg != NULL) vpi_release_handle(fmt_arg);
   if (fd_arg != NULL) vpi_release_handle(fd_arg);
   vpi_release_handle(argv);
   vpi_release_handle(call);
   return 0;
}

static PLI_INT32 sscanf_tf(PLI_BYTE8 *userdata)
{
   vpiHandle call = vpi_handle(vpiSysTfCall, NULL);
   assert(call != NULL);

   vpiHandle argv = vpi_iterate(vpiArgument, call);
   vpiHandle str_arg = vpi_scan(argv);
   vpiHandle fmt_arg = vpi_scan(argv);

   PLI_INT32 result = -1;
   if (str_arg != NULL && fmt_arg != NULL) {
      s_vpi_value str = { .format = vpiStringVal };
      s_vpi_value fmt = { .format = vpiStringVal };
      vpi_get_value(str_arg, &str);
      char *copy LOCAL = xstrdup(str.value.str);
      vpi_get_value(fmt_arg, &fmt);

      scan_src_t src = { .str = copy };
      result = scan_format(&src, fmt.value.str, argv);
   }

   put_int_result(result);

   if (fmt_arg != NULL) vpi_release_handle(fmt_arg);
   if (str_arg != NULL) vpi_release_handle(str_arg);
   vpi_release_handle(argv);
   vpi_release_handle(call);
   return 0;
}

static PLI_INT32 fgets_tf(PLI_BYTE8 *userdata)
{
   vpiHandle call = vpi_handle(vpiSysTfCall, NULL);
   assert(call != NULL);

   vpiHandle argv = vpi_iterate(vpiArgument, call);
   vpiHandle str_arg = vpi_scan(argv);
   vpiHandle fd_arg = vpi_scan(argv);

   PLI_INT32 result = 0;
   if (str_arg != NULL && fd_arg != NULL) {
      FILE *f = get_file_stream(fd_arg, false);

      char *line LOCAL = NULL;
      size_t line_len = 0;
      ssize_t nread = f != NULL ? getline(&line, &line_len, f) : -1;
      if (nread != -1) {
         result = nread;
         s_vpi_value value = {
            .format = vpiStringVal,
            .value = { .str = (PLI_BYTE8 *)line },
         };
         vpi_put_value(str_arg, &value, NULL, vpiNoDelay);
      }
      else if (vpi_get(vpiSize, str_arg) == -3) {   // A string variable
         // At the end of the file the string is empty so that a loop
         // which stops on an empty line terminates
         s_vpi_value value = {
            .format = vpiStringVal,
            .value = { .str = (PLI_BYTE8 *)"" },
         };
         vpi_put_value(str_arg, &value, NULL, vpiNoDelay);
      }
   }

   put_int_result(result);

   if (fd_arg != NULL) vpi_release_handle(fd_arg);
   if (str_arg != NULL) vpi_release_handle(str_arg);
   vpi_release_handle(argv);
   vpi_release_handle(call);
   return 0;
}

static bool sv_finishing = false;   // No postponed output after $finish

static PLI_INT32 finish_tf(PLI_BYTE8 *userdata)
{
   sv_finishing = true;
   model_finish(get_model());

   vpiHandle call = vpi_handle(vpiSysTfCall, NULL);
   assert(call != NULL);

   vpiHandle argv = vpi_iterate(vpiArgument, call);
   vpiHandle status = vpi_scan(argv);

   if (status != NULL) {
      PLI_INT32 val = get_int_arg(status);

      vpi_release_handle(argv);
      vpi_release_handle(status);

      notef("$finish called with status %d", val);
      vpi_abort_call();
      jit_abort_with_status(val);
   }
   else {
      notef("$finish called");
      vpi_abort_call();
      jit_abort();
   }
}

static PLI_INT32 stop_tf(PLI_BYTE8 *userdata)
{
   sv_finishing = true;
   model_finish(get_model());

   // There is no interactive prompt so stopping ends the simulation as
   // $finish does, the optional argument only selects diagnostics.  The
   // simulation cannot be resumed so the exit status shows that it did
   // not finish.

   vpiHandle call = vpi_handle(vpiSysTfCall, NULL);
   assert(call != NULL);

   vpiHandle argv = vpi_iterate(vpiArgument, call);
   if (argv != NULL) {
      vpiHandle arg = vpi_scan(argv);
      if (arg != NULL)
         vpi_release_handle(arg);
      vpi_release_handle(argv);
   }

   notef("$stop called, simulation stopped");
   vpi_abort_call();
   jit_abort_with_status(1);
}

static PLI_INT32 diag_tf(PLI_BYTE8 *userdata)
{
   vpiHandle call = vpi_handle(vpiSysTfCall, NULL);
   assert(call != NULL);

   vpiHandle argv = vpi_iterate(vpiArgument, call);

   LOCAL_TEXT_BUF tb = tb_new();

   ostream_t os = {
      .callback = tb_ostream_write,
      .context = tb,
      .charset = CHARSET_ISO88591,
   };

   verilog_printf(&os, 'd', argv);

   diag_t *d = diag_new((uintptr_t)userdata, NULL);
   diag_write(d, tb_get(tb), tb_len(tb));
   diag_show_source(d, false);

   if ((uintptr_t)userdata == DIAG_ERROR)
      emit_vhdl_diag(d, SEVERITY_ERROR);
   else {
      notify_assert_hook(d, (uintptr_t)userdata == DIAG_WARN
                         ? SEVERITY_WARNING : SEVERITY_NOTE);
      diag_emit(d);
   }

   vpi_release_handle(argv);
   vpi_release_handle(call);

   return 0;
}

static PLI_INT32 fatal_tf(PLI_BYTE8 *userdata)
{
   vpiHandle call = vpi_handle(vpiSysTfCall, NULL);
   assert(call != NULL);

   vpiHandle argv = vpi_iterate(vpiArgument, call);

   vpiHandle status = vpi_scan(argv);
   vpi_release_handle(status);  // TODO

   LOCAL_TEXT_BUF tb = tb_new();

   ostream_t os = {
      .callback = tb_ostream_write,
      .context = tb,
      .charset = CHARSET_ISO88591,
   };

   verilog_printf(&os, 'd', argv);

   if (get_model_or_null() == NULL) {
      // A constant function called while elaborating, such as the value
      // of a parameter, has no simulation to end: the message is shown
      // and evaluation carries on
      diag_t *d = diag_new(DIAG_WARN, NULL);
      diag_printf(d, "ignored $fatal during elaboration: ");
      diag_write(d, tb_get(tb), tb_len(tb));
      diag_show_source(d, false);
      diag_emit(d);

      vpi_release_handle(argv);
      vpi_release_handle(call);
      return 0;
   }

   diag_t *d = diag_new(DIAG_FATAL, NULL);
   diag_write(d, tb_get(tb), tb_len(tb));
   diag_show_source(d, false);
   notify_assert_hook(d, SEVERITY_FAILURE);
   diag_emit(d);

   vpi_release_handle(argv);
   vpi_release_handle(call);

   sv_finishing = true;
   model_finish(get_model());

   vpi_abort_call();
   jit_abort_with_status(1);
   return 0;
}

typedef struct {
   const char *scope;    // Instance containing the call
   int         n;        // Call site in the module
   vpiHandle   kick;     // Printing the new values
   unsigned    pending;  // Calls of $strobe in this time step
   bool        fmon;     // $fmonitor
   bool        active;
   PLI_INT32   fd;
   char       *last;
   int64_t     last_time;
   int64_t     kick_time;  // Last time step with a call or $monitoron
   rt_proc_t **dprocs;     // Processes of pending deferred reports
   uint32_t   *druns;
   uint32_t   *dseqs;
   int64_t   **dscopes;    // Blocks around each pending report
   unsigned   *dnscopes;
   unsigned    dhead, dcount, dmax;
   int64_t    *scopes;     // Blocks around the next evaluation
   unsigned    nscopes;
} monitor_t;

static A(monitor_t) monitors = AINIT;
static bool monitor_enabled = true;

static monitor_t *monitor_find(int n, bool create)
{
   const char *scope = vpi_caller_scope_name();
   if (scope == NULL)
      scope = "";

   for (unsigned i = 0; i < monitors.count; i++) {
      if (monitors.items[i].n == n && !strcmp(monitors.items[i].scope, scope))
         return &(monitors.items[i]);
   }

   if (!create)
      return NULL;

   monitor_t m = {
      .scope     = scope,
      .n         = n,
      .last_time = -1,
      .kick_time = -1,
   };
   APUSH(monitors, m);
   return &(monitors.items[monitors.count - 1]);
}

static void monitor_toggle(vpiHandle kick)
{
   s_vpi_value value = { .format = vpiBinStrVal };
   vpi_get_value(kick, &value);

   const bool was_one = value.value.str[0] == '1';
   s_vpi_value next = {
      .format = vpiBinStrVal,
      .value = { .str = (PLI_BYTE8 *)(was_one ? "0" : "1") },
   };
   vpi_put_value(kick, &next, NULL, vpiNoDelay);
}

static void monitor_release_cb(rt_model_t *m, void *user)
{
   for (unsigned i = 0; i < monitors.count; i++) {
      if (monitors.items[i].kick != NULL)
         vpi_release_handle(monitors.items[i].kick);
      free(monitors.items[i].last);
      free(monitors.items[i].dprocs);
      free(monitors.items[i].druns);
      free(monitors.items[i].dseqs);
      for (unsigned j = monitors.items[i].dhead; j < monitors.items[i].dcount;
           j++)
         free(monitors.items[i].dscopes[j]);
      free(monitors.items[i].dscopes);
      free(monitors.items[i].dnscopes);
      free(monitors.items[i].scopes);
   }

   ACLEAR(monitors);
}

static PLI_INT32 monitor_start_tf(PLI_BYTE8 *userdata)
{
   vpiHandle call = vpi_handle(vpiSysTfCall, NULL);
   vpiHandle it = vpi_iterate(vpiArgument, call);
   const char kind = *(char *)userdata;

   vpiHandle n_arg = vpi_scan(it);
   const int n = get_int_arg(n_arg);
   vpi_release_handle(n_arg);

   if (monitors.count == 0)
      model_set_phase_cb(get_model(), END_OF_SIMULATION, monitor_release_cb,
                         NULL);

   monitor_t *m = monitor_find(n, true);

   vpiHandle kick = vpi_scan(it);
   if (kind == 's') {
      m->pending++;
      if (kick != NULL) {
         monitor_toggle(kick);
         vpi_release_handle(kick);
      }
   }
   else {
      m->fmon = kind == 'f';

      if (!m->fmon) {
         // Only the latest call of $monitor is active
         for (unsigned i = 0; i < monitors.count; i++) {
            if (!monitors.items[i].fmon)
               monitors.items[i].active = false;
         }

         monitor_enabled = true;
      }

      m->active = true;

      if (m->kick == NULL)
         m->kick = kick;
      else if (kick != NULL)
         vpi_release_handle(kick);

      if (m->kick != NULL) {
         m->kick_time = model_now(get_model(), NULL);
         monitor_toggle(m->kick);
      }
   }

   vpi_release_handle(call);
   return 0;
}

static PLI_INT32 defer_tf(PLI_BYTE8 *userdata)
{
   vpiHandle call = vpi_handle(vpiSysTfCall, NULL);
   vpiHandle it = vpi_iterate(vpiArgument, call);

   vpiHandle n_arg = vpi_scan(it);
   const int n = get_int_arg(n_arg);
   vpi_release_handle(n_arg);

   if (monitors.count == 0)
      model_set_phase_cb(get_model(), END_OF_SIMULATION, monitor_release_cb,
                         NULL);

   // The assertion reports in the order it was first evaluated in this
   // time step
   monitor_t *m = monitor_find(-1 - n, true);
   const int64_t now = model_now(get_model(), NULL);

   vpiHandle kick = vpi_scan(it);
   if (kick == NULL) {
      // The pending report has matured: a later evaluation in the same
      // time step reports again
      m->kick_time = -1;
      vpi_release_handle(call);
      return 0;
   }

   vpiHandle zero = vpi_scan(it);
   if (zero != NULL) {
      // The next queued report is shown unless its process has reached a
      // flush point since the evaluation
      bool live = true;
      if (m->dhead < m->dcount) {
         live = model_defer_live(m->dprocs[m->dhead], m->druns[m->dhead],
                                 m->dseqs[m->dhead], m->dscopes[m->dhead],
                                 m->dnscopes[m->dhead]);
         free(m->dscopes[m->dhead]);
         if (++m->dhead == m->dcount)
            m->dhead = m->dcount = 0;
      }

      s_vpi_value value = {
         .format = vpiIntVal,
         .value = { .integer = live }
      };
      vpi_put_value(kick, &value, NULL, vpiNoDelay);
      vpi_release_handle(zero);
      vpi_release_handle(kick);
      vpi_release_handle(call);
      return 0;
   }

   if (m->dcount == m->dmax) {
      m->dmax = MAX(m->dmax * 2, 16);
      m->dprocs = xrealloc_array(m->dprocs, m->dmax, sizeof(rt_proc_t *));
      m->druns = xrealloc_array(m->druns, m->dmax, sizeof(uint32_t));
      m->dseqs = xrealloc_array(m->dseqs, m->dmax, sizeof(uint32_t));
      m->dscopes = xrealloc_array(m->dscopes, m->dmax, sizeof(int64_t *));
      m->dnscopes = xrealloc_array(m->dnscopes, m->dmax, sizeof(unsigned));
   }
   m->dprocs[m->dcount] = model_defer_note(&(m->druns[m->dcount]),
                                           &(m->dseqs[m->dcount]));
   m->dscopes[m->dcount] = m->scopes;
   m->dnscopes[m->dcount] = m->nscopes;
   m->scopes = NULL;
   m->nscopes = 0;
   m->dcount++;

   if (m->kick_time != now) {
      m->kick_time = now;
      monitor_toggle(kick);
   }
   vpi_release_handle(kick);

   vpi_release_handle(call);
   return 0;
}

static PLI_INT32 defer_scope_tf(PLI_BYTE8 *userdata)
{
   // The blocks around the deferred assertion that evaluates next, given
   // by hash of their names, so that a disable can cancel its report

   vpiHandle call = vpi_handle(vpiSysTfCall, NULL);
   vpiHandle it = vpi_iterate(vpiArgument, call);

   vpiHandle n_arg = vpi_scan(it);
   const int n = get_int_arg(n_arg);
   vpi_release_handle(n_arg);

   if (monitors.count == 0)
      model_set_phase_cb(get_model(), END_OF_SIMULATION, monitor_release_cb,
                         NULL);

   monitor_t *m = monitor_find(-1 - n, true);
   free(m->scopes);
   m->scopes = NULL;
   m->nscopes = 0;

   for (vpiHandle arg; (arg = vpi_scan(it)) != NULL; ) {
      m->scopes = xrealloc_array(m->scopes, m->nscopes + 1, sizeof(int64_t));
      m->scopes[m->nscopes++] = get_int_arg(arg);
      vpi_release_handle(arg);
   }

   vpi_release_handle(call);
   return 0;
}

static PLI_INT32 monitor_print_tf(PLI_BYTE8 *userdata)
{
   vpiHandle call = vpi_handle(vpiSysTfCall, NULL);
   vpiHandle it = vpi_iterate(vpiArgument, call);
   const char kind = tolower(*(char *)userdata);
   const bool strobe = kind == 's' || kind == 't';
   const bool to_fd = kind == 'f' || kind == 't';
   const bool kicked = isupper(*(char *)userdata);

   vpiHandle n_arg = vpi_scan(it);
   const int n = get_int_arg(n_arg);
   vpi_release_handle(n_arg);

   vpiHandle r_arg = vpi_scan(it);
   const char radix = get_int_arg(r_arg);
   vpi_release_handle(r_arg);

   monitor_t *m = monitor_find(n, false);

   sv_outputs_t out = { .count = 0 };
   bool to_file = false;
   if (to_fd) {
      vpiHandle fd_arg = vpi_scan(it);
      if (fd_arg != NULL) {
         const PLI_INT32 fd = get_int_arg(fd_arg);
         if (m != NULL)
            m->fd = fd;
         to_file = sv_outputs(fd, &out, true);
         vpi_release_handle(fd_arg);
      }
   }

   unsigned times = 0;
   if (sv_finishing || m == NULL)
      times = 0;
   else if (!strobe && !kicked
            && (m->kick_time == model_now(get_model(), NULL)
                || m->last_time == model_now(get_model(), NULL)))
      times = 0;   // Printed with the call or by another argument
   else if (strobe) {
      times = to_fd && !to_file ? 0 : m->pending;
      m->pending = 0;
   }
   else if (m->active && monitor_enabled && (!to_fd || to_file))
      times = 1;

   if (times == 0) {
      vpi_release_handle(it);
      vpi_release_handle(call);
      return 0;
   }

   LOCAL_TEXT_BUF tb = tb_new();
   ostream_t os = {
      .callback = tb_ostream_write,
      .context = tb,
      .charset = CHARSET_ISO88591,
   };
   verilog_printf(&os, radix, it);
   ostream_putc(&os, '\n');

   // The same line is printed for each call in this time step
   for (unsigned i = 1; i < times; i++) {
      vpiHandle again = vpi_iterate(vpiArgument, call);
      vpi_release_handle(vpi_scan(again));
      vpi_release_handle(vpi_scan(again));
      if (to_fd)
         vpi_release_handle(vpi_scan(again));
      verilog_printf(&os, radix, again);
      ostream_putc(&os, '\n');
   }

   if (to_file)
      sv_outputs_write(&out, tb_get(tb), tb_len(tb));
   else
      ostream_write(nvc_stdout(), tb_get(tb), tb_len(tb));

   if (!strobe) {
      free(m->last);
      m->last = xstrdup(tb_get(tb));
      m->last_time = model_now(get_model(), NULL);
   }

   vpi_release_handle(call);
   return 0;
}

static PLI_INT32 monitor_switch_tf(PLI_BYTE8 *userdata)
{
   const bool on = *(char *)userdata == '1';

   if (!on) {
      monitor_enabled = false;
      return 0;
   }

   // Turning monitoring on prints the current values of each active
   // monitor once in this time step, even if it was already on (21.2.3)
   monitor_enabled = true;

   const int64_t now = model_now(get_model(), NULL);
   for (unsigned i = 0; i < monitors.count; i++) {
      monitor_t *m = &(monitors.items[i]);
      if (m->active && m->kick != NULL && m->kick_time != now) {
         m->kick_time = now;
         monitor_toggle(m->kick);
      }
   }

   return 0;
}

typedef struct {
   vpiHandle    mem;
   const char  *file_name;
   bool         binary;
   int64_t      low;
   int64_t      high;
   int          width;
} readmem_t;

static int64_t readmem_range_end(vpiHandle mem, PLI_INT32 which)
{
   vpiHandle h = vpi_handle(which, mem);
   if (h == NULL)
      return 0;

   s_vpi_value value = { .format = vpiIntVal };
   vpi_get_value(h, &value);
   vpi_release_handle(h);
   return value.value.integer;
}

static bool readmem_store(const readmem_t *r, int64_t addr, const char *digits,
                          int line)
{
   // The value is the digits with underscores removed which are extended
   // to the width of the word

   LOCAL_TEXT_BUF tb = tb_new();
   for (const char *p = digits; *p != '\0'; p++) {
      if (*p != '_')
         tb_append(tb, *p);
   }

   const char *word = tb_get(tb);
   const size_t ndigits = tb_len(tb);

   const size_t per = r->binary ? 1 : 4;
   const size_t maxdigits = (r->width + per - 1) / per;

   for (const char *p = word; *p != '\0'; p++) {
      const char c = tolower((unsigned char)*p);
      const bool ok = r->binary
         ? (c == '0' || c == '1' || c == 'x' || c == 'z' || c == '?')
         : (isxdigit((unsigned char)c) || c == 'x' || c == 'z' || c == '?');
      if (!ok) {
         jit_msg(NULL, DIAG_WARN, "illegal character '%c' in data on line %d "
                 "of file \"%s\"", *p, line, r->file_name);
         return false;
      }
   }

   if (ndigits > maxdigits) {
      jit_msg(NULL, DIAG_WARN, "too many digits (%zu) in data on line %d of "
              "file \"%s\" (the maximum is %zu)", ndigits, line,
              r->file_name, maxdigits);
      return false;
   }
   else if (ndigits == 0)
      return true;

   vpiHandle elem = vpi_handle_by_index(r->mem, addr);
   if (elem == NULL)
      return false;

   s_vpi_value val = {
      .format = r->binary ? vpiBinStrVal : vpiHexStrVal,
      .value.str = (PLI_BYTE8 *)word,
   };
   vpi_put_value(elem, &val, NULL, vpiNoDelay);
   vpi_release_handle(elem);
   return true;
}

static PLI_INT32 readmem_tf(PLI_BYTE8 *userdata)
{
   vpiHandle callh = vpi_handle(vpiSysTfCall, 0);
   vpiHandle argv = vpi_iterate(vpiArgument, callh);

   vpiHandle file_arg = vpi_scan(argv);
   vpiHandle mem_arg = vpi_scan(argv);

   s_vpi_value file = { .format = vpiStringVal };
   vpi_get_value(file_arg, &file);
   char *file_name LOCAL = xstrdup(file.value.str);
   vpi_release_handle(file_arg);

   bool has_start = false, has_finish = false;
   int64_t start = 0, finish = 0;

   vpiHandle start_arg = vpi_scan(argv);
   if (start_arg != NULL) {
      start = get_int_arg(start_arg);
      has_start = true;
      vpi_release_handle(start_arg);

      vpiHandle finish_arg = vpi_scan(argv);
      if (finish_arg != NULL) {
         finish = get_int_arg(finish_arg);
         has_finish = true;
         vpi_release_handle(finish_arg);
      }
   }

   file_handle_t fh = file_open(file_name, FILE_READ);
   FILE *f = file_stream(fh);
   if (f == NULL)
      jit_msg(NULL, DIAG_FATAL, "failed to open %s: %s",
              file_name, strerror(errno));

   readmem_t r = {
      .mem       = mem_arg,
      .file_name = file_name,
      .binary    = *(char *)userdata == 'b',
   };

   const int64_t left = readmem_range_end(mem_arg, vpiLeftRange);
   const int64_t right = readmem_range_end(mem_arg, vpiRightRange);
   r.low = MIN(left, right);
   r.high = MAX(left, right);

   vpiHandle first = vpi_handle_by_index(mem_arg, r.low);
   r.width = first != NULL ? vpi_get(vpiSize, first) : 1;
   if (first != NULL)
      vpi_release_handle(first);

   // The addresses visited go from the start to the finish
   int64_t addr = has_start ? start : r.low;
   int64_t last = has_finish ? finish : r.high;
   const int step = last < addr ? -1 : 1;

   bool done = false;
   int line = 1;
   int c = fgetc(f);
   while (c != EOF && !done) {
      if (c == '\n') {
         line++;
         c = fgetc(f);
      }
      else if (isspace(c))
         c = fgetc(f);
      else if (c == '/') {
         const int next = fgetc(f);
         if (next == '/') {
            while ((c = fgetc(f)) != EOF && c != '\n')
               ;
         }
         else if (next == '*') {
            int prev = 0;
            while ((c = fgetc(f)) != EOF && !(prev == '*' && c == '/')) {
               if (c == '\n')
                  line++;
               prev = c;
            }
            c = fgetc(f);
         }
         else {
            jit_msg(NULL, DIAG_WARN, "illegal character '/' in data on line "
                    "%d of file \"%s\"", line, file_name);
            c = next;
         }
      }
      else {
         LOCAL_TEXT_BUF tok = tb_new();
         const bool is_addr = c == '@';
         if (is_addr)
            c = fgetc(f);

         while (c != EOF && !isspace(c) && c != '/') {
            tb_append(tok, c);
            c = fgetc(f);
         }

         if (is_addr) {
            char *end;
            const int64_t a = strtoll(tb_get(tok), &end, 16);
            if (*end != '\0')
               jit_msg(NULL, DIAG_WARN, "illegal address on line %d of file "
                       "\"%s\"", line, file_name);
            else if (a < r.low || a > r.high) {
               jit_msg(NULL, DIAG_WARN, "memory address %"PRIi64" read on "
                       "line %d of file \"%s\" is out of bounds",
                       a, line, file_name);
               done = true;
            }
            else {
               addr = a;
            }
         }
         else {
            const int64_t lo = has_start || has_finish
               ? MIN(has_start ? start : r.low, last) : r.low;
            const int64_t hi = has_start || has_finish
               ? MAX(has_start ? start : r.low, last) : r.high;
            if (addr < lo || addr > hi) {
               jit_msg(NULL, DIAG_WARN, "too many data words read on line %d "
                       "of file \"%s\" (the address range is [%"PRIi64":%"
                       PRIi64"])", line, file_name, lo, hi);
               done = true;
            }
            else if (readmem_store(&r, addr, tb_get(tok), line))
               addr += step;
            else
               done = true;
         }
      }
   }

   file_close(fh);

   vpi_release_handle(mem_arg);
   vpi_release_handle(argv);
   vpi_release_handle(callh);
   return 0;
}

static PLI_INT32 writemem_tf(PLI_BYTE8 *userdata)
{
   vpiHandle callh = vpi_handle(vpiSysTfCall, 0);
   vpiHandle argv = vpi_iterate(vpiArgument, callh);

   vpiHandle file_arg = vpi_scan(argv);
   vpiHandle mem_arg = vpi_scan(argv);

   s_vpi_value file = { .format = vpiStringVal };
   vpi_get_value(file_arg, &file);
   char *file_name LOCAL = xstrdup(file.value.str);
   vpi_release_handle(file_arg);

   const int64_t left = readmem_range_end(mem_arg, vpiLeftRange);
   const int64_t right = readmem_range_end(mem_arg, vpiRightRange);
   int64_t first = MIN(left, right), last = MAX(left, right);

   vpiHandle start_arg = vpi_scan(argv);
   if (start_arg != NULL) {
      first = get_int_arg(start_arg);
      vpi_release_handle(start_arg);

      vpiHandle finish_arg = vpi_scan(argv);
      if (finish_arg != NULL) {
         last = get_int_arg(finish_arg);
         vpi_release_handle(finish_arg);
      }
   }

   file_handle_t fh = file_open(file_name, FILE_WRITE);
   FILE *f = file_stream(fh);
   if (f == NULL)
      jit_msg(NULL, DIAG_FATAL, "failed to open %s: %s",
              file_name, strerror(errno));

   const char radix = *(char *)userdata;
   const int step = last < first ? -1 : 1;
   for (int64_t addr = first;; addr += step) {
      vpiHandle elem = vpi_handle_by_index(mem_arg, addr);
      if (elem != NULL) {
         s_vpi_value val = { .format = radix == 'b' ? vpiBinStrVal
                             : vpiHexStrVal };
         vpi_get_value(elem, &val);
         fprintf(f, "%s\n", val.value.str);
         vpi_release_handle(elem);
      }

      if (addr == last)
         break;
   }

   file_close(fh);

   vpi_release_handle(mem_arg);
   vpi_release_handle(argv);
   vpi_release_handle(callh);
   return 0;
}

static PLI_INT32 time_tf(PLI_BYTE8 *userdata)
{
   rt_model_t *m = get_model();
   const int64_t scale = vpi_caller_timescale();
   const int64_t now = (model_now(m, NULL) + scale / 2) / scale;

   s_vpi_time timeval = {
      .type = vpiSimTime,
      .high = now >> 32,
      .low  = now & 0xffffffff
   };

   s_vpi_value result = {
      .format = vpiTimeVal,
      .value = { .time = &timeval },
   };

   vpiHandle call = vpi_handle(vpiSysTfCall, NULL);
   assert(call != NULL);

   vpi_put_value(call, &result, NULL, 0);

   vpi_release_handle(call);
   return 0;
}

static PLI_INT32 stime_tf(PLI_BYTE8 *userdata)
{
   rt_model_t *m = get_model();
   const int64_t scale = vpi_caller_timescale();
   const int64_t now = (model_now(m, NULL) + scale / 2) / scale;

   s_vpi_value result = {
      .format = vpiIntVal,
      .value = { .integer = now & 0xffffffff },
   };

   vpiHandle call = vpi_handle(vpiSysTfCall, NULL);
   assert(call != NULL);

   vpi_put_value(call, &result, NULL, 0);

   vpi_release_handle(call);
   return 0;
}

static PLI_INT32 random_tf(PLI_BYTE8 *userdata)
{
   static __thread int32_t i_seed;
   INIT_ONCE(i_seed = opt_verilog_seed());

   int32_t a_seed = i_seed;

   vpiHandle call = vpi_handle(vpiSysTfCall, NULL);
   assert(call != NULL);

   vpiHandle argv = vpi_iterate(vpiArgument, call);

   vpiHandle seed = vpi_scan(argv);
   if (seed != NULL) {
      s_vpi_value val = { .format = vpiIntVal };
      vpi_get_value(seed, &val);

      a_seed = val.value.integer;
   }

   vpi_release_handle(argv);

   // A process with its own random stream does not use the default one
   uint64_t draw;
   if (seed == NULL && model_rng_draw(&draw)) {
      s_vpi_value own = {
         .format = vpiIntVal,
         .value = { .integer = (int32_t)draw },
      };
      vpi_put_value(call, &own, NULL, 0);
      vpi_release_handle(call);
      return 0;
   }

   s_vpi_value result = {
      .format = vpiIntVal,
      .value = { .integer = rtl_dist_uniform(&a_seed, INT32_MIN, INT32_MAX) },
   };

   vpi_put_value(call, &result, NULL, 0);

   if (seed != NULL) {
      s_vpi_value next_seed = {
         .format = vpiIntVal,
         .value = { .integer = a_seed },
      };
      vpi_put_value(seed, &next_seed, NULL, 0);
      vpi_release_handle(seed);
   }
   else
      i_seed = a_seed;

   vpi_release_handle(call);
   return 0;
}

static PLI_INT32 urandom_tf(PLI_BYTE8 *userdata)
{
   // The LRM does not specify the sequence (18.13.1)
   static __thread int32_t i_seed;
   INIT_ONCE(i_seed = opt_verilog_seed());

   vpiHandle call = vpi_handle(vpiSysTfCall, NULL);
   assert(call != NULL);

   int32_t a_seed = 0, *seedp = &i_seed;

   vpiHandle argv = vpi_iterate(vpiArgument, call);
   vpiHandle seed = vpi_scan(argv);
   if (seed != NULL) {
      s_vpi_value val = { .format = vpiIntVal };
      vpi_get_value(seed, &val);
      a_seed = val.value.integer;
      seedp = &a_seed;
      vpi_release_handle(seed);
   }

   vpi_release_handle(argv);

   uint64_t draw;
   const bool own = seedp == &i_seed && model_rng_draw(&draw);

   s_vpi_value result = {
      .format = vpiIntVal,
      .value = { .integer = own ? (int32_t)draw
                 : rtl_dist_uniform(seedp, INT32_MIN, INT32_MAX) },
   };
   vpi_put_value(call, &result, NULL, 0);

   vpi_release_handle(call);
   return 0;
}

static PLI_INT32 urandom_range_tf(PLI_BYTE8 *userdata)
{
   static __thread int32_t i_seed;
   INIT_ONCE(i_seed = opt_verilog_seed());

   vpiHandle call = vpi_handle(vpiSysTfCall, NULL);
   assert(call != NULL);

   vpiHandle argv = vpi_iterate(vpiArgument, call);
   vpiHandle arg0 = vpi_scan(argv), arg1 = vpi_scan(argv);

   uint32_t hi = get_int_arg(arg0), lo = 0;
   vpi_release_handle(arg0);

   if (arg1 != NULL) {
      lo = get_int_arg(arg1);
      vpi_release_handle(arg1);
   }

   vpi_release_handle(argv);

   if (lo > hi) {
      const uint32_t tmp = lo;
      lo = hi;
      hi = tmp;
   }

   uint64_t draw;
   const uint32_t r = model_rng_draw(&draw)
      ? (uint32_t)draw : rtl_dist_uniform(&i_seed, INT32_MIN, INT32_MAX);
   const uint64_t range = (uint64_t)hi - lo + 1;

   s_vpi_value result = {
      .format = vpiIntVal,
      .value = { .integer = lo + (uint32_t)(r % range) },
   };
   vpi_put_value(call, &result, NULL, 0);

   vpi_release_handle(call);
   return 0;
}

typedef struct {
   char   *name;
   int     key;
   int     weight;
   int     ninst;
   double *pct;
} cov_type_t;

typedef struct {
   int type;
   int slot;
} cov_inst_t;

static cov_type_t *cov_types = NULL;
static int         ncov_types = 0;
static cov_inst_t *cov_insts = NULL;
static int         ncov_insts = 0;

static PLI_INT32 cov_key_tf(PLI_BYTE8 *userdata)
{
   static int next_key = 0;
   put_int_result(next_key++);
   return 0;
}

static PLI_INT32 cov_new_tf(PLI_BYTE8 *userdata)
{
   vpiHandle call = vpi_handle(vpiSysTfCall, NULL);
   assert(call != NULL);

   vpiHandle argv = vpi_iterate(vpiArgument, call);
   vpiHandle arg0 = vpi_scan(argv), arg1 = vpi_scan(argv);
   vpiHandle arg2 = vpi_scan(argv);

   s_vpi_value name = { .format = vpiStringVal };
   vpi_get_value(arg0, &name);

   char *name_copy LOCAL = xstrdup(name.value.str);
   const int weight = get_int_arg(arg1);
   const int key = get_int_arg(arg2);

   int type = 0;
   for (; type < ncov_types; type++) {
      if (cov_types[type].key == key
          && strcmp(cov_types[type].name, name_copy) == 0)
         break;
   }

   if (type == ncov_types) {
      cov_types = xrealloc_array(cov_types, ++ncov_types, sizeof(cov_type_t));
      cov_types[type] = (cov_type_t){
         .name = xstrdup(name_copy),
         .key = key,
      };
   }

   cov_type_t *ct = &(cov_types[type]);
   ct->weight = weight;
   ct->pct = xrealloc_array(ct->pct, ct->ninst + 1, sizeof(double));
   ct->pct[ct->ninst] = 0.0;

   cov_insts = xrealloc_array(cov_insts, ncov_insts + 1, sizeof(cov_inst_t));
   cov_insts[ncov_insts] = (cov_inst_t){ type, ct->ninst++ };

   put_int_result(ncov_insts++);

   vpi_release_handle(arg0);
   vpi_release_handle(arg1);
   vpi_release_handle(arg2);
   vpi_release_handle(argv);
   vpi_release_handle(call);
   return 0;
}

static PLI_INT32 cov_set_tf(PLI_BYTE8 *userdata)
{
   vpiHandle call = vpi_handle(vpiSysTfCall, NULL);
   assert(call != NULL);

   vpiHandle argv = vpi_iterate(vpiArgument, call);
   vpiHandle arg0 = vpi_scan(argv), arg1 = vpi_scan(argv);

   const int id = get_int_arg(arg0);

   s_vpi_value value = { .format = vpiRealVal };
   vpi_get_value(arg1, &value);

   if (id >= 0 && id < ncov_insts)
      cov_types[cov_insts[id].type].pct[cov_insts[id].slot] = value.value.real;

   vpi_release_handle(arg0);
   vpi_release_handle(arg1);
   vpi_release_handle(argv);
   vpi_release_handle(call);
   return 0;
}

static PLI_INT32 get_coverage_tf(PLI_BYTE8 *userdata)
{
   // Average of the covergroup types weighted by their type option

   double sum = 0.0, wsum = 0.0;
   for (int i = 0; i < ncov_types; i++) {
      const cov_type_t *ct = &(cov_types[i]);
      if (ct->weight <= 0 || ct->ninst == 0)
         continue;

      double mean = 0.0;
      for (int j = 0; j < ct->ninst; j++)
         mean += ct->pct[j];

      sum += ct->weight * (mean / ct->ninst);
      wsum += ct->weight;
   }

   s_vpi_value result = {
      .format = vpiRealVal,
      .value = { .real = wsum > 0.0 ? sum / wsum : 0.0 },
   };

   vpiHandle call = vpi_handle(vpiSysTfCall, NULL);
   assert(call != NULL);

   vpi_put_value(call, &result, NULL, 0);
   vpi_release_handle(call);
   return 0;
}

static PLI_INT32 set_coverage_db_name_tf(PLI_BYTE8 *userdata)
{
   // No coverage database is written so the name has no effect
   return 0;
}

static PLI_INT32 load_coverage_db_tf(PLI_BYTE8 *userdata)
{
   jit_msg(NULL, DIAG_FATAL, "sorry, $load_coverage_db is not supported as "
           "there is no coverage database");
   return 0;
}

static PLI_INT32 clog2_tf(PLI_BYTE8 *userdata)
{
   vpiHandle call = vpi_handle(vpiSysTfCall, NULL);
   assert(call != NULL);

   vpiHandle argv = vpi_iterate(vpiArgument, call);
   vpiHandle arg = vpi_scan(argv);
   assert(arg != NULL);

   s_vpi_value value = { .format = vpiIntVal };
   vpi_get_value(arg, &value);

   vpi_release_handle(arg);
   vpi_release_handle(argv);

   s_vpi_value result = {
      .format = vpiIntVal,
      .value = { .integer = ilog2(value.value.integer) },
   };

   vpi_put_value(call, &result, NULL, 0);

   vpi_release_handle(call);
   return 0;
}

static PLI_INT32 rtoi_tf(PLI_BYTE8 *userdata)
{
   vpiHandle call = vpi_handle(vpiSysTfCall, NULL);
   assert(call != NULL);

   vpiHandle argv = vpi_iterate(vpiArgument, call);
   vpiHandle arg = vpi_scan(argv);
   assert(arg != NULL);

   s_vpi_value value = { .format = vpiRealVal };
   vpi_get_value(arg, &value);

   vpi_release_handle(arg);
   vpi_release_handle(argv);

   s_vpi_vecval xval = { .aval = UINT32_MAX, .bval = UINT32_MAX };
   s_vpi_value result;
   if (isfinite(value.value.real)) {
      // Truncate towards zero (20.5) and keep the low 32 bits of the
      // 64-bit result: a magnitude too large for 64 bits gives an
      // implementation-defined value
      const double truncated = trunc(value.value.real);
      const int64_t wide = fabs(truncated) < 9.2e18 ? (int64_t)truncated
         : INT64_MIN;

      result.format = vpiIntVal;
      result.value.integer = (int32_t)(uint32_t)(uint64_t)wide;
   }
   else {
      // 6.12.1 defines no result for a NaN or infinity
      static bool warned = false;
      if (!warned) {
         jit_msg(NULL, DIAG_WARN, "$rtoi of %s gives an unknown value",
                 isnan(value.value.real) ? "NaN" : "infinity");
         warned = true;
      }

      result.format = vpiVectorVal;
      result.value.vector = &xval;
   }

   vpi_put_value(call, &result, NULL, 0);

   vpi_release_handle(call);
   return 0;
}

static PLI_INT32 itor_tf(PLI_BYTE8 *userdata)
{
   vpiHandle call = vpi_handle(vpiSysTfCall, NULL);
   assert(call != NULL);

   vpiHandle argv = vpi_iterate(vpiArgument, call);
   vpiHandle arg = vpi_scan(argv);
   assert(arg != NULL);

   s_vpi_value value = { .format = vpiIntVal };
   vpi_get_value(arg, &value);

   vpi_release_handle(arg);
   vpi_release_handle(argv);

   s_vpi_value result = {
      .format = vpiRealVal,
      .value = { .real = value.value.integer },
   };

   vpi_put_value(call, &result, NULL, 0);

   vpi_release_handle(call);
   return 0;
}

static PLI_INT32 real_unary_tf(double (*fn)(double))
{
   vpiHandle call = vpi_handle(vpiSysTfCall, NULL);
   assert(call != NULL);

   vpiHandle argv = vpi_iterate(vpiArgument, call);
   vpiHandle arg = vpi_scan(argv);
   assert(arg != NULL);

   s_vpi_value value = { .format = vpiRealVal };
   vpi_get_value(arg, &value);

   vpi_release_handle(arg);
   vpi_release_handle(argv);

   s_vpi_value result = {
      .format = vpiRealVal,
      .value = { .real = (*fn)(value.value.real) },
   };

   vpi_put_value(call, &result, NULL, 0);

   vpi_release_handle(call);
   return 0;
}

static PLI_INT32 pow_tf(PLI_BYTE8 *userdata)
{
   vpiHandle call = vpi_handle(vpiSysTfCall, NULL);
   assert(call != NULL);

   vpiHandle argv = vpi_iterate(vpiArgument, call);
   vpiHandle left = vpi_scan(argv);
   vpiHandle right = vpi_scan(argv);
   assert(left != NULL);
   assert(right != NULL);

   s_vpi_value lval = { .format = vpiRealVal };
   s_vpi_value rval = { .format = vpiRealVal };
   vpi_get_value(left, &lval);
   vpi_get_value(right, &rval);

   vpi_release_handle(left);
   vpi_release_handle(right);
   vpi_release_handle(argv);

   s_vpi_value result = {
      .format = vpiRealVal,
      .value = { .real = pow(lval.value.real, rval.value.real) },
   };

   vpi_put_value(call, &result, NULL, 0);

   vpi_release_handle(call);
   return 0;
}

static PLI_INT32 sqrt_tf(PLI_BYTE8 *userdata)
{
   return real_unary_tf(sqrt);
}

static PLI_INT32 sin_tf(PLI_BYTE8 *userdata)
{
   return real_unary_tf(sin);
}

static PLI_INT32 cos_tf(PLI_BYTE8 *userdata)
{
   return real_unary_tf(cos);
}

static PLI_INT32 tan_tf(PLI_BYTE8 *userdata)
{
   return real_unary_tf(tan);
}

static PLI_INT32 ln_tf(PLI_BYTE8 *userdata)
{
   return real_unary_tf(log);
}

static PLI_INT32 log10_tf(PLI_BYTE8 *userdata)
{
   return real_unary_tf(log10);
}

static PLI_INT32 exp_tf(PLI_BYTE8 *userdata)
{
   return real_unary_tf(exp);
}

static PLI_INT32 ceil_tf(PLI_BYTE8 *userdata)
{
   return real_unary_tf(ceil);
}

static PLI_INT32 floor_tf(PLI_BYTE8 *userdata)
{
   return real_unary_tf(floor);
}

static PLI_INT32 real_binary_tf(double (*fn)(double, double))
{
   vpiHandle call = vpi_handle(vpiSysTfCall, NULL);
   assert(call != NULL);

   vpiHandle argv = vpi_iterate(vpiArgument, call);
   vpiHandle left = vpi_scan(argv);
   vpiHandle right = vpi_scan(argv);
   assert(left != NULL);
   assert(right != NULL);

   s_vpi_value lval = { .format = vpiRealVal };
   s_vpi_value rval = { .format = vpiRealVal };
   vpi_get_value(left, &lval);
   vpi_get_value(right, &rval);

   vpi_release_handle(left);
   vpi_release_handle(right);
   vpi_release_handle(argv);

   s_vpi_value result = {
      .format = vpiRealVal,
      .value = { .real = (*fn)(lval.value.real, rval.value.real) },
   };

   vpi_put_value(call, &result, NULL, 0);

   vpi_release_handle(call);
   return 0;
}

static PLI_INT32 atan2_tf(PLI_BYTE8 *userdata)
{
   return real_binary_tf(atan2);
}

static PLI_INT32 hypot_tf(PLI_BYTE8 *userdata)
{
   return real_binary_tf(hypot);
}

static PLI_INT32 asin_tf(PLI_BYTE8 *userdata)
{
   return real_unary_tf(asin);
}

static PLI_INT32 acos_tf(PLI_BYTE8 *userdata)
{
   return real_unary_tf(acos);
}

static PLI_INT32 atan_tf(PLI_BYTE8 *userdata)
{
   return real_unary_tf(atan);
}

static PLI_INT32 sinh_tf(PLI_BYTE8 *userdata)
{
   return real_unary_tf(sinh);
}

static PLI_INT32 cosh_tf(PLI_BYTE8 *userdata)
{
   return real_unary_tf(cosh);
}

static PLI_INT32 tanh_tf(PLI_BYTE8 *userdata)
{
   return real_unary_tf(tanh);
}

static PLI_INT32 asinh_tf(PLI_BYTE8 *userdata)
{
   return real_unary_tf(asinh);
}

static PLI_INT32 acosh_tf(PLI_BYTE8 *userdata)
{
   return real_unary_tf(acosh);
}

static PLI_INT32 atanh_tf(PLI_BYTE8 *userdata)
{
   return real_unary_tf(atanh);
}

static uint64_t get_bits_arg(vpiHandle arg)
{
   s_vpi_value value = { .format = vpiBinStrVal };
   vpi_get_value(arg, &value);

   uint64_t bits = 0;
   for (const char *p = value.value.str; *p != '\0'; p++)
      bits = (bits << 1) | (*p == '1');

   return bits;
}

static PLI_INT32 realtobits_tf(PLI_BYTE8 *userdata)
{
   vpiHandle call = vpi_handle(vpiSysTfCall, NULL);
   assert(call != NULL);

   vpiHandle argv = vpi_iterate(vpiArgument, call);
   vpiHandle arg = vpi_scan(argv);
   assert(arg != NULL);

   s_vpi_value value = { .format = vpiRealVal };
   vpi_get_value(arg, &value);

   vpi_release_handle(arg);
   vpi_release_handle(argv);

   uint64_t bits;
   memcpy(&bits, &value.value.real, sizeof(bits));

   s_vpi_time timeval = {
      .type = vpiSimTime,
      .high = bits >> 32,
      .low  = bits & 0xffffffff
   };

   s_vpi_value result = {
      .format = vpiTimeVal,
      .value = { .time = &timeval },
   };

   vpi_put_value(call, &result, NULL, 0);

   vpi_release_handle(call);
   return 0;
}

static PLI_INT32 bitstoreal_tf(PLI_BYTE8 *userdata)
{
   vpiHandle call = vpi_handle(vpiSysTfCall, NULL);
   assert(call != NULL);

   vpiHandle argv = vpi_iterate(vpiArgument, call);
   vpiHandle arg = vpi_scan(argv);
   assert(arg != NULL);

   const uint64_t bits = get_bits_arg(arg);

   vpi_release_handle(arg);
   vpi_release_handle(argv);

   s_vpi_value result = { .format = vpiRealVal };
   memcpy(&result.value.real, &bits, sizeof(bits));

   vpi_put_value(call, &result, NULL, 0);

   vpi_release_handle(call);
   return 0;
}

static PLI_INT32 shortrealtobits_tf(PLI_BYTE8 *userdata)
{
   vpiHandle call = vpi_handle(vpiSysTfCall, NULL);
   assert(call != NULL);

   vpiHandle argv = vpi_iterate(vpiArgument, call);
   vpiHandle arg = vpi_scan(argv);
   assert(arg != NULL);

   s_vpi_value value = { .format = vpiRealVal };
   vpi_get_value(arg, &value);

   vpi_release_handle(arg);
   vpi_release_handle(argv);

   const float f = value.value.real;
   uint32_t bits;
   memcpy(&bits, &f, sizeof(bits));

   put_int_result(bits);

   vpi_release_handle(call);
   return 0;
}

static PLI_INT32 bitstoshortreal_tf(PLI_BYTE8 *userdata)
{
   vpiHandle call = vpi_handle(vpiSysTfCall, NULL);
   assert(call != NULL);

   vpiHandle argv = vpi_iterate(vpiArgument, call);
   vpiHandle arg = vpi_scan(argv);
   assert(arg != NULL);

   const uint32_t bits = get_bits_arg(arg);

   vpi_release_handle(arg);
   vpi_release_handle(argv);

   float f;
   memcpy(&f, &bits, sizeof(f));

   s_vpi_value result = {
      .format = vpiRealVal,
      .value = { .real = f },
   };

   vpi_put_value(call, &result, NULL, 0);

   vpi_release_handle(call);
   return 0;
}

static PLI_INT32 countbits_tf(PLI_BYTE8 *userdata)
{
   // The control bits are the remaining arguments and an x or z control
   // bit counts the bits with that value (20.9)

   vpiHandle call = vpi_handle(vpiSysTfCall, NULL);
   assert(call != NULL);

   vpiHandle argv = vpi_iterate(vpiArgument, call);
   vpiHandle arg = vpi_scan(argv);
   assert(arg != NULL);

   s_vpi_value value = { .format = vpiBinStrVal };
   vpi_get_value(arg, &value);
   vpi_release_handle(arg);

   char *bits LOCAL = xstrdup(value.value.str);

   bool match[256] = {};
   for (;;) {
      vpiHandle ctrl = vpi_scan(argv);
      if (ctrl == NULL)
         break;

      s_vpi_value cv = { .format = vpiBinStrVal };
      vpi_get_value(ctrl, &cv);
      vpi_release_handle(ctrl);

      const char c = cv.value.str[strlen(cv.value.str) - 1];
      match[(unsigned char)c] = true;
   }

   int count = 0;
   for (const char *p = bits; *p != '\0'; p++)
      count += match[(unsigned char)*p];

   put_int_result(count);

   vpi_release_handle(argv);
   vpi_release_handle(call);
   return 0;
}

static PLI_INT32 bit_query_tf(char which)
{
   vpiHandle call = vpi_handle(vpiSysTfCall, NULL);
   assert(call != NULL);

   vpiHandle argv = vpi_iterate(vpiArgument, call);
   vpiHandle arg = vpi_scan(argv);
   assert(arg != NULL);

   s_vpi_value value = { .format = vpiBinStrVal };
   vpi_get_value(arg, &value);

   int nones = 0, nunknown = 0;
   for (const char *p = value.value.str; *p != '\0'; p++) {
      nones += *p == '1';
      nunknown += *p == 'x' || *p == 'z';
   }

   vpi_release_handle(arg);
   vpi_release_handle(argv);

   switch (which) {
   case '1': put_int_result(nones); break;
   case 'h': put_int_result(nones == 1); break;
   case '0': put_int_result(nones <= 1); break;
   default:  put_int_result(nunknown > 0); break;
   }

   vpi_release_handle(call);
   return 0;
}

static PLI_INT32 countones_tf(PLI_BYTE8 *userdata)
{
   return bit_query_tf('1');
}

static PLI_INT32 onehot_tf(PLI_BYTE8 *userdata)
{
   return bit_query_tf('h');
}

static PLI_INT32 onehot0_tf(PLI_BYTE8 *userdata)
{
   return bit_query_tf('0');
}

static PLI_INT32 isunknown_tf(PLI_BYTE8 *userdata)
{
   return bit_query_tf('u');
}

static PLI_INT32 test_plusargs_tf(PLI_BYTE8 *userdata)
{
   vpiHandle call = vpi_handle(vpiSysTfCall, NULL);
   assert(call != NULL);

   vpiHandle argv = vpi_iterate(vpiArgument, call);
   vpiHandle arg = vpi_scan(argv);
   assert(arg != NULL);

   s_vpi_value value = { .format = vpiStringVal };
   vpi_get_value(arg, &value);

   const size_t len = strlen(value.value.str);

   PLI_INT32 found = 0;
   for (int i = 0; i < nplusargs && !found; i++)
      found = plusargs[i][0] == '+'
         && strncmp(plusargs[i] + 1, value.value.str, len) == 0;

   put_int_result(found);

   vpi_release_handle(arg);
   vpi_release_handle(argv);
   vpi_release_handle(call);
   return 0;
}

static PLI_INT32 value_plusargs_tf(PLI_BYTE8 *userdata)
{
   vpiHandle call = vpi_handle(vpiSysTfCall, NULL);
   assert(call != NULL);

   vpiHandle argv = vpi_iterate(vpiArgument, call);
   vpiHandle fmt_arg = vpi_scan(argv);
   vpiHandle var_arg = vpi_scan(argv);
   assert(fmt_arg != NULL);
   assert(var_arg != NULL);

   s_vpi_value fmt = { .format = vpiStringVal };
   vpi_get_value(fmt_arg, &fmt);

   // The format is a prefix followed by a conversion
   const char *pct = strchr(fmt.value.str, '%');
   PLI_INT32 found = 0;
   if (pct != NULL) {
      const size_t len = pct - fmt.value.str;
      const char *conv = pct + 1;
      while (isdigit_iso88591(*conv))
         conv++;

      for (int i = 0; i < nplusargs && !found; i++) {
         if (plusargs[i][0] != '+'
             || strncmp(plusargs[i] + 1, fmt.value.str, len) != 0)
            continue;

         const char *text = plusargs[i] + 1 + len;
         char *clean LOCAL = xstrdup(text);
         if (*conv != 's') {
            // Underscores in a number are ignored
            char *wp = clean;
            for (const char *rp = text; *rp != '\0'; rp++) {
               if (*rp != '_')
                  *wp++ = *rp;
            }
            *wp = '\0';
            text = clean;
         }
         s_vpi_value value = { .value = { .str = (PLI_BYTE8 *)text } };
         switch (*conv) {
         case 'd': value.format = vpiDecStrVal; break;
         case 'b': value.format = vpiBinStrVal; break;
         case 'o': value.format = vpiOctStrVal; break;
         case 'h':
         case 'x': value.format = vpiHexStrVal; break;
         case 's': value.format = vpiStringVal; break;
         case 'e':
         case 'f':
         case 'g':
            value.format = vpiRealVal;
            value.value.real = strtod(text, NULL);
            break;
         default:
            continue;
         }

         vpi_put_value(var_arg, &value, NULL, vpiNoDelay);
         found = 1;
      }
   }

   put_int_result(found);

   vpi_release_handle(fmt_arg);
   vpi_release_handle(var_arg);
   vpi_release_handle(argv);
   vpi_release_handle(call);
   return 0;
}

static void print_time_unit(int64_t fs)
{
   static const char *const units[] = { "fs", "ps", "ns", "us", "ms", "s" };

   int u = 0;
   while (fs >= 1000 && fs % 1000 == 0 && u + 1 < ARRAY_LEN(units)) {
      fs /= 1000;
      u++;
   }

   printf("%"PRId64"%s", fs, units[u]);
}

static void bad_timescale_scope(const char *name)
{
   diag_t *d = diag_new(DIAG_ERROR, NULL);
   diag_printf(d, "cannot find module instance '%s' for %s", name,
               vpi_caller_name());
   emit_vhdl_diag(d, SEVERITY_ERROR);
}

static PLI_INT32 printtimescale_tf(PLI_BYTE8 *userdata)
{
   vpiHandle call = vpi_handle(vpiSysTfCall, NULL);
   assert(call != NULL);

   LOCAL_TEXT_BUF path = tb_new();
   int64_t unit, precision;

   vpiHandle argv = vpi_iterate(vpiArgument, call);
   vpiHandle arg = argv != NULL ? vpi_scan(argv) : NULL;
   if (arg != NULL) {
      // The parser gives the hierarchical name of the instance as a string
      s_vpi_value argval = { .format = vpiStringVal };
      vpi_get_value(arg, &argval);
      vpi_release_handle(argv);
      assert(argval.value.str != NULL);

      if (!vpi_instance_timescale(argval.value.str, path, &unit, &precision)) {
         bad_timescale_scope(argval.value.str);
         vpi_release_handle(call);
         return 0;
      }
   }
   else {
      rt_model_t *m = get_model_or_null();
      rt_scope_t *scope = m != NULL ? get_active_scope(m) : NULL;
      if (scope != NULL)
         get_verilog_path_name(scope, path);
      else {
         const char *name = vpi_caller_scope_name();
         tb_cat(path, name != NULL ? name : "top");
      }

      unit = vpi_caller_timescale();
      precision = vpi_caller_precision();
   }

   // The format of the example in 20.4.2
   printf("Time scale of (%s) is ", tb_get(path));
   print_time_unit(unit);
   printf(" / ");
   print_time_unit(precision);
   printf("\n");

   vpi_release_handle(call);
   return 0;
}

static int time_log10(int64_t fs)
{
   // Power of ten of a time in seconds given in femtoseconds

   int log10 = -15;
   for (; fs >= 10; fs /= 10)
      log10++;
   return log10;
}

static PLI_INT32 timeunit_tf(PLI_BYTE8 *userdata)
{
   vpiHandle call = vpi_handle(vpiSysTfCall, NULL);
   assert(call != NULL);

   const bool precision = userdata != NULL;

   int64_t unit, prec;

   vpiHandle argv = vpi_iterate(vpiArgument, call);
   vpiHandle arg = argv != NULL ? vpi_scan(argv) : NULL;
   if (arg != NULL) {
      // The parser gives the hierarchical name of the instance as a string
      s_vpi_value argval = { .format = vpiStringVal };
      vpi_get_value(arg, &argval);
      vpi_release_handle(argv);
      assert(argval.value.str != NULL);

      LOCAL_TEXT_BUF tb = tb_new();
      if (!vpi_instance_timescale(argval.value.str, tb, &unit, &prec)) {
         bad_timescale_scope(argval.value.str);
         put_int_result(0);
         vpi_release_handle(call);
         return 0;
      }
   }
   else {
      unit = vpi_caller_timescale();
      prec = vpi_caller_precision();
   }

   put_int_result(time_log10(precision ? prec : unit));

   vpi_release_handle(call);
   return 0;
}

static PLI_INT32 noop_tf(PLI_BYTE8 *userdata)
{
   return 0;
}

static PLI_INT32 ferror_tf(PLI_BYTE8 *userdata)
{
   vpiHandle call = vpi_handle(vpiSysTfCall, NULL);
   assert(call != NULL);

   vpiHandle argv = vpi_iterate(vpiArgument, call);
   vpiHandle fd_arg = vpi_scan(argv);
   vpiHandle str_arg = vpi_scan(argv);

   PLI_INT32 err = 0;
   const char *msg = "";
   if (fd_arg != NULL) {
      FILE *f = sv_fd_stream(get_int_arg(fd_arg));
      if (f == NULL) {
         err = EBADF;
         msg = strerror(err);
      }
      else if (ferror(f)) {
         err = errno != 0 ? errno : EIO;
         msg = strerror(err);
      }
   }

   if (str_arg != NULL) {
      s_vpi_value value = {
         .format = vpiStringVal,
         .value = { .str = (PLI_BYTE8 *)msg },
      };
      vpi_put_value(str_arg, &value, NULL, vpiNoDelay);
      vpi_release_handle(str_arg);
   }

   put_int_result(err);

   if (fd_arg != NULL) vpi_release_handle(fd_arg);
   vpi_release_handle(argv);
   vpi_release_handle(call);
   return 0;
}

static PLI_INT32 fgetc_tf(PLI_BYTE8 *userdata)
{
   vpiHandle call = vpi_handle(vpiSysTfCall, NULL);
   assert(call != NULL);

   vpiHandle argv = vpi_iterate(vpiArgument, call);
   vpiHandle fd_arg = vpi_scan(argv);
   assert(fd_arg != NULL);

   FILE *f = get_file_stream(fd_arg, false);
   put_int_result(f != NULL ? fgetc(f) : -1);

   vpi_release_handle(fd_arg);
   vpi_release_handle(argv);
   vpi_release_handle(call);
   return 0;
}

static PLI_INT32 ungetc_tf(PLI_BYTE8 *userdata)
{
   vpiHandle call = vpi_handle(vpiSysTfCall, NULL);
   assert(call != NULL);

   vpiHandle argv = vpi_iterate(vpiArgument, call);
   vpiHandle c_arg = vpi_scan(argv);
   vpiHandle fd_arg = vpi_scan(argv);
   assert(c_arg != NULL);
   assert(fd_arg != NULL);

   const int c = get_int_arg(c_arg);
   FILE *f = get_file_stream(fd_arg, false);
   put_int_result(f == NULL || ungetc(c, f) == EOF ? -1 : 0);

   vpi_release_handle(c_arg);
   vpi_release_handle(fd_arg);
   vpi_release_handle(argv);
   vpi_release_handle(call);
   return 0;
}

static PLI_INT32 ftell_tf(PLI_BYTE8 *userdata)
{
   vpiHandle call = vpi_handle(vpiSysTfCall, NULL);
   assert(call != NULL);

   vpiHandle argv = vpi_iterate(vpiArgument, call);
   vpiHandle fd_arg = vpi_scan(argv);
   assert(fd_arg != NULL);

   FILE *f = get_file_stream(fd_arg, false);
   put_int_result(f != NULL ? ftell(f) : -1);

   vpi_release_handle(fd_arg);
   vpi_release_handle(argv);
   vpi_release_handle(call);
   return 0;
}

static PLI_INT32 fseek_tf(PLI_BYTE8 *userdata)
{
   vpiHandle call = vpi_handle(vpiSysTfCall, NULL);
   assert(call != NULL);

   vpiHandle argv = vpi_iterate(vpiArgument, call);
   vpiHandle fd_arg = vpi_scan(argv);
   vpiHandle off_arg = vpi_scan(argv);
   vpiHandle op_arg = vpi_scan(argv);
   assert(fd_arg != NULL);
   assert(off_arg != NULL);
   assert(op_arg != NULL);

   static const int whence[] = { SEEK_SET, SEEK_CUR, SEEK_END };
   const int op = get_int_arg(op_arg);

   FILE *f = get_file_stream(fd_arg, false);
   put_int_result(f != NULL && op >= 0 && op < 3
                  ? fseek(f, get_int_arg(off_arg), whence[op]) : -1);

   vpi_release_handle(fd_arg);
   vpi_release_handle(off_arg);
   vpi_release_handle(op_arg);
   vpi_release_handle(argv);
   vpi_release_handle(call);
   return 0;
}

static s_vpi_systf_data builtins[] = {
   {
      .type        = vpiSysFunc,
      .tfname      = "$asin",
      .sysfunctype = vpiRealFunc,
      .calltf      = asin_tf
   },
   {
      .type        = vpiSysFunc,
      .tfname      = "$acos",
      .sysfunctype = vpiRealFunc,
      .calltf      = acos_tf
   },
   {
      .type        = vpiSysFunc,
      .tfname      = "$atan",
      .sysfunctype = vpiRealFunc,
      .calltf      = atan_tf
   },
   {
      .type        = vpiSysFunc,
      .tfname      = "$sinh",
      .sysfunctype = vpiRealFunc,
      .calltf      = sinh_tf
   },
   {
      .type        = vpiSysFunc,
      .tfname      = "$cosh",
      .sysfunctype = vpiRealFunc,
      .calltf      = cosh_tf
   },
   {
      .type        = vpiSysFunc,
      .tfname      = "$tanh",
      .sysfunctype = vpiRealFunc,
      .calltf      = tanh_tf
   },
   {
      .type        = vpiSysFunc,
      .tfname      = "$asinh",
      .sysfunctype = vpiRealFunc,
      .calltf      = asinh_tf
   },
   {
      .type        = vpiSysFunc,
      .tfname      = "$acosh",
      .sysfunctype = vpiRealFunc,
      .calltf      = acosh_tf
   },
   {
      .type        = vpiSysFunc,
      .tfname      = "$atanh",
      .sysfunctype = vpiRealFunc,
      .calltf      = atanh_tf
   },
   {
      .type        = vpiSysFunc,
      .tfname      = "$atan2",
      .sysfunctype = vpiRealFunc,
      .calltf      = atan2_tf
   },
   {
      .type        = vpiSysFunc,
      .tfname      = "$hypot",
      .sysfunctype = vpiRealFunc,
      .calltf      = hypot_tf
   },
   {
      .type        = vpiSysFunc,
      .tfname      = "$realtobits",
      .sysfunctype = vpiTimeFunc,
      .calltf      = realtobits_tf
   },
   {
      .type        = vpiSysFunc,
      .tfname      = "$bitstoreal",
      .sysfunctype = vpiRealFunc,
      .calltf      = bitstoreal_tf
   },
   {
      .type        = vpiSysFunc,
      .tfname      = "$shortrealtobits",
      .sysfunctype = vpiIntFunc,
      .calltf      = shortrealtobits_tf
   },
   {
      .type        = vpiSysFunc,
      .tfname      = "$bitstoshortreal",
      .sysfunctype = vpiRealFunc,
      .calltf      = bitstoshortreal_tf
   },
   {
      .type        = vpiSysFunc,
      .tfname      = "$countbits",
      .sysfunctype = vpiIntFunc,
      .calltf      = countbits_tf
   },
   {
      .type        = vpiSysFunc,
      .tfname      = "$countones",
      .sysfunctype = vpiIntFunc,
      .calltf      = countones_tf
   },
   {
      .type        = vpiSysFunc,
      .tfname      = "$onehot",
      .sysfunctype = vpiIntFunc,
      .calltf      = onehot_tf
   },
   {
      .type        = vpiSysFunc,
      .tfname      = "$onehot0",
      .sysfunctype = vpiIntFunc,
      .calltf      = onehot0_tf
   },
   {
      .type        = vpiSysFunc,
      .tfname      = "$isunknown",
      .sysfunctype = vpiIntFunc,
      .calltf      = isunknown_tf
   },
   {
      .type        = vpiSysFunc,
      .tfname      = "$test$plusargs",
      .sysfunctype = vpiIntFunc,
      .calltf      = test_plusargs_tf
   },
   {
      .type        = vpiSysFunc,
      .tfname      = "$value$plusargs",
      .sysfunctype = vpiIntFunc,
      .calltf      = value_plusargs_tf
   },
   {
      .type        = vpiSysFunc,
      .tfname      = "$ferror",
      .sysfunctype = vpiIntFunc,
      .calltf      = ferror_tf
   },
   {
      .type        = vpiSysFunc,
      .tfname      = "$fgetc",
      .sysfunctype = vpiIntFunc,
      .calltf      = fgetc_tf
   },
   {
      .type        = vpiSysFunc,
      .tfname      = "$ungetc",
      .sysfunctype = vpiIntFunc,
      .calltf      = ungetc_tf
   },
   {
      .type        = vpiSysFunc,
      .tfname      = "$ftell",
      .sysfunctype = vpiIntFunc,
      .calltf      = ftell_tf
   },
   {
      .type        = vpiSysFunc,
      .tfname      = "$fseek",
      .sysfunctype = vpiIntFunc,
      .calltf      = fseek_tf
   },
   {
      .type   = vpiSysTask,
      .tfname = "$printtimescale",
      .calltf = printtimescale_tf
   },
   {
      .type        = vpiSysFunc,
      .tfname      = "$timeunit",
      .sysfunctype = vpiIntFunc,
      .calltf      = timeunit_tf
   },
   {
      .type        = vpiSysFunc,
      .tfname      = "$timeprecision",
      .sysfunctype = vpiIntFunc,
      .calltf      = timeunit_tf,
      .user_data   = (PLI_BYTE8 *)"1",
   },
   {
      .type   = vpiSysTask,
      .tfname = "$monitoron",
      .calltf = monitor_switch_tf,
      .user_data = (PLI_BYTE8 *)"1",
   },
   {
      .type   = vpiSysTask,
      .tfname = "$monitoroff",
      .calltf = monitor_switch_tf,
      .user_data = (PLI_BYTE8 *)"0",
   },
   {
      .type   = vpiSysTask,
      .tfname = "$monitorb",
      .calltf = noop_tf
   },
   {
      .type   = vpiSysTask,
      .tfname = "$monitoro",
      .calltf = noop_tf
   },
   {
      .type   = vpiSysTask,
      .tfname = "$monitorh",
      .calltf = noop_tf
   },
   {
      .type   = vpiSysTask,
      .tfname = "$asserton",
      .calltf = noop_tf
   },
   {
      .type   = vpiSysTask,
      .tfname = "$assertoff",
      .calltf = noop_tf
   },
   {
      .type   = vpiSysTask,
      .tfname = "$assertkill",
      .calltf = noop_tf
   },
   {
      .type   = vpiSysTask,
      .tfname = "$assertcontrol",
      .calltf = noop_tf
   },
   {
      .type   = vpiSysTask,
      .tfname = "$assertpasson",
      .calltf = noop_tf
   },
   {
      .type   = vpiSysTask,
      .tfname = "$assertpassoff",
      .calltf = noop_tf
   },
   {
      .type   = vpiSysTask,
      .tfname = "$assertfailon",
      .calltf = noop_tf
   },
   {
      .type   = vpiSysTask,
      .tfname = "$assertfailoff",
      .calltf = noop_tf
   },
   {
      .type   = vpiSysTask,
      .tfname = "$assertnonvacuouson",
      .calltf = noop_tf
   },
   {
      .type   = vpiSysTask,
      .tfname = "$assertvacuousoff",
      .calltf = noop_tf
   },
   {
      .type   = vpiSysTask,
      .tfname = "$dumpfile",
      .calltf = noop_tf
   },
   {
      .type   = vpiSysTask,
      .tfname = "$dumpvars",
      .calltf = noop_tf
   },
   {
      .type   = vpiSysTask,
      .tfname = "$dumplimit",
      .calltf = noop_tf
   },
   {
      .type   = vpiSysTask,
      .tfname = "$dumpoff",
      .calltf = noop_tf
   },
   {
      .type   = vpiSysTask,
      .tfname = "$dumpon",
      .calltf = noop_tf
   },
   {
      .type   = vpiSysTask,
      .tfname = "$dumpflush",
      .calltf = noop_tf
   },
   {
      .type   = vpiSysTask,
      .tfname = "$dumpall",
      .calltf = noop_tf
   },
   {
      .type   = vpiSysTask,
      .tfname = "$dumpports",
      .calltf = noop_tf
   },
   {
      .type   = vpiSysTask,
      .tfname = "$dumpportsoff",
      .calltf = noop_tf
   },
   {
      .type   = vpiSysTask,
      .tfname = "$dumpportson",
      .calltf = noop_tf
   },
   {
      .type   = vpiSysTask,
      .tfname = "$dumpportsall",
      .calltf = noop_tf
   },
   {
      .type   = vpiSysTask,
      .tfname = "$dumpportsflush",
      .calltf = noop_tf
   },
   {
      .type   = vpiSysTask,
      .tfname = "$dumpportslimit",
      .calltf = noop_tf
   },
   {
      .type        = vpiSysFunc,
      .tfname      = "$sformatf",
      .sysfunctype = vpiIntFunc,
      .calltf      = sformatf_tf,
   },
   {
      .type        = vpiSysFunc,
      .tfname      = "$__swriteb",
      .sysfunctype = vpiIntFunc,
      .calltf      = sformatf_tf,
      .user_data   = (PLI_BYTE8 *)"b",
   },
   {
      .type        = vpiSysFunc,
      .tfname      = "$__swriteo",
      .sysfunctype = vpiIntFunc,
      .calltf      = sformatf_tf,
      .user_data   = (PLI_BYTE8 *)"o",
   },
   {
      .type        = vpiSysFunc,
      .tfname      = "$__swriteh",
      .sysfunctype = vpiIntFunc,
      .calltf      = sformatf_tf,
      .user_data   = (PLI_BYTE8 *)"h",
   },
   {
      .type        = vpiSysFunc,
      .tfname      = "$rewind",
      .sysfunctype = vpiIntFunc,
      .calltf      = rewind_tf,
   },
   {
      .type        = vpiSysFunc,
      .tfname      = "$dist_uniform",
      .sysfunctype = vpiIntFunc,
      .calltf      = dist_uniform_tf,
   },
   {
      .type        = vpiSysFunc,
      .tfname      = "$realtime",
      .sysfunctype = vpiRealFunc,
      .calltf      = realtime_tf,
   },
   {
      .type   = vpiSysTask,
      .tfname = "$timeformat",
      .calltf = timeformat_tf,
   },
   {
      .type      = vpiSysTask,
      .tfname    = "$display",
      .calltf    = display_tf,
      .user_data = "d"
   },
   {
      .type      = vpiSysTask,
      .tfname    = "$displayb",
      .calltf    = display_tf,
      .user_data = "b"
   },
   {
      .type      = vpiSysTask,
      .tfname    = "$displayh",
      .calltf    = display_tf,
      .user_data = "h"
   },
   {
      .type      = vpiSysTask,
      .tfname    = "$displayo",
      .calltf    = display_tf,
      .user_data = "o"
   },
   {
      .type      = vpiSysTask,
      .tfname    = "$write",
      .calltf    = write_tf,
      .user_data = "d"
   },
   {
      .type      = vpiSysTask,
      .tfname    = "$writeb",
      .calltf    = write_tf,
      .user_data = "b"
   },
   {
      .type      = vpiSysTask,
      .tfname    = "$writeh",
      .calltf    = write_tf,
      .user_data = "h"
   },
   {
      .type      = vpiSysTask,
      .tfname    = "$writeo",
      .calltf    = write_tf,
      .user_data = "o"
   },
   {
      .type   = vpiSysTask,
      .tfname = "$finish",
      .calltf = finish_tf
   },
   {
      .type   = vpiSysTask,
      .tfname = "$stop",
      .calltf = stop_tf
   },
   {
      .type      = vpiSysTask,
      .tfname    = "$info",
      .calltf    = diag_tf,
      .user_data = (void *)DIAG_NOTE,
   },
   {
      .type      = vpiSysTask,
      .tfname    = "$warning",
      .calltf    = diag_tf,
      .user_data = (void *)DIAG_WARN,
   },
   {
      .type      = vpiSysTask,
      .tfname    = "$error",
      .calltf    = diag_tf,
      .user_data = (void *)DIAG_ERROR,
   },
   {
      .type      = vpiSysTask,
      .tfname    = "$fatal",
      .calltf    = fatal_tf,
   },
   {
      .type     = vpiSysTask,
      .tfname   = "$__monitor_start",
      .calltf   = monitor_start_tf,
      .user_data = (PLI_BYTE8 *)"m",
   },
   {
      .type     = vpiSysTask,
      .tfname   = "$__strobe_start",
      .calltf   = monitor_start_tf,
      .user_data = (PLI_BYTE8 *)"s",
   },
   {
      .type     = vpiSysTask,
      .tfname   = "$__defer",
      .calltf   = defer_tf,
   },
   {
      .type     = vpiSysTask,
      .tfname   = "$__defer_scope",
      .calltf   = defer_scope_tf,
   },
   {
      .type     = vpiSysTask,
      .tfname   = "$__monitor",
      .calltf   = monitor_print_tf,
      .user_data = (PLI_BYTE8 *)"m",
   },
   {
      .type     = vpiSysTask,
      .tfname   = "$__monitor_kick",
      .calltf   = monitor_print_tf,
      .user_data = (PLI_BYTE8 *)"M",
   },
   {
      .type     = vpiSysTask,
      .tfname   = "$__fmonitor_kick",
      .calltf   = monitor_print_tf,
      .user_data = (PLI_BYTE8 *)"F",
   },
   {
      .type     = vpiSysTask,
      .tfname   = "$__fmonitor_start",
      .calltf   = monitor_start_tf,
      .user_data = (PLI_BYTE8 *)"f",
   },
   {
      .type     = vpiSysTask,
      .tfname   = "$__fmonitor",
      .calltf   = monitor_print_tf,
      .user_data = (PLI_BYTE8 *)"f",
   },
   {
      .type     = vpiSysTask,
      .tfname   = "$__strobe",
      .calltf   = monitor_print_tf,
      .user_data = (PLI_BYTE8 *)"s",
   },
   {
      .type     = vpiSysTask,
      .tfname   = "$__fstrobe",
      .calltf   = monitor_print_tf,
      .user_data = (PLI_BYTE8 *)"t",
   },
   {
      .type   = vpiSysTask,
      .tfname = "$readmemh",
      .calltf = readmem_tf,
      .user_data = "h"
   },
   {
      .type   = vpiSysTask,
      .tfname = "$readmemb",
      .calltf = readmem_tf,
      .user_data = "b"
   },
   {
      .type   = vpiSysTask,
      .tfname = "$writememh",
      .calltf = writemem_tf,
      .user_data = "h"
   },
   {
      .type   = vpiSysTask,
      .tfname = "$writememb",
      .calltf = writemem_tf,
      .user_data = "b"
   },
   {
      .type      = vpiSysTask,
      .tfname    = "$fdisplay",
      .calltf    = fdisplay_tf,
      .user_data = "dn"
   },
   {
      .type      = vpiSysTask,
      .tfname    = "$fdisplayb",
      .calltf    = fdisplay_tf,
      .user_data = "bn"
   },
   {
      .type      = vpiSysTask,
      .tfname    = "$fdisplayh",
      .calltf    = fdisplay_tf,
      .user_data = "hn"
   },
   {
      .type      = vpiSysTask,
      .tfname    = "$fdisplayo",
      .calltf    = fdisplay_tf,
      .user_data = "on"
   },
   {
      .type      = vpiSysTask,
      .tfname    = "$fwrite",
      .calltf    = fdisplay_tf,
      .user_data = "d"
   },
   {
      .type      = vpiSysTask,
      .tfname    = "$fwriteb",
      .calltf    = fdisplay_tf,
      .user_data = "b"
   },
   {
      .type      = vpiSysTask,
      .tfname    = "$fwriteh",
      .calltf    = fdisplay_tf,
      .user_data = "h"
   },
   {
      .type      = vpiSysTask,
      .tfname    = "$fwriteo",
      .calltf    = fdisplay_tf,
      .user_data = "o"
   },
   {
      .type   = vpiSysTask,
      .tfname = "$fclose",
      .calltf = fclose_tf
   },
   {
      .type   = vpiSysTask,
      .tfname = "$fflush",
      .calltf = fflush_tf
   },
   {
      .type        = vpiSysFunc,
      .tfname      = "$fopen",
      .sysfunctype = vpiIntFunc,
      .calltf      = fopen_tf
   },
   {
      .type        = vpiSysFunc,
      .tfname      = "$feof",
      .sysfunctype = vpiIntFunc,
      .calltf      = feof_tf
   },
   {
      .type        = vpiSysFunc,
      .tfname      = "$system",
      .sysfunctype = vpiIntFunc,
      .calltf      = system_tf
   },
   {
      .type        = vpiSysFunc,
      .tfname      = "$fscanf",
      .sysfunctype = vpiIntFunc,
      .calltf      = fscanf_tf
   },
   {
      .type        = vpiSysFunc,
      .tfname      = "$sscanf",
      .sysfunctype = vpiIntFunc,
      .calltf      = sscanf_tf
   },
   {
      .type        = vpiSysFunc,
      .tfname      = "$fgets",
      .sysfunctype = vpiIntFunc,
      .calltf      = fgets_tf
   },
   {
      .type        = vpiSysFunc,
      .tfname      = "$time",
      .sysfunctype = vpiTimeFunc,
      .calltf      = time_tf
   },
   {
      .type        = vpiSysFunc,
      .tfname      = "$stime",
      .sysfunctype = vpiIntFunc,
      .calltf      = stime_tf
   },
   {
      .type        = vpiSysFunc,
      .tfname      = "$random",
      .sysfunctype = vpiIntFunc,
      .calltf      = random_tf
   },
   {
      .type        = vpiSysFunc,
      .tfname      = "$urandom",
      .sysfunctype = vpiIntFunc,
      .calltf      = urandom_tf
   },
   {
      .type        = vpiSysFunc,
      .tfname      = "$urandom_range",
      .sysfunctype = vpiIntFunc,
      .calltf      = urandom_range_tf
   },
   {
      .type        = vpiSysFunc,
      .tfname      = "$__cov_new",
      .sysfunctype = vpiIntFunc,
      .calltf      = cov_new_tf
   },
   {
      .type        = vpiSysFunc,
      .tfname      = "$__cov_key",
      .sysfunctype = vpiIntFunc,
      .calltf      = cov_key_tf
   },
   {
      .type        = vpiSysTask,
      .tfname      = "$__cov_set",
      .calltf      = cov_set_tf
   },
   {
      .type        = vpiSysFunc,
      .tfname      = "$get_coverage",
      .sysfunctype = vpiRealFunc,
      .calltf      = get_coverage_tf
   },
   {
      .type        = vpiSysTask,
      .tfname      = "$set_coverage_db_name",
      .calltf      = set_coverage_db_name_tf
   },
   {
      .type        = vpiSysTask,
      .tfname      = "$load_coverage_db",
      .calltf      = load_coverage_db_tf
   },
   {
      .type        = vpiSysFunc,
      .tfname      = "$clog2",
      .sysfunctype = vpiIntFunc,
      .calltf      = clog2_tf
   },
   {
      .type        = vpiSysFunc,
      .tfname      = "$rtoi",
      .sysfunctype = vpiIntFunc,
      .calltf      = rtoi_tf
   },
   {
      .type        = vpiSysFunc,
      .tfname      = "$itor",
      .sysfunctype = vpiRealFunc,
      .calltf      = itor_tf
   },
   {
      .type        = vpiSysFunc,
      .tfname      = "$pow",
      .sysfunctype = vpiRealFunc,
      .calltf      = pow_tf
   },
   {
      .type        = vpiSysFunc,
      .tfname      = "$sqrt",
      .sysfunctype = vpiRealFunc,
      .calltf      = sqrt_tf
   },
   {
      .type        = vpiSysFunc,
      .tfname      = "$sin",
      .sysfunctype = vpiRealFunc,
      .calltf      = sin_tf
   },
   {
      .type        = vpiSysFunc,
      .tfname      = "$cos",
      .sysfunctype = vpiRealFunc,
      .calltf      = cos_tf
   },
   {
      .type        = vpiSysFunc,
      .tfname      = "$tan",
      .sysfunctype = vpiRealFunc,
      .calltf      = tan_tf
   },
   {
      .type        = vpiSysFunc,
      .tfname      = "$ln",
      .sysfunctype = vpiRealFunc,
      .calltf      = ln_tf
   },
   {
      .type        = vpiSysFunc,
      .tfname      = "$log10",
      .sysfunctype = vpiRealFunc,
      .calltf      = log10_tf
   },
   {
      .type        = vpiSysFunc,
      .tfname      = "$exp",
      .sysfunctype = vpiRealFunc,
      .calltf      = exp_tf
   },
   {
      .type        = vpiSysFunc,
      .tfname      = "$ceil",
      .sysfunctype = vpiRealFunc,
      .calltf      = ceil_tf
   },
   {
      .type        = vpiSysFunc,
      .tfname      = "$floor",
      .sysfunctype = vpiRealFunc,
      .calltf      = floor_tf
   }
};

void vpi_register_builtins(void)
{
   for (int i = 0; i < ARRAY_LEN(builtins); i++) {
      vpiHandle handle = vpi_register_systf(&builtins[i]);
      if (handle == NULL)
         fatal("failed to register system task %s", builtins[i].tfname);
   }
}
