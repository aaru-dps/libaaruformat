# This file is part of the Aaru Data Preservation Suite.
# Copyright (c) 2019-2026 Natalia Portillo.
#
# This library is free software; you can redistribute it and/or modify
# it under the terms of the GNU Lesser General Public License as
# published by the Free Software Foundation; either version 2.1 of the
# License, or (at your option) any later version.
#
# This library is distributed in the hope that it will be useful, but
# WITHOUT ANY WARRANTY; without even the implied warranty of
# MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the GNU
# Lesser General Public License for more details.
#
# You should have received a copy of the GNU Lesser General Public
# License along with this library; if not, see <http://www.gnu.org/licenses/>.

# Find asciidoctor-pdf executable
find_program(ASCIIDOCTOR_PDF asciidoctor-pdf)

if(ASCIIDOCTOR_PDF)
    message(STATUS "Found asciidoctor-pdf: ${ASCIIDOCTOR_PDF}")

    # Add custom target to build the specification PDF
    add_custom_target(spec
        COMMAND ${ASCIIDOCTOR_PDF}
            -a pdf-theme=theme.yml
            -a "pdf-fontsdir=fonts;GEM_FONTS_DIR"
            -a source-highlighter=rouge
            spec.adoc
        WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}
        COMMENT "Building AaruFormat specification PDF with asciidoctor-pdf"
        VERBATIM
    )

    message(STATUS "Added 'spec' target to build the specification PDF")
else()
    message(STATUS "asciidoctor-pdf not found - 'spec' target will not be available")
    message(STATUS "Install it with: gem install asciidoctor-pdf")
endif()

