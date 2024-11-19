AUTOMAKE_OPTIONS = foreign 

include $(srcdir)/common.mk

CFLAGS = -Wall
include_HEADERS =

 
bin_PROGRAMS = epxcore
epxcore_LDADD = ../libep/libep.a ../liblog/liblog.a
epxcore_SOURCES = util.c sharedVar.c middle.c md5.c main.c fpga.c driver.c command.c bite.c epxcore.c
