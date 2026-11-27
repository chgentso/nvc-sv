//
//  Copyright (C) 2011-2024  Nick Gasson
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
#include "fbuf.h"
#include "option.h"
#include "printf.h"
#include "thread.h"

#include <ctype.h>
#include <string.h>
#include <stdlib.h>
#include <stdarg.h>
#include <limits.h>
#include <sys/types.h>
#include <fcntl.h>
#include <unistd.h>

#define LOC_MAGIC 0xf00e

typedef struct _hint_rec hint_rec_t;

typedef struct {
   file_ref_t  ref;
   char       *name_str;
   char       *abs_str;
   const char *linebuf;
   bool        tried_open;
   const char **lines;     // Start of each line seen so far
   unsigned    nlines;
   unsigned    maxlines;
} loc_file_t;

typedef A(loc_file_t) file_list_t;

struct loc_wr_ctx {
   fbuf_t   *fbuf;
   unsigned  first_line;
   unsigned  first_column;
   unsigned  line_delta;
   unsigned  column_delta;
   bool      have_header;
   unsigned *local_map;
   size_t    map_size;
   unsigned  n_files;
};

struct loc_rd_ctx {
   fbuf_t      *fbuf;
   char       **file_map;
   file_ref_t  *ref_map;
   size_t       n_files;
   size_t       max_files;
   unsigned     first_line;
   unsigned     first_column;
   unsigned     line_delta;
   unsigned     column_delta;
   bool         have_header;
};

typedef enum {
   HINT_NOTE,
   HINT_HELP
} hint_kind_t;

typedef struct {
   loc_t        loc;
   char        *text;
   int          priority;
   hint_kind_t  kind;
} diag_hint_t;

typedef A(diag_hint_t) hint_list_t;

typedef struct {
   loc_t  loc;
   char  *text;
} diag_edit_t;

typedef A(diag_edit_t) edit_list_t;

struct _diag {
   text_buf_t   *msg;
   diag_level_t  level;
   hint_list_t   hints;
   hint_list_t   trace;
   edit_list_t   edits;
   char         *rule;
   bool          rule_shown;
   char         *fix_title;
   ostream_t    *ostream;
   bool          source;
   bool          suppress;
   bool          quiet;
   bool          prefix;
   bool          relaxable;
};

typedef struct _hint_rec {
   diag_hint_fn_t  fn;
   void           *context;
} hint_rec_t;

static unsigned    n_diags[DIAG_FATAL + 1];
static unsigned    error_limit = 0;
static bool        soft_limit = false;
static bool        scan_filter = false;
static unsigned    scan_visible = 0;
static file_list_t loc_files;
static nvc_lock_t  diag_lock   = 0;

static __thread diag_consumer_t  consumer_fn = NULL;
static __thread void            *consumer_ctx = NULL;

#define MAX_HINT_RECS 4
static __thread hint_rec_t hint_recs[MAX_HINT_RECS];

#define DIAG_THEME_CLASSIC 1
#define DIAG_THEME_RUST    2

#define DIAG_THEME DIAG_THEME_CLASSIC

////////////////////////////////////////////////////////////////////////////////
// File management

static loc_file_t *loc_file_data(const loc_t *loc)
{
   return AREF(loc_files, loc->file_ref);
}

file_ref_t loc_file_ref(const char *name, const char *linebuf)
{
   if (name == NULL)
      return FILE_INVALID;

   for (unsigned i = 0; i < loc_files.count; i++) {
      if (strcmp(loc_files.items[i].name_str, name) == 0)
         return loc_files.items[i].ref;
   }

   LOCAL_TEXT_BUF tb = tb_new();

   const char *relative = opt_get_str(OPT_RELATIVE_PATH);
   if (relative != NULL)
      get_relative_path(tb, relative, name);
   else {
      // Strip any consecutive '/' characters
      for (const char *s = name; *s != '\0'; s++) {
         if (*s != '/' || *(s + 1) != '/')
            tb_append(tb, *s);
      }
   }

   // TODO: use a hash table
   for (unsigned i = 0; i < loc_files.count; i++) {
      if (strcmp(loc_files.items[i].name_str, tb_get(tb)) == 0)
         return loc_files.items[i].ref;
   }

   loc_file_t new = {
      .linebuf  = linebuf,
      .name_str = tb_claim(tb),
      .ref      = loc_files.count
   };

   APUSH(loc_files, new);

   return new.ref;
}

const char *loc_file_str(const loc_t *loc)
{
   if (loc->file_ref != FILE_INVALID)
      return loc_file_data(loc)->name_str;
   else
      return NULL;
}

static const char *loc_abs_path(const loc_t *loc)
{
   if (loc->file_ref == FILE_INVALID)
      return NULL;

   loc_file_t *data = loc_file_data(loc);
   if (data->abs_str != NULL)
      return data->abs_str;

   if ((data->abs_str = realpath(data->name_str, NULL)) == NULL) {
      // Avoid repeated calls to realpath in failing case
      data->abs_str = data->name_str;
   }

   return data->abs_str;
}

////////////////////////////////////////////////////////////////////////////////
// Source location tracking

bool loc_invalid_p(const loc_t *loc)
{
   return loc == NULL
      || loc->first_line == LINE_INVALID
      || loc->file_ref == FILE_INVALID;
}

loc_t get_loc(unsigned first_line, unsigned first_column, unsigned last_line,
              unsigned last_column, file_ref_t file_ref)
{
   if (first_line > last_line)
      return LOC_INVALID;
   else if (first_line == last_line && first_column > last_column)
      return LOC_INVALID;

   loc_t result = {
      .first_line   = MIN(first_line, LINE_INVALID),
      .first_column = MIN(first_column, COLUMN_INVALID),
      .line_delta   = MIN(last_line - first_line, DELTA_INVALID),
      .column_delta = MIN(last_column - first_column, DELTA_INVALID),
      .file_ref     = file_ref
   };
   return result;
}

