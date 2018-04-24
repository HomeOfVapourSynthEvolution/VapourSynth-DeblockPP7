warning_flags = -Wall -Wextra -Wshadow -Wno-unused-parameter -Wno-attributes
common_cflags = -O3 -ffast-math -fvisibility=hidden $(warning_flags) $(MFLAGS)
AM_CXXFLAGS = -std=c++14 $(common_cflags)

AM_CPPFLAGS = $(VapourSynth_CFLAGS)

lib_LTLIBRARIES = libdeblockpp7.la

libdeblockpp7_la_SOURCES = DeblockPP7/DeblockPP7.cpp \
                           DeblockPP7/DeblockPP7.hpp \
                           DeblockPP7/vectorclass/instrset.h \
                           DeblockPP7/vectorclass/instrset_detect.cpp

if VS_TARGET_CPU_X86
libdeblockpp7_la_SOURCES += DeblockPP7/DeblockPP7_SSE2.cpp \
                            DeblockPP7/vectorclass/vectorclass.h \
                            DeblockPP7/vectorclass/vectorf128.h \
                            DeblockPP7/vectorclass/vectori128.h
endif

libdeblockpp7_la_LDFLAGS = -no-undefined -avoid-version $(PLUGINLDFLAGS)
