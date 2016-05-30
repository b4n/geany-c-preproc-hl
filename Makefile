#!/usr/bin/make -f

PLUGIN = c-preproc-hl
PLUGIN_CFLAGS = -DLOCALEDIR='"$(datadir)/locale"' \
                -DGETTEXT_PACKAGE='"$(PLUGIN)"'

VPATH ?= .

# fetch from https://github.com/b4n/geany-plugin.mk
include $(VPATH)/geany-plugin.mk