const char *loc_get_source(const loc_t *loc)
{
   if (loc->file_ref == FILE_INVALID
       || loc->first_line == LINE_INVALID
       || loc->first_column == COLUMN_INVALID)
      return NULL;

   loc_file_t *file = loc_file_data(loc);

   if (file->linebuf == NULL && !file->tried_open) {
      file->tried_open = true;

      int fd = open(file->name_str, O_RDONLY);
      if (fd < 0)
         return NULL;

      file_info_t info;
      if (!get_handle_info(fd, &info))
         goto close_file;

      if (info.type != FILE_REGULAR)
         goto close_file;

      if (info.size > 0)
         file->linebuf = map_file(fd, info.size);

   close_file:
      close(fd);
   }

   if (file->linebuf == NULL)
      return NULL;

   // Index the line starts lazily so that lookups in any order are cheap
   if (file->nlines == 0) {
      file->maxlines = 256;
      file->lines = xmalloc_array(file->maxlines, sizeof(const char *));
      file->lines[file->nlines++] = file->linebuf;
   }

   const unsigned want = loc->first_line > 1 ? loc->first_line : 1;
   while (file->nlines < want) {
      const char *nl = strchr(file->lines[file->nlines - 1], '\n');
      if (nl == NULL)
         return NULL;

      if (file->nlines == file->maxlines) {
         file->maxlines *= 2;
         file->lines = xrealloc_array(file->lines, file->maxlines,
                                      sizeof(const char *));
      }

      file->lines[file->nlines++] = nl + 1;
   }

   return file->lines[want - 1];
}

bool loc_eq(const loc_t *a, const loc_t *b)
{
   return a->first_line == b->first_line
      && a->first_column == b->first_column
      && a->line_delta == b->line_delta
      && a->column_delta == b->column_delta
      && a->file_ref == b->file_ref;
}

loc_wr_ctx_t *loc_write_begin(fbuf_t *f)
{
   loc_wr_ctx_t *ctx = xcalloc(sizeof(loc_wr_ctx_t));
   ctx->fbuf = f;

   return ctx;
}

void loc_write_end(loc_wr_ctx_t *ctx)
{
   free(ctx->local_map);
   free(ctx);
}

void loc_write(const loc_t *loc, loc_wr_ctx_t *ctx)
{
   if (!ctx->have_header) {
      write_u16(LOC_MAGIC, ctx->fbuf);
      ctx->have_header = true;
   }

   // A file is numbered when first used so the contents of a unit do not
   // depend on the other files that have been read
   if (loc->file_ref == FILE_INVALID)
      fbuf_put_uint(ctx->fbuf, 0);
   else {
      if (loc->file_ref >= ctx->map_size) {
         const size_t new_size = MAX(loc_files.count, loc->file_ref + 1);
         ctx->local_map = xrealloc_array(ctx->local_map, new_size,
                                         sizeof(unsigned));
         memset(ctx->local_map + ctx->map_size, '\0',
                (new_size - ctx->map_size) * sizeof(unsigned));
         ctx->map_size = new_size;
      }

      unsigned *local = &(ctx->local_map[loc->file_ref]);
      if (*local == 0) {
         *local = ++(ctx->n_files);
         fbuf_put_uint(ctx->fbuf, *local);

         const char *name = loc_files.items[loc->file_ref].name_str;
         const size_t len = strlen(name) + 1;
         fbuf_put_uint(ctx->fbuf, len);
         write_raw(name, len, ctx->fbuf);
      }
      else
         fbuf_put_uint(ctx->fbuf, *local);
   }

   fbuf_put_int(ctx->fbuf, loc->first_line - ctx->first_line);
   fbuf_put_int(ctx->fbuf, loc->line_delta - ctx->line_delta);
   fbuf_put_int(ctx->fbuf, loc->first_column - ctx->first_column);
   fbuf_put_int(ctx->fbuf, loc->column_delta - ctx->column_delta);

   ctx->first_line   = loc->first_line;
   ctx->first_column = loc->first_column;
   ctx->line_delta   = loc->line_delta;
   ctx->column_delta = loc->column_delta;
}

loc_rd_ctx_t *loc_read_begin(fbuf_t *f)
{
   loc_rd_ctx_t *ctx = xcalloc(sizeof(loc_rd_ctx_t));
   ctx->fbuf = f;

   return ctx;
}

void loc_read_end(loc_rd_ctx_t *ctx)
{
   for (size_t i = 0; i < ctx->n_files; i++)
      free(ctx->file_map[i]);

   free(ctx->file_map);
   free(ctx->ref_map);
   free(ctx);
}

void loc_read(loc_t *loc, loc_rd_ctx_t *ctx)
{
   if (!ctx->have_header) {
      uint16_t magic = read_u16(ctx->fbuf);
      if (magic != LOC_MAGIC)
         fatal("corrupt location header in %s", fbuf_file_name(ctx->fbuf));

      ctx->have_header = true;
   }

   const size_t local = fbuf_get_uint(ctx->fbuf);
   file_ref_t new_ref = FILE_INVALID;
   if (local != 0) {
      if (unlikely(local > ctx->n_files + 1))
         fatal("corrupt location file reference %zx", local);

      if (local == ctx->n_files + 1) {
         if (ctx->n_files == ctx->max_files) {
            ctx->max_files = MAX(ctx->max_files * 2, 16);
            ctx->file_map = xrealloc_array(ctx->file_map, ctx->max_files,
                                           sizeof(char *));
            ctx->ref_map = xrealloc_array(ctx->ref_map, ctx->max_files,
                                          sizeof(file_ref_t));
         }

         const size_t len = fbuf_get_uint(ctx->fbuf);
         char *buf = xmalloc(len + 1);
         read_raw(buf, len, ctx->fbuf);
         buf[len] = '\0';

         ctx->file_map[ctx->n_files] = buf;
         ctx->ref_map[ctx->n_files]  = FILE_INVALID;
         ctx->n_files++;
      }

      const size_t index = local - 1;
      if (ctx->ref_map[index] == FILE_INVALID) {
         for (unsigned i = 0; i < loc_files.count; i++) {
            if (strcmp(loc_files.items[i].name_str,
                       ctx->file_map[index]) == 0)
               ctx->ref_map[index] = loc_files.items[i].ref;
         }
      }

      if (ctx->ref_map[index] == FILE_INVALID) {
         loc_file_t new = {
            .linebuf  = NULL,
            .name_str = ctx->file_map[index],
            .ref      = loc_files.count
         };

         APUSH(loc_files, new);

         ctx->ref_map[index]  = new.ref;
         ctx->file_map[index] = NULL;   // Owned by loc_file_t now
      }

      new_ref = ctx->ref_map[index];
   }

   ctx->first_line   += fbuf_get_int(ctx->fbuf);
   ctx->line_delta   += fbuf_get_int(ctx->fbuf);
   ctx->first_column += fbuf_get_int(ctx->fbuf);
   ctx->column_delta += fbuf_get_int(ctx->fbuf);

   loc->first_line   = MIN(ctx->first_line, LINE_INVALID);
   loc->first_column = MIN(ctx->first_column, COLUMN_INVALID);
   loc->line_delta   = MIN(ctx->line_delta, DELTA_INVALID);
   loc->column_delta = MIN(ctx->column_delta, DELTA_INVALID);
   loc->file_ref     = new_ref;
}

