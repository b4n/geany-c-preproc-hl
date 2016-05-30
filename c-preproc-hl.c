/*
 * Copyright 2015 Colomban Wendling <colomban@geany.org>
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *     http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

#ifdef HAVE_CONFIG_H
# include "config.h"
#endif

#include <glib.h>
#include <geanyplugin.h>
#include <SciLexer.h>

/* FIXME: get this from Geany */
#define TM_PARSER_C 0
#define TM_PARSER_CPP 1


static sptr_t color_blend (const sptr_t a,
                           const sptr_t b,
                           const double p)
{
#if 1
  sptr_t r = 0;
  unsigned int i;

  for (i = 0; i < 3; i++) {
    const sptr_t c_a = (a >> (8 * i)) & 0xff;
    const sptr_t c_b = (b >> (8 * i)) & 0xff;

    r |= (sptr_t) ((double) c_a * (1 - p) + (double) c_b * p) << (8 * i);
  }

  return r;
#else
  /* is there a way to do that magically in one single computation?
   * it looks like maybe possible, but can't find how */
  sptr_t P = p * 0xff;
  return (a * (0x010101 * (0xff - P)) / 0xffffff) +
         (b * (0x010101 *         P ) / 0xffffff);
#endif
}

#define BG_COLOR_BLEND 1.0
#define FG_COLOR_BLEND 0.5

#define ACTIVITY_FLAG 0x40 /* see LexCPP */


static gboolean document_is_supported (GeanyDocument *doc)
{
  return SCLEX_CPP == scintilla_send_message (doc->editor->sci, SCI_GETLEXER, 0, 0);
}

