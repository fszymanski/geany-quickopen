#  Copyright (C) 2020 Filip Szymański <fszymanski(dot)pl(at)gmail(dot)com>
#
#  This program is free software: you can redistribute it and/or modify
#  it under the terms of the GNU General Public License as published by
#  the Free Software Foundation, either version 3 of the License, or
#  (at your option) any later version.
#
#  This program is distributed in the hope that it will be useful,
#  but WITHOUT ANY WARRANTY; without even the implied warranty of
#  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
#  GNU General Public License for more details.
#
#  You should have received a copy of the GNU General Public License
#  along with this program. If not, see <https://www.gnu.org/licenses/>.
#

CC = gcc
CFLAGS = -std=c11 -Wall -Wextra -O2 -fPIC -I. `pkg-config --cflags geany`
LDFLAGS = -shared `pkg-config --libs geany`
RM = rm -f
DEFS = -DLOCALEDIR=\"\" -DGETTEXT_PACKAGE=\"geany-quickopen\"

SRCS = geany_quickopen.c
OBJS = $(SRCS:.c=.o)

TARGET_LIB = geany-quickopen.so
PLUGIN_LIBDIR = `pkg-config --variable=libdir geany`/geany

.PHONY: all
all: $(TARGET_LIB)

%.o: %.c
	$(CC) $(DEFS) -c -o $@ $< $(CFLAGS)

$(TARGET_LIB): $(OBJS)
	$(CC) -o $@ $^ $(LDFLAGS)

.PHONY: clean
clean:
	$(RM) $(OBJS) $(TARGET_LIB)

.PHONY: install
install:
	mkdir -p $(DESTDIR)$(PLUGIN_LIBDIR)
	install $(TARGET_LIB) $(DESTDIR)$(PLUGIN_LIBDIR)/$(TARGET_LIB)

.PHONY: uninstall
uninstall:
	$(RM) $(DESTDIR)$(PLUGIN_LIBDIR)/$(TARGET_LIB)