////////////////////////////////////////////////////////////////////////////////
// Fancy diagnostics

#if DIAG_THEME == DIAG_THEME_CLASSIC
#define DEBUG_PREFIX   "** Debug: "
#define NOTE_PREFIX    "** Note: "
#define WARNING_PREFIX "$yellow$** Warning:$$ "
#define ERROR_PREFIX   "$red$** Error:$$ "
#define FAILURE_PREFIX "$red$** Failure:$$ "
#define FATAL_PREFIX   "$red$** Fatal:$$ "
#define GUTTER_STYLE   "$blue$"
#define HINT_STYLE     ""
#define CARET_STYLE    ""
#define TRACE_STYLE    ""
#define TRAILING_BLANK 0
#elif DIAG_THEME == DIAG_THEME_RUST
#define DEBUG_PREFIX   "$!green$Debug:$$ "
#define NOTE_PREFIX    "$bold$Note:$$ "
#define WARNING_PREFIX "$!yellow$Warning:$$ "
#define ERROR_PREFIX   "$!red$Error:$$ "
#define FAILURE_PREFIX "$!red$Failure:$$ "
#define FATAL_PREFIX   "$!red$Fatal:$$ "
#define GUTTER_STYLE   "$!blue$"
#define HINT_STYLE     "$bold$"
#define CARET_STYLE    "$bold$"
#define TRACE_STYLE    "$bold$"
#define TRAILING_BLANK 1
#else
#error "invalid DIAG_THEME"
#endif

static const struct {
   const char *title;
   const char *section[STD_19 + 1];
} lrm_sections[] = {
   { "Names", { [STD_08] = "8.1", [STD_93] = "6.1", [STD_19] = "8.1" } },
   { "Signal parameters", { [STD_08] = "4.2.2.3", [STD_93] = "2.1.1.2" } },
   { "Constant and variable parameters", { [STD_93] = "2.1.1.1",
        [STD_08] = "4.2.2.2" } },
   { "Object aliases", { [STD_93] = "4.3.3.1", [STD_08] = "6.6.2" } },
   { "Case statement", { [STD_93] = "8.8", [STD_08] = "10.9" } },
   { "Elaboration of a declarative part", { [STD_93] = "12.3",
        [STD_08] = "14.4" } },
   { "Process statement", { [STD_93] = "9.2", [STD_08] = "11.3" } },
   { "Entity statement part", { [STD_93] = "1.1.3", [STD_08] = "3.2.4" } },
   { "Qualified expressions", { [STD_08] = "9.3.5" } },
   { "Interface package declarations", { [STD_08] = "6.5.5" } },
   { "Selected names", { [STD_08] = "8.3", [STD_19] = "8.3", [STD_93] = "6.3" } },
   { "Simple variable assignments", { [STD_08] = "10.6.2" } },
   { "Configuration declarations", { [STD_08] = "3.4", [STD_93] = "1.4" } },
   { "Formal parameter lists", { [STD_19] = "4.2.2.1", [STD_08] = "4.2.2.1",
        [STD_93] = "2.1.1" } },
   { "Protected type declarations", { [STD_08] = "5.6.2",
        [STD_02] = "3.5.1" } },
   { "Type conversions", { [STD_93] = "7.3.5", [STD_08] = "9.3.6" } },
   { "External names", { [STD_08] = "8.7", [STD_19] = "8.7" } },
   { "Port clauses", { [STD_08] = "6.5.6.3", [STD_19] = "6.5.6.3" } },
   { "Subprogram instantiation declarations", { [STD_08] = "4.4" } },
   { "Incomplete type declarations", { [STD_08] = "5.4.2", [STD_93] = "3.3.1",
        [STD_19] = "5.4.2" } },
   { "String literals", { [STD_93] = "13.6", [STD_08] = "15.7" } },
   { "Simple signal assignments", { [STD_93] = "8.4", [STD_08] = "10.5.2" } },
   { "Basic identifiers", { [STD_93] = "13.3.1", [STD_08] = "15.4.2" } },
   { "Concurrent signal assignment statements", { [STD_93] = "9.5",
        [STD_08] = "11.6" } },
   { "Elaboration of a declaration", { [STD_08] = "14.4.2", [STD_19] = "14.4.2",
        [STD_93] = "12.3.1" } },
   { "Package bodies", { [STD_08] = "4.8", [STD_93] = "2.6" } },
   { "Interface type declarations", { [STD_08] = "6.5.3",
        [STD_19] = "6.5.3" } },
   { "Conformance rules", { [STD_08] = "4.10", [STD_93] = "2.7" } },
   { "Executing a simple assignment statement", { [STD_08] = "10.5.2.2",
        [STD_93] = "8.4.1" } },
   { "Subprogram bodies", { [STD_08] = "4.3", [STD_93] = "2.2" } },
   { "Index contraints and discrete ranges", { [STD_93] = "3.2.1.1",
        [STD_08] = "5.3.2.2", [STD_19] = "5.3.2.2" } },
   { "Aggregates", { [STD_08] = "9.3.3", [STD_93] = "7.3.2",
      [STD_19] = "9.3.3" } },
   { "Interface object declarations", { [STD_08] = "6.5.2", [STD_19] = "6.5.2",
        [STD_93] = "4.3.2" } },
   { "Association lists", { [STD_08] = "6.5.7", [STD_93] = "4.3.2.2" } },
   { "Protect tool directives", { [STD_08] = "24.1" } },
   { "Resolution functions", { [STD_93] = "2.4", [STD_08] = "4.6" } },
};

static void diag_set_ostream(diag_t *d)
{
   if (d->level >= opt_get_int(OPT_STDERR_LEVEL))
      d->ostream = nvc_stderr();
   else
      d->ostream = nvc_stdout();
}