static void setup_document (GeanyDocument *doc)
{
  if (document_is_supported (doc)) {
    uptr_t i;

    scintilla_send_message (doc->editor->sci, SCI_SETPROPERTY,
                            (uptr_t) "lexer.cpp.track.preprocessor", (sptr_t) "1");
    scintilla_send_message (doc->editor->sci, SCI_SETPROPERTY,
                            (uptr_t) "lexer.cpp.update.preprocessor", (sptr_t) "1");

    for (i = 0; i < ACTIVITY_FLAG; i++) {
      sptr_t v, fg, bg;
      
      bg = scintilla_send_message (doc->editor->sci, SCI_STYLEGETBACK, i, 0);
      bg = color_blend (0x7f7f7f, bg, BG_COLOR_BLEND);
      scintilla_send_message (doc->editor->sci, SCI_STYLESETBACK,
                              i | ACTIVITY_FLAG, bg /*COLOR_MUL (bg, BG_COLOR_MUL)*/);
      fg = scintilla_send_message (doc->editor->sci, SCI_STYLEGETFORE, i, 0);
      fg = color_blend (bg, fg, FG_COLOR_BLEND);
      scintilla_send_message (doc->editor->sci, SCI_STYLESETFORE,
                              i | ACTIVITY_FLAG, fg /*COLOR_MUL (fg, FG_COLOR_MUL)*/);

#define COPY_SCI_STYLE(which)                                                 \
  v = scintilla_send_message (doc->editor->sci, SCI_STYLEGET##which, i, 0);   \
  scintilla_send_message (doc->editor->sci, SCI_STYLESET##which,              \
                          i | ACTIVITY_FLAG, v);

      COPY_SCI_STYLE (BOLD)
      COPY_SCI_STYLE (ITALIC)
      COPY_SCI_STYLE (SIZE)
      COPY_SCI_STYLE (EOLFILLED)
      COPY_SCI_STYLE (UNDERLINE)
      COPY_SCI_STYLE (CASE)
      COPY_SCI_STYLE (VISIBLE)

#undef COPY_SCI_STYLE
    }
  }
}

static void on_filetype_set (GObject       *obj,
                             GeanyDocument *doc,
                             GeanyFiletype *filetype_old,
                             gpointer       user_data)
{
  setup_document (doc);
}

static gboolean lang_compatible (TMParserType a,
                                 TMParserType b)
{
  /* mostly stolen from Geany */
  return (a == b ||
          (a == TM_PARSER_CPP && b == TM_PARSER_C) ||
          (a == TM_PARSER_C && b == TM_PARSER_CPP));
}

/* try and determine whether @tag comes from a header or not.  @tag->local
 * doesn't work because it's dummy in our implementation (see isIncludeFile()
 * in CTags' source) */
static gboolean tag_is_header (TMTag *tag)
{
  const gchar *ext;
  
  if (tag->file && tag->file->short_name &&
      (ext = strrchr (tag->file->short_name, '.'))) {
    ++ext;
    
    return ((ext[0] == 'h' /* optimize the match for .h* */ &&
             (ext[1] == 0 /* h */ ||
              ! g_ascii_strcasecmp (&ext[1], "h") /* hh */ ||
              ! g_ascii_strcasecmp (&ext[1], "p") /* hp */ ||
              ! g_ascii_strcasecmp (&ext[1], "pp") /* hpp */ ||
              ! g_ascii_strcasecmp (&ext[1], "xx") /* hxx */ ||
              ! g_ascii_strcasecmp (&ext[1], "++") /* h++ */)) ||
            ! g_ascii_strcasecmp (ext, "tcc") /* from GCC's manual */);
  } else {
    return TRUE; /* global tag file (tag->file == NULL) or no extension */
  }
}

static void collect_preprocessor_definitions_append (GPtrArray     *tags,
                                                     GString       *str,
                                                     TMParserType   lang,
                                                     TMSourceFile  *current_file)
{
  if (tags) {
    guint i;
    
    for (i = 0; i < tags->len; i++) {
      TMTag *tag = tags->pdata[i];
      
      if (tag && lang_compatible (lang, tag->lang) &&
          tag->type & (tm_tag_macro_t | tm_tag_macro_with_arg_t) &&
          (! tag->file || tag->file != current_file) &&
          tag_is_header (tag)) {
        if (str->len > 0) {
          g_string_append_c (str, ' ');
        }
        /* FIXME: we should find the value of the define somehow, so we catch
         * 0s, and we should also catch undefs as such so we can drop it. */
        g_string_append(str, tag->name);
      }
    }
  }
}

static gchar *collect_preprocessor_definitions (const TMWorkspace  *ws,
                                                TMParserType        lang,
                                                TMSourceFile       *current_file)
{
  GString *str = g_string_new (NULL);
  
  collect_preprocessor_definitions_append (ws->tags_array, str, lang, current_file);
  collect_preprocessor_definitions_append (ws->global_tags, str, lang, current_file);
  
  return g_string_free (str, FALSE);
}

static void on_document_activate (GObject        *obj,
                                  GeanyDocument  *doc,
                                  GeanyPlugin    *plugin)
{
  if (document_is_supported (doc)) {
    gchar *words = collect_preprocessor_definitions (plugin->geany_data->app->tm_workspace,
                                                     doc->file_type->lang,
                                                     doc->tm_file);
    
    if (words) {
      scintilla_send_message (doc->editor->sci, SCI_SETKEYWORDS, 4, (sptr_t) words);
      g_free (words);
    }
  }
}

static gboolean cph_init (GeanyPlugin  *plugin,
                          gpointer      data)
{
  plugin_signal_connect (plugin, NULL, "document-filetype-set", TRUE,
                         G_CALLBACK (on_filetype_set), NULL);
  plugin_signal_connect (plugin, NULL, "document-activate", TRUE,
                         G_CALLBACK (on_document_activate), plugin);
  
  /* initial setup of currently open documents */
  if (main_is_realized ()) {
    guint i;
    
    for (i = 0; i < plugin->geany_data->documents_array->len; i++) {
      GeanyDocument *doc = plugin->geany_data->documents_array->pdata[i];
      
      if (doc->is_valid) {
        setup_document (doc);
      }
    }
  }
  
  return TRUE;
}

static void cph_cleanup (GeanyPlugin *plugin,
                         gpointer     data)
{
}

G_MODULE_EXPORT
void geany_load_module (GeanyPlugin *plugin)
{
  main_locale_init (LOCALEDIR, GETTEXT_PACKAGE);
  
  plugin->info->name = _("C Proprocessor highlight");
  plugin->info->description = _("Highlights disabled C code");
  plugin->info->version = "0.1";
  plugin->info->author = "Colomban Wendling <colomban@geany.org>";
  
  plugin->funcs->init = cph_init;
  plugin->funcs->cleanup = cph_cleanup;
  
  GEANY_PLUGIN_REGISTER (plugin, 225);
}
