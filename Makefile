BIN=	n2kafka

SRCS=
OBJS = $(SRCS:.c=.o)
TESTS_PY = $(wildcard tests/0*.py)

TEST_REPORTS_DIR ?= tests

TESTS_CHECKS_XML = $(TESTS_PY:tests/%.py=$(TEST_REPORTS_DIR)/%.xml)
TESTS_MEM_XML = $(TESTS_PY:tests/%.py=$(TEST_REPORTS_DIR)/%.mem.xml)
TESTS_HELGRIND_XML = $(TESTS_PY:tests/%.py=$(TEST_REPORTS_DIR)/%.helgrind.xml)
TESTS_DRD_XML = $(TESTS_PY:tests/%.py=$(TEST_REPORTS_DIR)/%.drd.xml)
TESTS_VALGRIND_XML = $(TESTS_MEM_XML)
TESTS_XML = $(TESTS_CHECKS_XML) $(TESTS_VALGRIND_XML)
TESTS_PY_DEPS = $(wildcard tests/[!0]*.py)

all: $(BIN)

CURRENT_N2KAFKA_DIR = $(dir $(lastword $(MAKEFILE_LIST)))
include src/Makefile.mk
include mklove/Makefile.base

#
# Version
#

# Update binary version if needed
actual_git_version:=$(shell git describe --abbrev=6 --tags --dirty --always)

# Modify version in needed files
ifneq ($(actual_git_version),$(GITVERSION))
$(shell sed -i 's/$(GITVERSION)/$(actual_git_version)/' -- config.h Makefile.config)
GITVERSION=$actual_git_version
endif

#
# Binary related targets
#

install: bin-install

clean: bin-clean
	rm -f $(TESTS) $(TESTS_OBJS) $(TESTS_XML) $(COV_FILES)

#
# Testing
#

COV_FILES = $(foreach ext,gcda gcno, $(SRCS:.c=.$(ext)))

VALGRIND ?= valgrind
SUPPRESSIONS_FILE ?= tests/valgrind.suppressions
ifneq ($(wildcard $(SUPPRESSIONS_FILE)),)
SUPPRESSIONS_VALGRIND_ARG = --suppressions=$(SUPPRESSIONS_FILE)
endif

.PHONY: tests checks memchecks drdchecks helchecks coverage check_coverage \
	docker dev-docker .clang_complete

run_tests = tests/run_tests.sh $(1) $(TESTS_CHECKS_XML:.xml=)
run_valgrind = py.test --junitxml="$@" "./$<" -- $(VALGRIND) --tool=$(1) \
	$(SUPPRESSIONS_VALGRIND_ARG) --xml=yes --xml-file=$(2) $(3) >/dev/null 2>&1

tests: $(TESTS_XML)
	@$(call run_tests, -cvdh)

checks: $(TESTS_CHECKS_XML)
	$(call run_tests,-c)

memchecks: $(TESTS_VALGRIND_XML)
	@$(call run_tests,-v)

drdchecks:
	@$(call run_tests,-d)

helchecks:
	@$(call run_tests,-h)

$(TEST_REPORTS_DIR)/%.mem.xml: tests/%.py $(TESTS_PY_DEPS) $(BIN)
	@echo -e '\033[0;33m Checking memory:\033[0m $<'
	@$(call run_valgrind,memcheck,"$@",$(BIN))

$(TEST_REPORTS_DIR)/%.helgrind.xml: tests/%.py $(TESTS_PY_DEPS) $(BIN)
	@echo -e '\033[0;33m Testing concurrency [HELGRIND]:\033[0m $<'
	-@$(call run_valgrind,helgrind,"$@",$(BIN))

$(TEST_REPORTS_DIR)/%.drd.xml: tests/%.py $(TESTS_PY_DEPS) $(BIN)
	@echo -e '\033[0;33m Testing concurrency [DRD]:\033[0m $<'
	-@$(call run_valgrind,drd,"$@",$(BIN))

PYTEST_JOBS ?= 0
ifneq ($(PYTEST_JOBS), 0)
pytest_jobs_arg := -n $(PYTEST_JOBS)
endif

$(TEST_REPORTS_DIR)/%.xml: tests/%.py $(TESTS_PY_DEPS) $(BIN)
	@echo -e '\033[0;33m Testing:\033[0m $<'
	@py.test $(pytest_jobs_arg) --junitxml="$@" "./$<"

#
# COVERAGE
#

check_coverage:
	@( if [[ "x$(WITH_COVERAGE)" == "xn" ]]; then \
	echo -e "$(MKL_RED) You need to configure using --enable-coverage"; \
	echo -n "$(MKL_CLR_RESET)"; \
	false; \
	fi)

COVERAGE_INFO ?= coverage.info
COVERAGE_OUTPUT_DIRECTORY ?= coverage.out.html
COV_GCOV ?= gcov
COV_LCOV ?= lcov

coverage.out: $(COV_FILES) coverage.info
	genhtml --branch-coverage $(COVERAGE_INFO) --output-directory \
				${COVERAGE_OUTPUT_DIRECTORY} > coverage.out

coverage.info: $(COV_FILES)
	$(COV_LCOV) --gcov-tool=$(COV_GCOV) -q \
                --rc lcov_branch_coverage=1 --capture \
                --directory ./src --output-file $(COVERAGE_INFO)

	# Remove unwanted stuff
	$(COV_LCOV) --remove $(COVERAGE_INFO) '*vendor/*' '/usr/include/*' \
		'$(CURDIR)/src/util/tommyds/*' --rc lcov_branch_coverage=1 -o $(COVERAGE_INFO)

# Execution profile.
$(SRCS:.c=.gcda): $(TESTS_CHECKS_XML)

coverage: check_coverage coverage.out


#
# Docker containers
#

DOCKER_OUTPUT_TAG?=gcr.io/wizzie-registry/n2kafka
DOCKER_OUTPUT_VERSION?=1.99-2

docker: $(BIN)
	docker build -t $(DOCKER_OUTPUT_TAG):$(DOCKER_OUTPUT_VERSION) -f docker/Dockerfile

dev-docker:
	@docker build $(DOCKER_BUILD_PARAMETERS) --target n2k-dev -f docker/Dockerfile .

-include $(DEPS)

.clang_complete:
	echo $(CPPFLAGS) $(CFLAGS) > $@
	find src/ -type d | sed -e 's%^%-I%' >> $@