diag_t *diag_new(diag_level_t level, const loc_t *loc)
{
   diag_t *d = xcalloc(sizeof(diag_t));
   d->msg      = tb_new();
   d->level    = level;
   d->source   = loc != NULL && !loc_invalid_p(loc);
   d->suppress = false;
   d->quiet    = scan_filter && (level == DIAG_WARN || level == DIAG_ERROR);
   d->prefix   = true;

   diag_set_ostream(d);

   if (!loc_invalid_p(loc)) {
      diag_hint_t hint = {
         .loc   = *loc
      };
      APUSH(d->hints, hint);
   }

   // Callback could create new diagnostics
   static __thread bool entered = false;
   if (!entered) {
      entered = true;
      for (int i = 0; i < MAX_HINT_RECS; i++) {
         if (hint_recs[i].fn != NULL)
            (*hint_recs[i].fn)(d, hint_recs[i].context);
      }
      entered = false;
   }

   return d;
}

void diag_rule(diag_t *d, const char *rule)
{
   free(d->rule);
   d->rule = rule != NULL ? xstrdup(rule) : NULL;
}

const char *diag_get_rule(diag_t *d)
{
   return d->rule;
}

void diag_fix(diag_t *d, const loc_t *loc, const char *replacement,
              const char *title)
{
   // Replace the text at LOC with REPLACEMENT as one edit of the single
   // fix offered for the diagnostic: the title is that of the first call
   // and edits must not overlap

   if (loc_invalid_p(loc))
      return;

   diag_edit_t e = { .loc = *loc, .text = xstrdup(replacement ?: "") };
   APUSH(d->edits, e);

   if (d->fix_title == NULL && title != NULL)
      d->fix_title = xstrdup(title);
}

int diag_edits(diag_t *d)
{
   return d->edits.count;
}

const char *diag_get_fix_title(diag_t *d)
{
   return d->fix_title;
}

void diag_clear(diag_t *d)
{
   d->prefix = false;
   ACLEAR(d->hints);
   ACLEAR(d->trace);
   tb_rewind(d->msg);
}

diag_level_t diag_level(diag_t *d, const diag_level_t *new)
{
   diag_level_t old = d->level;
   if (new != NULL)
      d->level = *new;
   return old;
}

static ostream_t diag_ostream_for_text_buf(diag_t *d, text_buf_t *tb)
{
   ostream_t os = {
      .callback = tb_ostream_write,
      .context  = tb,
      .charset  = CHARSET_ISO88591,
   };

   if (consumer_fn == NULL)
      os.flags |= d->ostream->flags;

   return os;
}

void diag_vprintf(diag_t *d, const char *fmt, va_list ap)
{
   ostream_t os = diag_ostream_for_text_buf(d, d->msg);
   nvc_vfprintf(&os, fmt, ap);
}

void diag_printf(diag_t *d, const char *fmt, ...)
{
   va_list ap;
   va_start(ap, fmt);
   diag_vprintf(d, fmt, ap);
   va_end(ap);
}

void diag_write(diag_t *d, const char *str, size_t len)
{
   tb_catn(d->msg, str, len);
}

text_buf_t *diag_text_buf(diag_t *d)
{
   return d->msg;
}

void diag_vhint(diag_t *d, const loc_t *loc, const char *fmt, va_list ap)
{
   LOCAL_TEXT_BUF tb = tb_new();

   ostream_t os = diag_ostream_for_text_buf(d, tb);
   nvc_vfprintf(&os, fmt, ap);

   if (!loc_invalid_p(loc)) {
      for (int i = 0; i < d->hints.count; i++) {
         diag_hint_t *hint = &(d->hints.items[i]);
         if (loc_eq(loc, &(hint->loc))) {
            free(hint->text);
            hint->text = tb_claim(tb);
            return;
         }
      }

      d->source = true;
   }

   diag_hint_t h = {
      .loc      = loc ? *loc : LOC_INVALID,
      .text     = tb_claim(tb),
      .priority = -(d->hints.count),
      .kind     = HINT_NOTE,
   };
   APUSH(d->hints, h);
}

void diag_hint(diag_t *d, const loc_t *loc, const char *fmt, ...)
{
   va_list ap;
   va_start(ap, fmt);

   diag_vhint(d, loc, fmt, ap);

   va_end(ap);
}

void diag_trace(diag_t *d, const loc_t *loc, const char *fmt, ...)
{
   va_list ap;
   va_start(ap, fmt);

   LOCAL_TEXT_BUF tb = tb_new();

   ostream_t os = diag_ostream_for_text_buf(d, tb);
   nvc_vfprintf(&os, fmt, ap);

   va_end(ap);

   diag_hint_t h = {
      .loc      = loc ? *loc : LOC_INVALID,
      .text     = tb_claim(tb),
      .priority = d->hints.count,
   };
   APUSH(d->trace, h);

   if (loc != NULL && d->hints.count == 0) {
      diag_hint_t hint = {
         .loc = *loc
      };
      APUSH(d->hints, hint);
   }
}

void diag_lrm(diag_t *d, vhdl_standard_t std, const char *section)
{
   const char *title = NULL;
   for (int i = 0; i < ARRAY_LEN(lrm_sections); i++) {
      const char *s = lrm_sections[i].section[std];
      if (s != NULL && strcmp(s, section) == 0) {
         // Prefer the section number from the current standard
         const char *ss = lrm_sections[i].section[standard()];
         if (ss != NULL) {
            std = standard();
            section = ss;
         }

         title = lrm_sections[i].title;
         break;
      }
   }

   LOCAL_TEXT_BUF tb = tb_new();
   tb_printf(tb, "IEEE Std 1076-%s section %s",
             standard_text(std), section);

   if (title != NULL)
      tb_printf(tb, " \"%s\"", title);

   diag_hint_t h = {
      .loc      = LOC_INVALID,
      .text     = tb_claim(tb),
      .priority = d->hints.count,
      .kind     = HINT_HELP
   };
   APUSH(d->hints, h);
}

static void diag_putc_utf8(unsigned char ch, ostream_t *os)
{
   if (ch >= 128 && utf8_terminal()) {
      // Convert ISO-8859-1 internal encoding to UTF-8
      ostream_putc(os, 0xc2 + (ch > 0xbf));
      ostream_putc(os, (ch & 0x3f) + 0x80);
   }
   else if ((ch < 0x20 || ch == 0x7f)
            && ch != '\r' && ch != '\n' && ch != '\t' && ch != '\e') {
      if (utf8_terminal()) {
         // On unicode terminals emit the corresponding control picture
         // code point otherwise silently drop it
         ostream_putc(os, 0xe2);
         ostream_putc(os, 0x90);
         if (ch == 0x7f)  // DEL
            ostream_putc(os, 0xa1);
         else
            ostream_putc(os, 0x80 + ch);
      }
   }
   else
      ostream_putc(os, ch);
}

