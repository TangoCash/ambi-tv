# ambi-tv: a flexible ambilight clone for embedded linux
# Copyright (C) 2013 Georg Kaindl
# Copyright (C) 2016 TangoCash
# 
# This file is part of ambi-tv.
# 
# ambi-tv is free software: you can redistribute it and/or modify
# it under the terms of the GNU General Public License as published by
# the Free Software Foundation, either version 2 of the License, or
# (at your option) any later version.
# 
# ambi-tv is distributed in the hope that it will be useful,
# but WITHOUT ANY WARRANTY; without even the implied warranty of
# MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
# GNU General Public License for more details.
# 
# You should have received a copy of the GNU General Public License
# along with ambi-tv.  If not, see <http://www.gnu.org/licenses/>.
#

CFLAGS = -Wall
LDFLAGS = -lpthread -lasound -lm -lfftw3
CC = gcc

ifndef LOCALBUILD
	CFLAGS += -march=armv6 -mfpu=vfp -mfloat-abi=hard
else
	CFLAGS += -std=gnu90 -Wno-int-to-pointer-cast -Wno-pointer-to-int-cast
endif

ifdef DEBUG
	CFLAGS += -ggdb
else
	CFLAGS += -O3
endif

AMBITV = ambi-tv
BINDIR = /usr/bin
ETCDIR = /etc

SRC_AMBITV = src/main.c src/video-fmt.c src/parse-conf.c src/component.c	\
	src/registrations.c src/util.c src/program.c src/log.c src/color.c		\
	src/gpio.c src/dma.c src/mailbox.c src/rpihw.c src/pwm.c src/pwm_dev.c	\
	src/components/v4l2-grab-source.c										\
	src/components/audio-grab-source.c										\
	src/components/avg-color-processor.c									\
	src/components/ledstripe-sink.c 										\
	src/components/timer-source.c											\
	src/components/uart-sink.c												\
	src/components/udpraw-sink.c											\
	src/components/edge-color-processor.c									\
	src/components/audio-processor.c										\
	src/components/mood-light-processor.c									\
	src/components/web-processor.c

OBJ_AMBITV = $(SRC_AMBITV:.c=.o)

dir=@mkdir -p bin

all: $(AMBITV)

ambi-tv: $(OBJ_AMBITV)
	$(dir)
	$(CC) -o bin/$@ $(OBJ_AMBITV) $(LDFLAGS)

.c.o:
	$(CC) $(CFLAGS) -c $< -o $@

clean:
	rm -f $(OBJ_AMBITV)
	rm -rf bin

install: $(AMBITV)
	sudo install -d $(BINDIR)
	sudo install -m 755 -o root -g root bin/$(AMBITV) $(BINDIR)
	sudo install -m 755 -o root -g root $(AMBITV).init.sh $(ETCDIR)/init.d/$(AMBITV)
	sudo install -m 644 -o root -g root $(AMBITV).conf $(ETCDIR)/$(AMBITV).conf
	sudo update-rc.d $(AMBITV) defaults
