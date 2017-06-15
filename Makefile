BIN=	n2kafka

SRCS=   version.c
OBJS = $(SRCS:.c=.o)
TESTS_C = $(wildcard tests/0*.c)

TESTS = $(TESTS_C:.c=.test)
TESTS_OBJS = $(TESTS:.test=.o)
# TODO infividualize parsing objdeps!
TEST_COMMON_OBJS_DEPS = tests/assertion_handler.o tests/n2k_kafka_tests.o \
	tests/rb_json_tests.o tests/zz_http2k_tests.o

TEST_REPORTS_DIR ?= tests/

TESTS_CHECKS_XML = $(TESTS_C:tests/%.c=$(TEST_REPORTS_DIR)/%.xml)
TESTS_MEM_XML = $(TESTS_C:tests/%.c=$(TEST_REPORTS_DIR)/%.mem.xml)
TESTS_HELGRIND_XML = $(TESTS_C:tests/%.c=$(TEST_REPORTS_DIR)/%.helgrind.xml)
TESTS_DRD_XML = $(TESTS_C:tests/%.c=$(TEST_REPORTS_DIR)/%.drd.xml)
TESTS_VALGRIND_XML = $(TESTS_MEM_XML)
TESTS_XML = $(TESTS_CHECKS_XML) $(TESTS_VALGRIND_XML)

all: $(BIN)

CURRENT_N2KAFKA_DIR = $(dir $(lastword $(MAKEFILE_LIST)))

include src/Makefile.mk
include mklove/Makefile.base

.PHONY: version.c tests coverage .clang_complete

version.c:
	@rm -f $@
	@echo "const char *n2kafka_revision=\"`git describe --abbrev=6 --dirty --tags --always`\";" >> $@
	@echo 'const char *n2kafka_version="1.0.0";' >> $@

install: bin-install

clean: bin-clean
	rm -f $(TESTS) $(TESTS_OBJS) $(TESTS_XML) $(COV_FILES)

COV_FILES = $(foreach ext,gcda gcno, $(SRCS:.c=.$(ext)) $(TESTS_C:.c=.$(ext)))

VALGRIND ?= valgrind
SUPPRESSIONS_FILE ?= tests/valgrind.suppressions
ifneq ($(wildcard $(SUPPRESSIONS_FILE)),)
SUPPRESSIONS_VALGRIND_ARG = --suppressions=$(SUPPRESSIONS_FILE)
endif

.PHONY: tests checks memchecks drdchecks helchecks coverage check_coverage

run_tests = tests/run_tests.sh $(1) $(TESTS_C:.c=)
run_valgrind = $(VALGRIND) --tool=$(1) $(SUPPRESSIONS_VALGRIND_ARG) --xml=yes \
					--xml-file=$(2) $(3) >/dev/null 2>&1

tests: $(TESTS_XML)
	@$(call run_tests, -cvdh)

checks: $(TESTS_CHECKS_XML)
	@$(call run_tests,-c)

memchecks: $(TESTS_VALGRIND_XML)
	@$(call run_tests,-v)

drdchecks:
	@$(call run_tests,-d)

helchecks:
	@$(call run_tests,-h)

$(TEST_REPORTS_DIR)/%.mem.xml: tests/%.test $(BIN)
	@echo -e '\033[0;33m Checking memory:\033[0m $<'
	-@$(call run_valgrind,memcheck,"$@","./$<")

$(TEST_REPORTS_DIR)/%.helgrind.xml: tests/%.test $(BIN)
	@echo -e '\033[0;33m Testing concurrency [HELGRIND]:\033[0m $<'
	-@$(call run_valgrind,helgrind,"$@","./$<")

$(TEST_REPORTS_DIR)/%.drd.xml: tests/%.test $(BIN)
	@echo -e '\033[0;33m Testing concurrency [DRD]:\033[0m $<'
	-@$(call run_valgrind,drd,"$@","./$<")

$(TEST_REPORTS_DIR)/%.xml: tests/%.test $(BIN)
	@echo -e '\033[0;33m Testing:\033[0m $<'
	@CMOCKA_XML_FILE="$@" CMOCKA_MESSAGE_OUTPUT=XML "./$<" >/dev/null 2>&1

#Transforms __wrap_fn in -Wl,-wrap,fn
WRAP_BASH_FN = $(shell nm -P $(1) | grep -e "^__wrap" | cut -d ' ' -f 1 | \
	sed 's/__wrap_/-Wl,-wrap=/' | xargs)

tests/%.test: WRAP_LDFLAGS := -Wl,-wrap,MHD_get_connection_info
tests/%.test: CPPFLAGS := -I. $(CPPFLAGS)
tests/%.test: tests/%.o $(TEST_COMMON_OBJS_DEPS) $(filter-out src/engine/n2kafka.o,$(OBJS))
	@echo -e '\033[0;33m Building: $@ \033[0m'
	$(CC) $(CPPFLAGS) $(LDFLAGS) \
		$(call WRAP_BASH_FN,$(shell cat $(@:.test=.objdeps))) \
		$< $(shell cat $(@:.test=.objdeps)) -o $@ $(LIBS) -lcurl

check_coverage:
	@( if [[ "x$(WITH_COVERAGE)" == "xn" ]]; then \
	echo -e "$(MKL_RED) You need to configure using --enable-coverage"; \
	echo -n "$(MKL_CLR_RESET)"; \
	false; \
	fi)

COVERAGE_INFO ?= coverage.info
COVERAGE_OUTPUT_DIRECTORY ?= coverage.out.html
COV_VALGRIND ?= valgrind
COV_GCOV ?= gcov
COV_LCOV ?= lcov

coverage: check_coverage $(TESTS)
	( for test in $(TESTS); do ./$$test; done )
	$(COV_LCOV) --gcov-tool=$(COV_GCOV) -q \
                --rc lcov_branch_coverage=1 --capture \
                --directory ./src --output-file ${COVERAGE_INFO}

	# Remove unwanted stuff
	$(COV_LCOV) --remove ${COVERAGE_INFO} '*vendor/*' '/usr/include/*' \
		'$(CURDIR)/src/util/tommyds/*' --rc lcov_branch_coverage=1 -o ${COVERAGE_INFO}

	genhtml --branch-coverage ${COVERAGE_INFO} --output-directory \
				${COVERAGE_OUTPUT_DIRECTORY} > coverage.out

dev-docker:
	@docker build $(DOCKER_BUILD_PARAMETERS) docker/devel

-include $(DEPS)

.clang_complete:
	echo $(CPPFLAGS) $(CFLAGS) > $@
	find src/ -type d | sed -e 's%^%-I%' >> $@
