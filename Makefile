# -*- Mode: makefile-gmake -*-

.PHONY: clean test all debug release pkgconfig
.PHONY: print_debug_lib print_release_lib
.PHONY: print_debug_link print_release_link
.PHONY: print_debug_path print_release_path

#
# Required packages
#

PKGS = glib-2.0 gio-2.0 gio-unix-2.0 libglibutil

#
# Default target
#

all: debug release pkgconfig

#
# Library version
#

VERSION_MAJOR = 1
VERSION_MINOR = 0
VERSION_RELEASE = 27

# Version for pkg-config
PCVERSION = $(VERSION_MAJOR).$(VERSION_MINOR).$(VERSION_RELEASE)

#
# Library name
#

NAME = grilio
LIB_NAME = lib$(NAME)
LIB_DEV_SYMLINK = $(LIB_NAME).so
LIB_SYMLINK1 = $(LIB_DEV_SYMLINK).$(VERSION_MAJOR)
LIB_SYMLINK2 = $(LIB_SYMLINK1).$(VERSION_MINOR)
LIB_SONAME = $(LIB_SYMLINK1)
LIB = $(LIB_SONAME).$(VERSION_MINOR).$(VERSION_RELEASE)

#
# Sources
#

SRC = \
  grilio_channel.c \
  grilio_encode.c \
  grilio_hexdump.c \
  grilio_request.c \
  grilio_parser.c \
  grilio_transport.c \
  grilio_transport_socket.c \
  grilio_queue.c

#
# Directories
#

SRC_DIR = src
INCLUDE_DIR = include
BUILD_DIR = build
SPEC_DIR = spec
DEBUG_BUILD_DIR = $(BUILD_DIR)/debug
RELEASE_BUILD_DIR = $(BUILD_DIR)/release

#
# Code coverage
#

ifndef GCOV
GCOV = 0
endif

ifneq ($(GCOV),0)
CFLAGS += --coverage
LDFLAGS += --coverage
endif

#
# Tools and flags
#

CC = $(CROSS_COMPILE)gcc
LD = $(CC)
WARNINGS = -Wall
INCLUDES = -I$(INCLUDE_DIR)
BASE_FLAGS = -fPIC
FULL_CFLAGS = $(BASE_FLAGS) $(CFLAGS) $(DEFINES) $(WARNINGS) $(INCLUDES) \
  -MMD -MP $(shell pkg-config --cflags $(PKGS))
FULL_LDFLAGS = $(BASE_FLAGS) $(LDFLAGS) -shared -Wl,-soname -Wl,$(LIB_SONAME) \
  $(shell pkg-config --libs $(PKGS))
DEBUG_FLAGS = -g
RELEASE_FLAGS =

ifndef KEEP_SYMBOLS
KEEP_SYMBOLS = 0
endif

ifneq ($(KEEP_SYMBOLS),0)
RELEASE_FLAGS += -g
endif

DEBUG_LDFLAGS = $(FULL_LDFLAGS) $(DEBUG_FLAGS)
RELEASE_LDFLAGS = $(FULL_LDFLAGS) $(RELEASE_FLAGS)
DEBUG_CFLAGS = $(FULL_CFLAGS) $(DEBUG_FLAGS) -DDEBUG
RELEASE_CFLAGS = $(FULL_CFLAGS) $(RELEASE_FLAGS) -O2

#
# Files
#

PKGCONFIG = $(BUILD_DIR)/$(LIB_NAME).pc
DEBUG_OBJS = $(SRC:%.c=$(DEBUG_BUILD_DIR)/%.o)
RELEASE_OBJS = $(SRC:%.c=$(RELEASE_BUILD_DIR)/%.o)

#
# Dependencies
#

DEPS = $(DEBUG_OBJS:%.o=%.d) $(RELEASE_OBJS:%.o=%.d)
ifneq ($(MAKECMDGOALS),clean)
ifneq ($(strip $(DEPS)),)
-include $(DEPS)
endif
endif

$(DEBUG_OBJS) $(DEBUG_LIB): | $(DEBUG_BUILD_DIR)
$(RELEASE_OBJS) $(RELEASE_LIB): | $(RELEASE_BUILD_DIR)

#
# Rules
#

