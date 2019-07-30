#########################################
# VARIABLES - overridable by make flags #
#########################################
# {{{

# Stuff to set in CFLAGS:
#   -march=native
#       speed! Don't use for cross compilation.
#   -fpie -Wl,-pie
#       don't use along with -fPIE and -shared for shared libraries
CFLAGS         = -I. -Iinclude -Iinc -Isrc -Wall -Wextra \
                 -Wno-implicit-fallthrough -Wno-unused-const-variable \
                 -std=c11 -g3 -Os -D_FORTIFY_SOURCE=2 -fexceptions \
                 -fasynchronous-unwind-tables -fpie -Wl,-pie \
                 -fstack-protector-strong -grecord-gcc-switches \
                 -Werror=format-security \
                 -Werror=implicit-function-declaration -Wl,-z,defs -Wl,-z,now \
                 -Wl,-z,relro $(EXTRA_CFLAGS)
LDFLAGS        = $(EXTRA_LDFLAGS)
LDLIBS         = $(EXTRA_LDLIBS)
DESTDIR        = /
PREFIX         = /usr/local
RAGEL          = ragel
RAGELFLAGS     = -G2 $(EXTRA_RAGELFLAGS)
INSTALL        = install
BEAR           = bear
COMPLEXITY     = complexity
CFLOW          = cflow
NEATO          = neato
CTAGS          = ctags
SCAN_BUILD     = scan-build
Q              = @
CC_COLOR       = \033[0;34m
LD_COLOR       = \033[0;33m
TEST_COLOR     = \033[0;35m
INSTALL_COLOR  = \033[0;32m
NO_COLOR       = \033[m

# }}}


###############
# MAIN TARGET #
###############

default: all

all: tftpd

tftpd: tftpd.o tftpd_tftp_parser.o


#########################
# DEVELOPMENT UTILITIES #
#########################

.PHONY: complexity
complexity:
	$(COMPLEXITY) --scores --threshold=1 src/*.c

.PHONY: ci
ci: | cscope.files
	cat cscope.files | entr sh -c "clear; make -B"

.PHONY: ci-test
ci-test: | cscope.files
	cat cscope.files | entr sh -c "clear; make -B test"

.PHONY: cscope
cscope: | cscope.files
	cscope -b -q -k

.PHONY: compile_commands.json
compile_commands.json:
	$(BEAR) $(MAKE) -B all

.PHONY: tags
tags: | cscope.files
	$(CTAGS) -L cscope.files

.PHONY: scan-build
scan-build:
	$(SCAN_BUILD) $(MAKE) -B all



################
# TEST TARGETS #
################

# The 'test' target is primarily for running a separate test suite, usually
# for unit tests and property based testing. It differs from the 'check'
# target in that it does not necessarily need the compiled target (the
# library or binary that this Makefile builds) - it only needs some
# of the object files. Most users will make a project by naively running
# 'make' in the directory, and then run 'make test' -  but in 'make test',
# we'd like to enable code coverage and other neat stuff using CFLAGS and
# LDLIBS. I've taken the liberty to assume that this Makefile will be used
# in projects where a full recompile isn't a big deal, and we just recompile
# the entire project with the correct compile flags. Then we have the opposite
# problem, that the user might run 'make install' after a 'make test'; that
# won't be *as much* of an issue - at least the target binary will not be
# linked with the '--coverage' flag, and it won't generate gcov files when
# executed.
test: CFLAGS += -I./vendor/munit/ -fprofile-arcs -ftest-coverage
test: LDLIBS += -lgcov --coverage
test: test_driver
	@echo "$(TEST_COLOR)TEST$(NO_COLOR) $@"
	$(Q)./test_driver \
		&& gcov src/*.c src/*.c.rl \
		&& gcovr -r . -e ".*munit.c" -e "tests/test.*.c"


# The 'check' target is primarily for testing *the compiled target*; i.e. if
# you're building a shared library, the 'check' target would compile a binary
# which links to that shared library and runs tests. If you're building a
# binary, then this target would in some useful way execute that file and test
# it's behaviour.
check:
	@echo "No checks available."


test_driver: CFLAGS += -Ivendor/munit/
test_driver: test_driver.o test_ws_parser.o ws_parser.o \
             http_parser.o aat_parser.o munit.o


###################
# INSTALL TARGETS #
###################

install: $(DESTDIR)$(PREFIX)/bin/tftpd



#################
# CLEAN TARGETS #
#################
clean:
	rm -f *.o test_driver *.gcda *.gcno *.gcov *.cflow 

distclean: clean
	rm -f *.so tftpd compile_commands.json cscope.files cscope*.out


########
# DOCS #
########
.PHONY: docs
docs:
	$(MAKE) -C docs $@

.PHONY: latexpdf
latexpdf:
	$(MAKE) -C docs $@


################
# SOURCE PATHS #
################
vpath %.c src/
vpath %.c.rst src/
vpath %.c.md src/
vpath %.c.rl src/
vpath %.c.rl.md src/
vpath %.c.rl.rst src/
vpath %.h include/
vpath %.h inc/
vpath munit.c vendor/munit/
vpath test_%.c tests/


##################
# IMPLICIT RULES #
##################
$(DESTDIR)$(PREFIX)/bin:
	@echo "$(INSTALL_COLOR)INSTALL$(NO_COLOR) $@"
	$(Q)$(INSTALL) -m 0755 -d $@

$(DESTDIR)$(PREFIX)/lib:
	@echo "$(INSTALL_COLOR)INSTALL$(NO_COLOR) $@"
	$(Q)$(INSTALL) -m 0755 -d $@

$(DESTDIR)$(PREFIX)/include:
	@echo "$(INSTALL_COLOR)INSTALL$(NO_COLOR) $@"
	$(Q)$(INSTALL) -m 0755 -d $@

$(DESTDIR)$(PREFIX)/lib/%.so: %.so | $(DESTDIR)$(PREFIX)/lib
	@echo "$(INSTALL_COLOR)INSTALL$(NO_COLOR) $@"
	$(Q)$(INSTALL) -m 0644 $< $@

$(DESTDIR)$(PREFIX)/include/%.h: %.h | $(DESTDIR)$(PREFIX)/include
	@echo "$(INSTALL_COLOR)INSTALL$(NO_COLOR) $@"
	$(Q)$(INSTALL) -m 0644 $< $@

$(DESTDIR)$(PREFIX)/bin/%: % | $(DESTDIR)$(PREFIX)/bin
	@echo "$(INSTALL_COLOR)INSTALL$(NO_COLOR) $@"
	$(Q)$(INSTALL) -m 0755 $< $@

%: %.o
	@echo "$(LD_COLOR)LD$(NO_COLOR) $@"
	$(Q)$(CC) $(LDFLAGS) -o $@ $^ $(LOADLIBES) $(LDLIBS)

%.so: CFLAGS += -fPIC
%.so:
	@echo "$(LD_COLOR)LD$(NO_COLOR) $@"
	$(Q)$(CC) $(LDFLAGS) -shared -o $@ $^ $(LOADLIBES) $(LDLIBS)

%.o: %.c
	@echo "$(CC_COLOR)CC$(NO_COLOR) $@"
	$(Q)$(CC) -c $(CFLAGS) $(CPPFLAGS) -o $@ $^

# for each c file, it's possible to generate a cflow flow graph.
%.c.cflow: %.c
	@echo "$(CC_COLOR)CC$(NO_COLOR) $@"
	$(Q)$(CFLOW) -o $@ $<

%.png: %.dot
	@echo "$(CC_COLOR)CC$(NO_COLOR) $@"
	$(Q)$(NEATO) -Tpng -Ln100 -o $@ $<

%.dot: %.rl
	@echo "$(CC_COLOR)CC$(NO_COLOR) $@"
	$(Q)$(RAGEL) $(RAGELFLAGS) -V -p $< -o $@

%.c: %.c.rl
	@echo "$(CC_COLOR)CC$(NO_COLOR) $@"
	$(Q)$(RAGEL) -Iinclude $(RAGELFLAGS) -o $@ $<

%.c: %.c.rst
	@echo "$(CC_COLOR)CC$(NO_COLOR) $@"
	$(Q)cat $< | rst_tangle > $@

# build c files from markdown files - literate programming style
%.c: %.c.md
	@echo "$(CC_COLOR)CC$(NO_COLOR) $@"
	$(Q)cat $< | sed -n '/^```c/,/^```/ p' | sed '/^```/ d' > $@

#vim: set foldmethod=marker