static void diag_print_utf8(const char *str, size_t len, ostream_t *os)
{
   const unsigned char *ustr = (const unsigned char *)str;

   bool have_non_utf8 = false;
   for (const unsigned char *p = ustr; p < ustr + len; p++) {
      if (have_non_utf8)
         diag_putc_utf8(*p, os);
      else if (*p == '\033')
         continue;  // Do not replace ANSI escapes
      else if (*p >= 128 || *p < 0x20 || *p == 0x7f) {
         ostream_write(os, str, p - ustr);
         diag_putc_utf8(*p, os);
         have_non_utf8 = true;
      }
   }

   if (!have_non_utf8)
      ostream_write(os, str, len);
}

static void diag_wrap_lines(const char *str, size_t len, int left,
                            ostream_t *os)
{
   const int right = terminal_width();
   if (right == 0 || left + len < right) {
      diag_print_utf8(str, len, os);
      return;
   }

   const char *p = str, *begin = str;
   for (int col = left, escape = 0; *p != '\0'; p++) {
      if (*p == '\033')
         escape = 1;
      else if (escape) {
         if (*p == 'm')
            escape = 0;
      }
      else if (col + 1 >= right && p - begin + 1 < right - left) {
         nvc_fprintf(os, "\n%*s", left, "");
         col = left + p - begin;
      }
      else if (isspace_iso88591(*p)) {
         diag_print_utf8(begin, p - begin + 1, os);
         if (*p == '\n') {
            nvc_fprintf(os, "%*s", left, "");
            col = left;
         }
         else
            col++;
         begin = p + 1;
      }
      else
         ++col;
   }

   if (begin < p)
      diag_print_utf8(begin, p - begin, os);
}

static int diag_compar(const void *_a, const void *_b)
{
   const diag_hint_t *a = _a, *b = _b;

   assert(a->loc.file_ref == b->loc.file_ref);
   return a->loc.first_line - b->loc.first_line;
}

static void diag_emit_loc(const loc_t *loc, ostream_t *os)
{
   const char *file = loc_file_str(loc);
   const char *abspath = loc_abs_path(loc);

   if (abspath != file)
      nvc_fprintf(os, "$link:file://%s#%u\07%s:%u$",
                  abspath, loc->first_line, file, loc->first_line);
   else
      nvc_fprintf(os, "%s:%u", file, loc->first_line);
}

static void diag_emit_hints(diag_t *d, ostream_t *os)
{
   int fwidth = 0;
   const char *linebuf = NULL;
   bool need_gap = false;

   loc_t loc0 = d->hints.items[0].loc;
   if (loc0.file_ref == FILE_INVALID)
      goto other_files;

   int same_file = 0, line_max = 0, line_min = INT_MAX;
   for (int i = 0; i < d->hints.count; i++) {
      if (d->hints.items[i].loc.file_ref == loc0.file_ref) {
         same_file++;
         const int first_line = d->hints.items[i].loc.first_line;
         line_max = MAX(first_line, line_max);
         line_min = MIN(first_line, line_min);
      }
   }

   // Move all diagnostics from the first file to the front of the list
   for (int i = 0; i < d->hints.count; i++) {
      if (d->hints.items[i].loc.file_ref != loc0.file_ref) {
         for (int j = i + 1; j < d->hints.count; j++) {
            if (d->hints.items[j].loc.file_ref == loc0.file_ref) {
               diag_hint_t tmp = d->hints.items[j];
               d->hints.items[j] = d->hints.items[i];
               d->hints.items[i] = tmp;
               break;
            }
         }
      }
   }

   qsort(d->hints.items, same_file, sizeof(diag_hint_t), diag_compar);

   if (d->source)
      linebuf = loc_get_source(&(d->hints.items[0].loc));

   if (linebuf == NULL)
      fwidth = 3;
   else {
      for (int n = line_max; n > 0; n /= 10, fwidth++)
         ;
   }

   if (!d->source)
      goto other_files;

   nvc_fprintf(os, "%*s$blue$  > $$", fwidth, "");
   diag_emit_loc(&loc0, os);

   if (linebuf == NULL) {
      ostream_puts(os, "\n");
      d->source = false;   // Cannot get original source code
      goto other_files;
   }

   nvc_fprintf(os, "\n%*s " GUTTER_STYLE " |$$\n", fwidth, "");
   need_gap = true;

   const char *p = linebuf;
   for (int i = line_min, h = 0; h < d->hints.count; i++) {
      diag_hint_t *hint = &(d->hints.items[h]);
      while (hint->loc.file_ref != loc0.file_ref) {
         if (h + 1 == d->hints.count)
            goto other_files;
         else
            hint = &(d->hints.items[++h]);
      }

      if (hint->loc.first_line > i + 2) {
         // Skip some lines
         nvc_fprintf(os, " " GUTTER_STYLE "...$$\n");
         for (; i < hint->loc.first_line; i++) {
            if ((p = strchr(p, '\n')) == NULL)
               return;
            p++;   // Skip newline
         }
      }

      nvc_fprintf(os, " " GUTTER_STYLE "%*.u |$$ $cyan$", fwidth, i);

      int first_col = hint->loc.first_column;
      for (int col = 0, n = 0; *p != '\0' && *p != '\n'; p++, n++) {
         if (n == hint->loc.first_column)
            first_col = col;
         if (*p == '\r')
            continue;
         else if (*p == '\t') {
            do {
               col += ostream_putc(os, ' ');
            } while (col % 8 != 0);
         }
         else if (isprint_iso88591(*p)) {
            diag_putc_utf8(*p, os);
            col++;
         }
      }

      nvc_fprintf(os, "$$\n");
      p++;   // Skip newline

      if (hint->loc.first_line == i) {
         nvc_fprintf(os, "%*s " GUTTER_STYLE " |$$ ", fwidth, "");

         nvc_fprintf(os, CARET_STYLE "$green$");
         nvc_fprintf(os, "%*s", first_col, "");

         int ncarets = 1;
         if (hint->loc.line_delta == 0
             && hint->loc.column_delta != DELTA_INVALID)
            ncarets = hint->loc.column_delta + 1;

         const int hintcol = fwidth + hint->loc.first_column + ncarets + 4;

         while (ncarets--) ostream_putc(os, '^');

         const char *text = hint->text;
         if (text == NULL && same_file > 1 && hint->priority == 0
             && d->level >= DIAG_ERROR)
            text = "error occurred here";

         if (text != NULL) {
            if (hintcol + strlen(text) >= MAX(terminal_width(), 80))
               nvc_fprintf(os, "$$\n%*s " GUTTER_STYLE " |$$%*s", fwidth, "",
                           hint->loc.first_column, "");

            nvc_fprintf(os, "$$$green$ %s$$\n", text);
         }
         else
            nvc_fprintf(os, "$$\n");

         // Only support one hint per line
         for (; h < d->hints.count
                 && d->hints.items[h].loc.first_line == i; h++)
            ;
      }
   }

 other_files:
   for (int i = 0; i < d->hints.count; i++) {
      diag_hint_t *hint = &(d->hints.items[i]);
      if (hint->loc.file_ref == loc0.file_ref && d->source)
         continue;   // Printed above
      else if (hint->text == NULL)
         continue;

      if (need_gap) {
         nvc_fprintf(os, "%*s " GUTTER_STYLE " |$$\n", fwidth, "");
         need_gap = false;
      }

      int col = nvc_fprintf(os, "%*s", fwidth, "");

      if (linebuf != NULL)
         col += nvc_fprintf(os, " " GUTTER_STYLE " = $$");

      if (col == 0)
         col += nvc_fprintf(os, NOTE_PREFIX);
      else
         col += nvc_fprintf(os, HINT_STYLE "%s:$$ ",
                            hint->kind == HINT_HELP ? "Help" : "Note");

      diag_wrap_lines(hint->text, strlen(hint->text), col, os);
      ostream_putc(os, '\n');

      if (!loc_invalid_p(&(hint->loc))) {
         nvc_fprintf(os, "%*s      $blue$>$$ ", fwidth, "");
         diag_emit_loc(&(hint->loc), os);
         ostream_putc(os, '\n');
      }
   }
}