DEBUG_LIB = $(DEBUG_BUILD_DIR)/$(LIB)
RELEASE_LIB = $(RELEASE_BUILD_DIR)/$(LIB)
DEBUG_LINK = $(DEBUG_BUILD_DIR)/$(LIB_SONAME)
RELEASE_LINK = $(RELEASE_BUILD_DIR)/$(LIB_SONAME)

debug: $(DEBUG_LIB) $(DEBUG_LINK)

release: $(RELEASE_LIB) $(RELEASE_LINK)

pkgconfig: $(PKGCONFIG)

print_debug_lib:
	@echo $(DEBUG_LIB)

print_release_lib:
	@echo $(RELEASE_LIB)

print_debug_link:
	@echo $(DEBUG_LINK)

print_release_link:
	@echo $(RELEASE_LINK)

print_debug_path:
	@echo $(DEBUG_BUILD_DIR)

print_release_path:
	@echo $(RELEASE_BUILD_DIR)

clean:
	make -C test clean
	rm -f *~ $(SRC_DIR)/*~ $(INCLUDE_DIR)/*~ rpm/*~
	rm -fr $(BUILD_DIR) RPMS installroot
	rm -fr debian/tmp debian/libgrilio debian/libgrilio-dev
	rm -f documentation.list debian/files debian/*.substvars
	rm -f debian/*.debhelper.log debian/*.debhelper debian/*~

test:
	make -C test test

$(DEBUG_BUILD_DIR):
	mkdir -p $@

$(RELEASE_BUILD_DIR):
	mkdir -p $@

$(DEBUG_BUILD_DIR)/%.o : $(SRC_DIR)/%.c
	$(CC) -c $(DEBUG_CFLAGS) -MT"$@" -MF"$(@:%.o=%.d)" $< -o $@

$(RELEASE_BUILD_DIR)/%.o : $(SRC_DIR)/%.c
	$(CC) -c $(RELEASE_CFLAGS) -MT"$@" -MF"$(@:%.o=%.d)" $< -o $@

$(DEBUG_LIB): $(DEBUG_OBJS)
	$(LD) $(DEBUG_OBJS) $(DEBUG_LDFLAGS) -o $@

$(RELEASE_LIB): $(RELEASE_OBJS)
	$(LD) $(RELEASE_OBJS) $(RELEASE_LDFLAGS) -o $@
ifeq ($(KEEP_SYMBOLS),0)
	strip $@
endif

$(DEBUG_LINK):
	ln -sf $(LIB) $@

$(RELEASE_LINK):
	ln -sf $(LIB) $@

$(PKGCONFIG): $(LIB_NAME).pc.in
	sed -e 's/\[version\]/'$(PCVERSION)/g $< > $@

#
# Install
#

INSTALL = install
INSTALL_DIRS = $(INSTALL) -d
INSTALL_LIBS = $(INSTALL) -m 755
INSTALL_FILES = $(INSTALL) -m 644

INSTALL_LIB_DIR = $(DESTDIR)/usr/lib
INSTALL_INCLUDE_DIR = $(DESTDIR)/usr/include/$(NAME)
INSTALL_PKGCONFIG_DIR = $(DESTDIR)/usr/lib/pkgconfig

install: $(INSTALL_LIB_DIR)
	$(INSTALL_LIBS) $(RELEASE_LIB) $(INSTALL_LIB_DIR)
	ln -sf $(LIB) $(INSTALL_LIB_DIR)/$(LIB_SYMLINK2)
	ln -sf $(LIB_SYMLINK2) $(INSTALL_LIB_DIR)/$(LIB_SYMLINK1)

install-dev: install $(INSTALL_INCLUDE_DIR) $(INSTALL_PKGCONFIG_DIR)
	$(INSTALL_FILES) $(INCLUDE_DIR)/*.h $(INSTALL_INCLUDE_DIR)
	$(INSTALL_FILES) $(PKGCONFIG) $(INSTALL_PKGCONFIG_DIR)
	ln -sf $(LIB_SYMLINK1) $(INSTALL_LIB_DIR)/$(LIB_DEV_SYMLINK)

$(INSTALL_LIB_DIR):
	$(INSTALL_DIRS) $@

$(INSTALL_INCLUDE_DIR):
	$(INSTALL_DIRS) $@

$(INSTALL_PKGCONFIG_DIR):
	$(INSTALL_DIRS) $@
