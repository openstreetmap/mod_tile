builddir     = .
top_dir:=$(shell /usr/sbin/apxs -q exp_installbuilddir)
top_dir:=$(shell /usr/bin/dirname ${top_dir})

top_srcdir   = ${top_dir}
top_builddir = ${top_dir}

include ${top_builddir}/build/special.mk

CXX := g++
CXXFLAGS += -Wall

APXS      = apxs
APACHECTL = apachectl
EXTRA_CFLAGS = -I$(builddir)

EXTRA_CPPFLAGS += -g -O2 -Wall
EXTRA_LDFLAGS += $(shell pkg-config --libs libagg)

all: local-shared-build renderd speedtest render_list

clean:
	rm -f *.o *.lo *.slo *.la .libs/*
	rm -f renderd

RENDER_CPPFLAGS += -g -O2 -Wall
RENDER_CPPFLAGS += -I/usr/local/include/mapnik
RENDER_CPPFLAGS += $(shell pkg-config --cflags freetype2)
#RENDER_CPPFLAGS += $(shell Magick++-config --cxxflags --cppflags)
RENDER_CPPFLAGS += $(shell pkg-config --cflags libagg)

RENDER_LDFLAGS += -g
RENDER_LDFLAGS += -lmapnik -L/usr/local/lib64
RENDER_LDFLAGS += $(shell pkg-config --libs freetype2)
#RENDER_LDFLAGS += $(shell Magick++-config --ldflags --libs)
RENDER_LDFLAGS += $(shell pkg-config --libs libagg)

renderd: daemon.c gen_tile.cpp dir_utils.c protocol.h render_config.h dir_utils.h
	$(CXX) -o $@ $^ $(RENDER_LDFLAGS) $(RENDER_CPPFLAGS)

speedtest: render_config.h protocol.h dir_utils.c dir_utils.h

render_list: render_config.h protocol.h dir_utils.c dir_utils.h

MYSQL_CFLAGS += -g -O2 -Wall
MYSQL_CFLAGS += $(shell mysql_config --cflags)

MYSQL_LDFLAGS += $(shell mysql_config --libs)

mysql2file: mysql2file.c
	$(CC) $(MYSQL_CFLAGS) $(MYSQL_LDFLAGS) -o $@ $^

# Not sure why this is not created automatically
.deps:
	touch .deps
