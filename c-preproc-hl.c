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

#include <glib.h>
#include <geanyplugin.h>
#include <SciLexer.h>


GeanyPlugin    *geany_plugin;
GeanyData      *geany_data;
GeanyFunctions *geany_functions;

PLUGIN_VERSION_CHECK (211) /* FIXME */
PLUGIN_SET_INFO ("C Proprocessor highlight", "Highlights disabled C code",
                 "0.1", "Colomban Wendling <colomban@geany.org>")

#if 0
static void rgb2hsl (const double r,
                     const double g,
                     const double b,
                     double *const h,
                     double *const s,
                     double *const l)
{
  double xmin = r, xmax = r;
  if (xmin > g) xmin = g;
  if (xmax < g) xmax = g;
  if (xmin > b) xmin = b;
  if (xmax < b) xmax = b;
  
  *l = (xmin + xmax) / 2;
  
  if (xmin == xmax)   *s = 0;
  else if (*l < 0.5)  *s = (xmax - xmin) / (xmax + xmin);
  else                *s = (xmax - xmin) / (2 - xmax + xmin);
  
  if (xmin == xmax)   *h = 0;
  else if (r == xmax) *h = 0 + (g - b) / (xmax - xmin);
  else if (g == xmax) *h = 2 + (b - r) / (xmax - xmin);
  else if (b == xmax) *h = 4 + (r - g) / (xmax - xmin);
  else *h = 0;
  if (*h < 0)
    *h += 6;
}

static void hsl2rgb (/*const*/ double h,
                     const double s,
                     const double l,
                     double *const r,
                     double *const g,
                     double *const b)
{
  if (s == 0)
    *r = *g = *b = l;
  else
  {
    double t1, t2, t3;
    
    if (l < 0.5) t2 = l * (1 + s);
    else         t2 = l + s - l * s;
    t1 = 2 * l - t2;
    
    h = h/6.;

#define WTF(c) \
    if (t3 < 1/6.) c = t1 + (t2 - t1 ) * 6 * t3; \
    else if (t3 < 1/2.) c = t2; \
    else if (t3 < 2/3.) c = t1 + (t2 - t1) * (2/3. - t3) * 6; \
    else c = t1;

    /* r */ t3 = h + 1/3.; if (t3 > 1) t3 -= 1; WTF (*r);
    /* g */ t3 = h; WTF (*g);
    /* b */ t3 = h - 1/3.; if (t3 < 0) t3 += 1; WTF (*b);
#undef WTF
  }
}
#endif

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

#if 0
static sptr_t color_mul (const sptr_t c,
                         const double f)
{
  const sptr_t b = (c >> 16) & 0xff;
  const sptr_t g = (c >>  8) & 0xff;
  const sptr_t r = (c >>  0) & 0xff;
  
  return ((sptr_t) (0x7f + (b - 0x7f) * f) << 16 |
          (sptr_t) (0x7f + (g - 0x7f) * f) <<  8 |
          (sptr_t) (0x7f + (r - 0x7f) * f) <<  0);
}


static sptr_t color_mul2 (const sptr_t c,
                         const double f)
{
  double b = ((c >> 16) & 0xff) / 256.;
  double g = ((c >>  8) & 0xff) / 256.;
  double r = ((c >>  0) & 0xff) / 256.;
  
  double h, s, l;
  rgb2hsl (r, g, b, &h, &s, &l);
  g_debug("h, s, l = %g, %g, %g", h, s, l);
  g_debug("h = %g", l);
  //~ l = 0.5 + (l - 0.5) * f;
  s = 0.5 + (s - 0.5) * f;
  s = MIN (s, 0.2);
  l = MAX (l, 0.7);
  g_debug("h = %g", l);
  //~ s = 1 + (s / 2) - s;
  //~ l *= f;
  //~ s /= f;
  hsl2rgb (h, s, l, &r, &g, &b);
  g_debug("r, b, g = %g, %g, %g", r, g, b);
  
  return ((sptr_t) (b * 256) << 16 |
          (sptr_t) (g * 256) <<  8 |
          (sptr_t) (r * 256) <<  0);
}

#define COLOR_MUL(c, f) color_mul2(c,f)

#define BG_COLOR_MUL 0.2
#define FG_COLOR_MUL 0.1
#endif
#define BG_COLOR_BLEND 1.0
#define FG_COLOR_BLEND 0.5

#define ACTIVITY_FLAG 0x40 /* see LexCPP */


static void on_filetype_set (GObject       *obj,
                             GeanyDocument *doc,
                             GeanyFiletype *filetype_old,
                             gpointer       user_data)
{
  sptr_t lexer = scintilla_send_message (doc->editor->sci, SCI_GETLEXER, 0, 0);

  if (lexer == SCLEX_CPP) {
    uptr_t i;

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

void plugin_init (GeanyData *data)
{
  plugin_signal_connect (geany_plugin, NULL, "document-filetype-set", TRUE,
                         G_CALLBACK (on_filetype_set), NULL);
}

void plugin_cleanup (void)
{
}