static void diag_emit_trace(diag_t *d, ostream_t *os)
{
   // Do not show a stack trace if it just repeats the initial location
   if (d->trace.count == 1 && d->hints.count > 0 && d->source) {
      const loc_t hloc0 = d->hints.items[0].loc;
      const loc_t tloc0 = d->trace.items[0].loc;

      if (tloc0.file_ref == hloc0.file_ref
          || tloc0.first_line == hloc0.first_line)
         return;
   }

#if TRAILING_BLANK
   fputc('\n', f);
#endif

   const int fwidth UNUSED = 6;

   for (int i = 0; i < d->trace.count; i++) {
      diag_hint_t *hint = &(d->trace.items[i]);
      nvc_fprintf(os, "   " TRACE_STYLE "%s", hint->text);

      if (!loc_invalid_p(&(hint->loc))) {
         ostream_puts(os, " at ");
         diag_emit_loc(&(hint->loc), os);
      }

      ostream_putc(os, '\n');
   }
}

static void diag_format_compact(diag_t *d, ostream_t *os)
{
   if (d->hints.count > 0) {
      loc_t *loc = &(d->hints.items[0].loc);
      if (!loc_invalid_p(loc)) {
         loc_file_t *file_data = loc_file_data(loc);
         nvc_fprintf(os, "%s:%d:%d: ", file_data->name_str, loc->first_line,
                     loc->first_column + 1);
      }
   }

   switch (d->level) {
   case DIAG_DEBUG:   nvc_fprintf(os, "debug: "); break;
   case DIAG_NOTE:    nvc_fprintf(os, "note: "); break;
   case DIAG_WARN:    nvc_fprintf(os, "warning: "); break;
   case DIAG_ERROR:   nvc_fprintf(os, "error: "); break;
   case DIAG_FAILURE: nvc_fprintf(os, "failure: "); break;
   case DIAG_FATAL:   nvc_fprintf(os, "fatal: "); break;
   }

   diag_print_utf8(tb_get(d->msg), tb_len(d->msg), os);
   ostream_putc(os, '\n');
}

static void json_string(text_buf_t *tb, const char *str, size_t len,
                        bool latin1)
{
   tb_append(tb, '"');

   for (const unsigned char *p = (const unsigned char *)str;
        p < (const unsigned char *)str + len; p++) {
      switch (*p) {
      case '"':  tb_cat(tb, "\\\""); break;
      case '\\': tb_cat(tb, "\\\\"); break;
      case '\n': tb_cat(tb, "\\n"); break;
      case '\r': tb_cat(tb, "\\r"); break;
      case '\t': tb_cat(tb, "\\t"); break;
      default:
         if (*p < 0x20 || *p == 0x7f)
            tb_printf(tb, "\\u%04x", *p);
         else if (*p >= 0x80 && latin1) {
            // Convert the ISO-8859-1 internal encoding to UTF-8
            tb_append(tb, 0xc2 + (*p > 0xbf));
            tb_append(tb, (*p & 0x3f) + 0x80);
         }
         else
            tb_append(tb, *p);
      }
   }

   tb_append(tb, '"');
}

static void json_loc(text_buf_t *tb, const loc_t *loc)
{
   // Lines and columns are one-based, columns are in bytes and the end
   // column is exclusive

   loc_file_t *file_data = loc_file_data(loc);

   tb_cat(tb, "\"file\":");
   json_string(tb, file_data->name_str, strlen(file_data->name_str), false);

   const unsigned line = loc->first_line;
   const unsigned col = loc->first_column + 1;

   unsigned end_line = line, end_col = col + 1;
   if (loc->line_delta == 0 && loc->column_delta != DELTA_INVALID)
      end_col = col + loc->column_delta + 1;
   else if (loc->line_delta != DELTA_INVALID && loc->line_delta > 0) {
      // The end column of a location spanning several lines is not
      // reliable so use the end of the last line
      end_line = line + loc->line_delta;

      loc_t last = *loc;
      last.first_line = end_line;
      last.first_column = 0;
      last.line_delta = 0;
      const char *src = loc_get_source(&last);
      if (src != NULL) {
         const char *eol = src;
         while (*eol != '\0' && *eol != '\n')
            eol++;
         end_col = (eol - src) + 1;
      }
      else
         end_col = 2;
   }

   tb_printf(tb, ",\"line\":%u,\"col\":%u,\"end_line\":%u,\"end_col\":%u",
             line, col, end_line, end_col);
}

