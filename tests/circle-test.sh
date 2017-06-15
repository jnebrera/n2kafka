
# Run a command in f2k docker container
# Arguments
#  Docker extra args
#  Others: command to execute
function docker_run () {
        local DOCKER_COMMON_ARGS="-e CC=gcc -v ${PWD}:/app"
        local docker_extra_args="$1"
        shift
        docker run $DOCKER_COMMON_ARGS $docker_extra_args n2k-dev $*
}

# Download a coverage budget
# Arguments
#   Coverage percent
function wget_coverage_budget {
        local readonly cov_d=$(printf '%d' $1) # Coverage integer format
        echo "cov_d=${cov_d}"
        local readonly green=$((cov_d*255/100))
        local readonly red=$((255-green))

        local readonly color="$(printf '%02x' $red)$(printf '%02x' $green)00"

        # Current directory could be created by docker root user
        sudo wget "https://img.shields.io/badge/coverage-$1-$color.svg" \
                -O "coverage.svg"
}

# Run f2k test
# Arguments:
#  [--coverage] Create coverage output
#  $1: results output dir
#  Others: Configure options
function run_f2k_tests () {
        local coverage_arg=""
        if [[ "$1" == "--coverage" ]]; then
                coverage_arg="--enable-coverage"
                shift
        fi

        local readonly output_dir="$1"
        shift

        if [[ ! -z "$coverage_arg" ]]; then
                local readonly make_target=checks # no need to valgrind tests
        else
                local readonly make_target=tests
        fi

        git clean -fqx
        docker_run "" "./configure $coverage_arg $*"
        docker_run "" "make all"
        docker_run "-v $output_dir:$output_dir -e TEST_REPORTS_DIR=$output_dir --link kafka" "make ${make_target}"

        if [[ ! -z "$coverage_arg" ]]; then
                docker_run "--link kafka -v $output_dir:$output_dir -e COVERAGE_OUTPUT_DIRECTORY=\"$output_dir/coverage\"" "make coverage"
                coverage=$(grep -o 'branches\.\.\.: [[:digit:]]*\.[[:digit:]]*' coverage.out | cut -d ' ' -f 2)
                (cd $output_dir; wget_coverage_budget $coverage)
        fi
}

set -e # Return at first error
set -x # Print commands
run_f2k_tests $*


