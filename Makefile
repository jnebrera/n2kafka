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

# Update binary version if needed
actual_git_version:=$(shell git describe --abbrev=6 --tags HEAD --always)
ifneq (,$(wildcard version.c))
version_c_version:=$(shell sed -n 's/.*n2kafka_version="\([^"]*\)";/\1/p' -- version.c)
endif

ifneq (,$(filter-out $(actual_git_version),$(GITVERSION) $(version_c_version)))
VERSION_C_PHONY=version.c
endif

version.c:
	sed -i 's%^GITVERSION=.*%GITVERSION=$(actual_git_version)%g' Makefile.config
	@echo "const char *n2kafka_version=\"$(actual_git_version)\";" > $@

install: bin-install

clean: bin-clean
	rm -f $(TESTS) $(TESTS_OBJS) $(TESTS_XML) $(COV_FILES)

#
# Testing
#

COV_FILES = $(foreach ext,gcda gcno, $(SRCS:.c=.$(ext)) $(TESTS_C:.c=.$(ext)))

VALGRIND ?= valgrind
SUPPRESSIONS_FILE ?= tests/valgrind.suppressions
ifneq ($(wildcard $(SUPPRESSIONS_FILE)),)
SUPPRESSIONS_VALGRIND_ARG = --suppressions=$(SUPPRESSIONS_FILE)
endif

.PHONY: tests checks memchecks drdchecks helchecks coverage check_coverage \
	docker dev-docker .clang_complete $(VERSION_C_PHONY)

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
WRAP_BASH_FN = $(shell gcc-nm -P $(1) | grep -e "^__wrap" | cut -d ' ' -f 1 | \
	sed 's/__wrap_/-Wl,-wrap=/' | xargs)

tests/%.test: WRAP_LDFLAGS := -Wl,-wrap,MHD_get_connection_info
tests/%.test: CPPFLAGS := -I. $(CPPFLAGS)
tests/%.test: tests/%.o $(BIN) $(TEST_COMMON_OBJS_DEPS) $(filter-out src/engine/n2kafka.o,$(OBJS))
	@echo -e '\033[0;33m Building: $@ \033[0m'
	$(CC) $(CPPFLAGS) $(LDFLAGS) \
		$(call WRAP_BASH_FN,$(shell cat $(@:.test=.objdeps))) \
		$< $(shell cat $(@:.test=.objdeps)) \
		-o $@ $(LIBS) -lcurl

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


#
# Docker containers
#

DOCKER_OUTPUT_TAG?=gcr.io/wizzie-registry/n2kafka
DOCKER_OUTPUT_VERSION?=1.99-2

DOCKER_RELEASE_FILES=n2kafka docker/release/config.json.env docker/release/n2k_setup.sh

docker: $(BIN) docker/release/Dockerfile
	docker build -t $(DOCKER_OUTPUT_TAG):$(DOCKER_OUTPUT_VERSION) -f docker/release/Dockerfile .

dev-docker: docker/devel/Dockerfile
	@docker build $(DOCKER_BUILD_PARAMETERS) docker/devel

docker/release/Dockerfile: RELEASEFILES_ARG=--define=releasefiles='$(DOCKER_RELEASE_FILES)'
%/Dockerfile: docker/Dockerfile.m4
	mkdir -p "$(dir $@)"
	m4 $(RELEASEFILES_ARG) --define=version="$(@:docker/%/Dockerfile=%)" "$<" > "$@"


-include $(DEPS)

.clang_complete:
	echo $(CPPFLAGS) $(CFLAGS) > $@
	find src/ -type d | sed -e 's%^%-I%' >> $@