static void diag_format_json(diag_t *d, ostream_t *os)
{
   LOCAL_TEXT_BUF tb = tb_new();

   const char *level = "note";
   switch (d->level) {
   case DIAG_DEBUG:   level = "debug"; break;
   case DIAG_NOTE:    level = "note"; break;
   case DIAG_WARN:    level = "warning"; break;
   case DIAG_ERROR:   level = "error"; break;
   case DIAG_FAILURE: level = "failure"; break;
   case DIAG_FATAL:   level = "fatal"; break;
   }

   tb_printf(tb, "{\"level\":\"%s\",\"message\":", level);

   // Hints without a location are part of the message
   LOCAL_TEXT_BUF msg = tb_new();
   tb_catn(msg, tb_get(d->msg), tb_len(d->msg));
   for (int i = 0; i < d->hints.count; i++) {
      const diag_hint_t *h = &(d->hints.items[i]);
      if (h->text != NULL && loc_invalid_p(&(h->loc)))
         tb_printf(msg, "\n%s", h->text);
   }

   json_string(tb, tb_get(msg), tb_len(msg), true);

   if (d->rule != NULL) {
      tb_cat(tb, ",\"rule\":");
      json_string(tb, d->rule, strlen(d->rule), false);
   }

   if (d->relaxable)
      tb_cat(tb, ",\"relaxable\":true");

   const loc_t *primary = NULL;
   if (d->hints.count > 0 && !loc_invalid_p(&(d->hints.items[0].loc)))
      primary = &(d->hints.items[0].loc);

   if (primary != NULL) {
      tb_append(tb, ',');
      json_loc(tb, primary);
   }

   bool first = true;
   for (int i = 0; i < d->hints.count; i++) {
      const diag_hint_t *h = &(d->hints.items[i]);
      if (h->text == NULL || loc_invalid_p(&(h->loc)))
         continue;

      tb_cat(tb, first ? ",\"related\":[{" : ",{");
      first = false;
      json_loc(tb, &(h->loc));
      tb_cat(tb, ",\"message\":");
      json_string(tb, h->text, strlen(h->text), true);
      tb_append(tb, '}');
   }

   if (!first)
      tb_append(tb, ']');

   if (d->edits.count > 0) {
      tb_cat(tb, ",\"fix\":{\"title\":");
      const char *title = d->fix_title ?: "Fix";
      json_string(tb, title, strlen(title), true);
      tb_cat(tb, ",\"edits\":[");
      for (int i = 0; i < d->edits.count; i++) {
         const diag_edit_t *e = &(d->edits.items[i]);
         if (i > 0)
            tb_append(tb, ',');
         tb_append(tb, '{');
         json_loc(tb, &(e->loc));
         tb_cat(tb, ",\"text\":");
         json_string(tb, e->text, strlen(e->text), true);
         tb_append(tb, '}');
      }
      tb_cat(tb, "]}");
   }

   tb_cat(tb, "}\n");
   ostream_write(os, tb_get(tb), tb_len(tb));
}

static inline bool diag_has_message(diag_t *d)
{
   return tb_len(d->msg) > 0;
}

static void diag_format_full(diag_t *d, ostream_t *os)
{
   if (diag_has_message(d)) {
      int col = 0;
      if (d->prefix) {
         switch (d->level) {
         case DIAG_DEBUG:   col = nvc_fprintf(os, DEBUG_PREFIX); break;
         case DIAG_NOTE:    col = nvc_fprintf(os, NOTE_PREFIX); break;
         case DIAG_WARN:    col = nvc_fprintf(os, WARNING_PREFIX); break;
         case DIAG_ERROR:   col = nvc_fprintf(os, ERROR_PREFIX); break;
         case DIAG_FAILURE: col = nvc_fprintf(os, FAILURE_PREFIX); break;
         case DIAG_FATAL:   col = nvc_fprintf(os, FATAL_PREFIX); break;
         }
      }

      diag_wrap_lines(tb_get(d->msg), tb_len(d->msg), col, os);
      ostream_putc(os, '\n');
   }

   if (d->hints.count > 0)
      diag_emit_hints(d, os);

   if (d->trace.count > 0)
      diag_emit_trace(d, os);

#if TRAILING_BLANK
   if (d->trace.count > 0 || d->hints.count > 0)
      ostream_putc(os, '\n');
#endif
}

void diag_emit(diag_t *d)
{
   // The rule is part of the text unless the message style carries it
   // as a separate field
   if (d->rule != NULL && !d->rule_shown
       && get_message_style() != MESSAGE_JSON) {
      tb_printf(d->msg, " [lint:%s]", d->rule);
      d->rule_shown = true;
   }

   bool limit_note = false;
   if (soft_limit && error_limit > 0 && d->level == DIAG_ERROR
       && !d->suppress && !d->quiet) {
      // The limit only decides what is shown: the rest of the run goes on
      const unsigned shown = relaxed_load(&n_diags[DIAG_ERROR]);
      if (shown >= error_limit)
         d->quiet = true;
      else if (shown + 1 == error_limit)
         limit_note = true;
   }

   if (d->suppress)
      goto cleanup;
   else if (d->quiet) {
      // Counted but not shown: see diag_set_scan_filter
   }
   else if (consumer_fn != NULL && d->level > DIAG_DEBUG)
      (*consumer_fn)(d, consumer_ctx);
   else if (d->level == DIAG_DEBUG && opt_get_int(OPT_UNIT_TEST)
            && diag_has_message(d))
      goto cleanup;
   else {
      // A fatal error may end the program: it appears after any output
      // held to be written in a fixed order
      if (d->level == DIAG_FATAL)
         nvc_output_urgent_begin();

      // The stderr and stdout streams are often redirected to the same
      // file so ensure that the output appears in a logical order
      fflush(stderr);
      fflush(stdout);

      diag_set_ostream(d);  // Level may have changed

      SCOPED_LOCK(diag_lock);

      if (get_message_style() == MESSAGE_COMPACT)
         diag_format_compact(d, d->ostream);
      else if (get_message_style() == MESSAGE_JSON)
         diag_format_json(d, d->ostream);
      else
         diag_format_full(d, d->ostream);

      fflush(d->ostream->context);

      if (d->level == DIAG_FATAL)
         nvc_output_urgent_end();
   }

   const unsigned count = relaxed_add(&n_diags[d->level], 1);

   if (!d->quiet && d->level >= DIAG_ERROR)
      relaxed_add(&scan_visible, 1);

   if (soft_limit && error_limit > 0) {
      const unsigned hard = MAX(1000, error_limit * 50);
      if (d->level == DIAG_ERROR && count == hard)
         fatal("too many errors, giving up");
   }
   else if (d->level == DIAG_ERROR && count == error_limit)
      fatal("too many errors, giving up");

   if (limit_note) {
      diag_t *n = diag_new(DIAG_NOTE, NULL);
      diag_printf(n, "%u errors shown; further errors in this run are not "
                  "shown", error_limit);
      diag_emit(n);
   }

 cleanup:
   for (int i = 0; i < d->hints.count; i++)
      free(d->hints.items[i].text);
   ACLEAR(d->hints);

   for (int i = 0; i < d->trace.count; i++)
      free(d->trace.items[i].text);
   ACLEAR(d->trace);

   for (int i = 0; i < d->edits.count; i++)
      free(d->edits.items[i].text);
   ACLEAR(d->edits);

   free(d->rule);
   free(d->fix_title);

   tb_free(d->msg);
   free(d);
}

