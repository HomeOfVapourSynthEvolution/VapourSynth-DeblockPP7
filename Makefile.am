warning_flags = -Wall -Wextra -Wshadow -Wno-unused-parameter
common_cflags = -O3 -ffast-math -fvisibility=hidden $(warning_flags) $(MFLAGS)
AM_CXXFLAGS = -std=c++14 $(common_cflags)

AM_CPPFLAGS = $(VapourSynth_CFLAGS)

lib_LTLIBRARIES = libdeblockpp7.la

libdeblockpp7_la_SOURCES = DeblockPP7/DeblockPP7.cpp

libdeblockpp7_la_LDFLAGS = -no-undefined -avoid-version $(PLUGINLDFLAGS)
