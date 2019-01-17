#!/usr/bin/env python3

#
# Copyright (C) 2014-2016, Eneo Tecnologia S.L.
# Copyright (C) 2017, Eugenio Perez <eupm90@gmail.com>
# Copyright (C) 2018-2019, Wizzie S.L.
# Author: Eugenio Perez <eupm90@gmail.com>
#
# This file is part of n2kafka.
#
# This program is free software; you can redistribute it and/or modify
# it under the terms of the GNU Affero General Public License as
# published by the Free Software Foundation, either version 3 of the
# License, or (at your option) any later version.
#
# This program is distributed in the hope that it will be useful,
# but WITHOUT ANY WARRANTY; without even the implied warranty of
# MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
# GNU Affero General Public License for more details.
#
# You should have received a copy of the GNU General Public License
# along with this program.  If not, see <http://www.gnu.org/licenses/>.
#

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
TLS_DEPS =  tests/key.pem tests/key.encrypted.pem tests/certificate.pem \
                         tests/client-key-1.pem tests/client-certificate-1.pem \
                         tests/client-key-2.pem tests/client-certificate-2.pem
TESTS_PY_DEPS = $(wildcard tests/[!0]*.py) $(TLS_DEPS)

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

PYTEST ?= py.test
PYTEST_JOBS ?= 0
ifneq ($(PYTEST_JOBS), 0)
pytest_jobs_arg := -n $(PYTEST_JOBS)
endif

.PHONY: tests checks memchecks drdchecks helchecks coverage check_coverage \
	docker dev-docker gdb-docker valgrind-docker .clang_complete

show_test_result = tests/show_tests.py $(1) $(TESTS_CHECKS_XML:.xml=)

# Macro for run valgrind.
# Arguments:
#  - Valgrind arguments
#  - Output XML file
#  - Python test file
run_valgrind = echo "$(MKL_YELLOW) Generating $(2) $(MKL_CLR_RESET)" && \
			$(PYTEST) \
				$(pytest_jobs_arg) \
				--child='valgrind \
						 $(1) \
			             $(SUPPRESSIONS_VALGRIND_ARG) \
			             --gen-suppressions=all \
			             --child-silent-after-fork=yes \
			             --xml=yes \
			             --xml-file=$(2)' \
			    $(3)

tests: $(TESTS_XML)
	@$(call show_test_result, -cmde)

checks: $(TESTS_CHECKS_XML)
	$(call show_test_result,-c)

memchecks: $(TESTS_MEM_XML)
	@$(call show_test_result,-m)

drdchecks:
	@$(call show_test_result,-d)

helchecks:
	@$(call show_test_result,-e)

$(TEST_REPORTS_DIR)/%.mem.xml: tests/%.py $(TESTS_PY_DEPS) $(BIN)
	@echo -e '\033[0;33m Checking memory:\033[0m $<'
	$(call run_valgrind,$(strip --tool=memcheck --show-leak-kinds=all \
		                                         --track-origins=yes),$@,"./$<")

$(TEST_REPORTS_DIR)/%.helgrind.xml: tests/%.py $(TESTS_PY_DEPS) $(BIN)
	@echo -e '\033[0;33m Testing concurrency [HELGRIND]:\033[0m $<'
	-@$(call run_valgrind,helgrind,"$@",$(BIN))

$(TEST_REPORTS_DIR)/%.drd.xml: tests/%.py $(TESTS_PY_DEPS) $(BIN)
	@echo -e '\033[0;33m Testing concurrency [DRD]:\033[0m $<'
	-@$(call run_valgrind,drd,"$@",$(BIN))

$(TEST_REPORTS_DIR)/%.xml: tests/%.py $(TESTS_PY_DEPS) $(BIN)
	@echo -e '\033[0;33m Testing:\033[0m $<'
	@$(PYTEST) $(pytest_jobs_arg) --junitxml="$@" "./$<"

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

# Generate the pair key + self signed certificate
# Arguments:
#  1 - output key name
#  2 - output certificate name
openssl_gen = $(strip openssl req \
    -newkey rsa:2048 -nodes -keyout "$(1)" \
            -x509 -days 3650 -subj '/CN=localhost/' -extensions SAN \
            -config <(cat /etc/ssl/openssl.cnf \
                        <(printf '\n[ SAN ]\nsubjectAltName=DNS:localhost\n')) \
            -out "$(2)"; \
       chmod 600 "$(1)" "$(2)")

tests/key.pem tests/certificate.pem: SHELL=/bin/bash
tests/key.pem tests/certificate.pem:
	$(call openssl_gen,tests/key.pem,tests/certificate.pem)

tests/key.encrypted.pem: tests/key.pem
	openssl rsa -aes256 -passout 'pass:1234' -in "$<" -out "$@"
	chmod 600 tests/key.encrypted.pem

tests/client-key-%.pem tests/client-certificate-%.pem: SHELL=/bin/bash
tests/client-key-%.pem tests/client-certificate-%.pem:
	# We only see one of the two targets, so we need to make tricks to generate
	# key/cert name
	$(call openssl_gen,tests/client-key-$(lastword $(subst -, ,$@)),tests/client-certificate-$(lastword $(subst -, ,$@)))


#
# Docker containers
#

DOCKER_OUTPUT_TAG?=gcr.io/wizzie-registry/n2kafka
DOCKER_OUTPUT_VERSION?=latest

DOCKER?=docker

docker_build_cb = $(strip $(DOCKER) build \
	-t $(DOCKER_OUTPUT_TAG):$(DOCKER_OUTPUT_VERSION) --target $(1) \
	$(3) -f $(2) .)

docker: $(BIN)
	$(call docker_build_cb,release,docker/Dockerfile)

gdb-docker: $(BIN)
	$(call docker_build_cb,bin_wrapper,docker/Dockerfile, \
		--build-arg INSTALL_PKGS=cgdb --build-arg EXEC_WRAPPER='cgdb --args')

valgrind-docker: $(BIN)
	$(call docker_build_cb,bin_wrapper,docker/Dockerfile,\
		--build-arg INSTALL_PKGS=valgrind --build-arg EXEC_WRAPPER=valgrind)

dev-docker:
	$(call docker_build_cb,n2k-dev,docker/Dockerfile)

ubuntu-dev-docker:
	$(call docker_build_cb,n2k-ubuntu-dev,docker/ubuntu.Dockerfile)


-include $(DEPS)

.clang_complete:
	echo $(CPPFLAGS) $(CFLAGS) > $@
	find src/ -type d | sed -e 's%^%-I%' >> $@