void diag_show_source(diag_t *d, bool show)
{
   d->source = show;
}

void diag_suppress(diag_t *d, bool suppress)
{
   d->suppress = suppress;
}

void diag_set_scan_filter(bool on)
{
   scan_filter = on;
}

void diag_scan_visible(diag_t *d)
{
   d->quiet = false;
}

unsigned diag_scan_errors(void)
{
   return relaxed_load(&scan_visible);
}

diag_consumer_t diag_get_consumer(void **context)
{
   *context = consumer_ctx;
   return consumer_fn;
}

void diag_set_consumer(diag_consumer_t fn, void *context)
{
   consumer_fn = fn;
   consumer_ctx = context;
}

const char *diag_get_text(diag_t *d)
{
   return tb_get(d->msg);
}

const char *diag_get_hint(diag_t *d, int nth)
{
   if (d->hints.items[0].text == NULL) nth++;
   assert(nth < d->hints.count);
   return d->hints.items[nth].text;
}

const char *diag_get_trace(diag_t *d, int nth)
{
   assert(nth < d->trace.count);
   return d->trace.items[nth].text;
}

const loc_t *diag_get_loc(diag_t *d)
{
   if (d->hints.count > 0)
      return &(d->hints.items[0].loc);
   else
      return NULL;
}

int diag_hints(diag_t *d)
{
   if (d->hints.items && d->hints.items[0].text == NULL)
      return d->hints.count - 1;
   else
      return d->hints.count;
}

int diag_traces(diag_t *d)
{
   return d->trace.count;
}

void diag_add_hint_fn(diag_hint_fn_t fn, void *context)
{
   int idx = 0;
   for (; idx < MAX_HINT_RECS; idx++) {
      if (hint_recs[idx].fn == NULL)
         break;
   }

   if (idx == MAX_HINT_RECS)
      fatal_trace("too many active hint callbacks");

   hint_recs[idx].fn = fn;
   hint_recs[idx].context = context;
}

void diag_remove_hint_fn(diag_hint_fn_t fn, void *context)
{
   for (int i = 0; i < MAX_HINT_RECS; i++) {
      if (hint_recs[i].fn == fn && hint_recs[i].context == context) {
         hint_recs[i].fn = NULL;
         hint_recs[i].context = NULL;
         return;
      }
   }

   fatal_trace("hint function %p (%p) not registered", fn, context);
}

unsigned diag_count(diag_level_t level)
{
   int sum = 0;
   for (int i = level; i <= DIAG_FATAL; i++)
      sum += relaxed_load(&n_diags[i]);
   return sum;
}

unsigned error_count(void)
{
   return diag_count(DIAG_ERROR);
}

void reset_error_count(void)
{
   for (int i = 0; i <= DIAG_FATAL; i++)
      relaxed_store(&n_diags[i], 0);
}

bool diag_set_soft_limit(bool on)
{
   const bool old = soft_limit;
   soft_limit = on;
   return old;
}

unsigned set_error_limit(unsigned limit)
{
   const unsigned old = error_limit;
   error_limit = limit;
   return old;
}

void fmt_loc(FILE *f, const loc_t *loc)
{
   // Legacy interface for debugging only
   diag_t *d = diag_new(DIAG_DEBUG, loc);
   diag_consumer_t old = consumer_fn;
   consumer_fn = NULL;
   diag_emit(d);
   consumer_fn = old;
}

void wrapped_vprintf(const char *fmt, va_list ap)
{
   LOCAL_TEXT_BUF tb = tb_new();

   ostream_t *stdout_os = nvc_stdout();
   ostream_t os = { tb_ostream_write, tb, CHARSET_ISO88591, stdout_os->flags };
   nvc_vfprintf(&os, fmt, ap);

   diag_wrap_lines(tb_get(tb), tb_len(tb), 0, nvc_stdout());
}

void wrapped_printf(const char *fmt, ...)
{
   va_list ap;
   va_start(ap, fmt);

   wrapped_vprintf(fmt, ap);

   va_end(ap);
}

void diag_set_relaxable(diag_t *d)
{
   // The error would be accepted or only a warning with --relaxed
   d->relaxable = true;
}

void diag_hint_relaxable(diag_t *d)
{
   if (!d->relaxable) {
      diag_set_relaxable(d);
      diag_hint(d, NULL, "the $bold$--relaxed$$ option downgrades this "
                "to a warning");
   }
}

diag_t *diag_new_relaxable(const loc_t *loc)
{
   // An error which is only a warning with --relaxed
   if (opt_get_int(OPT_RELAXED))
      return diag_new(DIAG_WARN, loc);

   diag_t *d = diag_new(DIAG_ERROR, loc);
   diag_hint_relaxable(d);
   return d;
}

diag_t *_pedantic_diag(const loc_t *loc, int *warned, bool *error)
{
   const bool relaxed = opt_get_int(OPT_RELAXED);
   if (!relaxed || !*warned) {
      const diag_level_t level = relaxed ? DIAG_WARN : DIAG_ERROR;
      diag_t *d = diag_new(level, loc);
      if (level == DIAG_ERROR)
         diag_hint_relaxable(d);

      *warned = 1;
      if (error) *error = !relaxed;
      return d;
   }
   else
      return NULL;
}
