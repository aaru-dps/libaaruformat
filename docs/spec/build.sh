#!/bin/bash
asciidoctor-pdf -a pdf-theme=theme.yml -a "pdf-fontsdir=fonts;GEM_FONTS_DIR" -a source-highlighter=rouge spec.adoc
