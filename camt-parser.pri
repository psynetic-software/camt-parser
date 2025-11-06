# Root of the parser project
CAMT_ROOT = $$PWD

DEFINES+=USE_UTF8PROC UTF8PROC_STATIC

# Public headers
INCLUDEPATH += $$CAMT_ROOT

# Source files of the parser
SOURCES += \
    $$CAMT_ROOT/gvc_map.cpp

HEADERS += \
    $$CAMT_ROOT/camt_parser_pugi.hpp \
    $$CAMT_ROOT/camt_csv.hpp \
    $$CAMT_ROOT/utf_convert.hpp \
    $$CAMT_ROOT/gvc_map.hpp

# pugixml (always needed)
PUGI = $$CAMT_ROOT/external/pugixml/src
INCLUDEPATH += $$PUGI
SOURCES += $$PUGI/pugixml.cpp

# utf8proc (optional - only if the user defines USE_UTF8PROC)
contains(DEFINES, USE_UTF8PROC) {
    UTF8 = $$CAMT_ROOT/external/utf8proc
    INCLUDEPATH += $$UTF8
    SOURCES += $$UTF8/utf8proc.c
    HEADERS += $$UTF8/utf8proc.h
}
