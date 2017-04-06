#!/bin/bash

set -e

SOURCE_DIR=$1
BINARY_DIR=$2
EXTRA_SETUP_SCRIPT=$3

# These directories have the Bazel meaning described at
# https://bazel.build/versions/master/docs/test-encyclopedia.html. In particular, TEST_SRCDIR is
# where we expect to find the generated outputs of various scripts preparing input data (these are
# not only the actual source files!).
# It is a precondition that both $TEST_TMPDIR and $TEST_SRCDIR are empty.
if [ -z "$TEST_TMPDIR" ] || [ -z "$TEST_SRCDIR" ]
then
  TEST_BASE=/tmp/envoy_test
  echo "Cleaning $TEST_BASE"
  rm -rf $TEST_BASE
fi
: ${TEST_TMPDIR:=$TEST_BASE/tmp}
: ${TEST_SRCDIR:=$TEST_BASE/runfiles}
export TEST_TMPDIR TEST_SRCDIR
export TEST_WORKSPACE=""
export TEST_UDSDIR="$TEST_TMPDIR"

echo "TEST_TMPDIR=$TEST_TMPDIR"
echo "TEST_SRCDIR=$TEST_SRCDIR"

mkdir -p $TEST_TMPDIR

$SOURCE_DIR/test/certs/gen_test_certs.sh $TEST_SRCDIR/test/certs

# Some hacks for the config file template substitution. These go away in the Bazel build.
CONFIG_IN_DIR="$SOURCE_DIR"/test/config/integration
CONFIG_RUNFILES_DIR="$TEST_SRCDIR/$TEST_WORKSPACE"/test/config/integration
CONFIG_OUT_DIR="$TEST_TMPDIR"/test/config/integration
mkdir -p "$CONFIG_RUNFILES_DIR"
mkdir -p "$CONFIG_OUT_DIR"
cp "$CONFIG_IN_DIR"/*.json "$CONFIG_RUNFILES_DIR"
for f in $(cd "$SOURCE_DIR"; find test/config/integration -name "*.json")
do
  "$SOURCE_DIR"/test/test_common/environment_sub.sh "$f"
done

# Some hacks for the runtime test filesystem. These go away in the Bazel build.
TEST_RUNTIME_DIR="$TEST_SRCDIR/$TEST_WORKSPACE"/test/common/runtime/test_data
mkdir -p "$TEST_RUNTIME_DIR"
cp -r "$SOURCE_DIR"/test/common/runtime/test_data/* "$TEST_RUNTIME_DIR"
"$SOURCE_DIR"/test/common/runtime/filesystem_setup.sh

if [ -n "$EXTRA_SETUP_SCRIPT" ]; then
  $EXTRA_SETUP_SCRIPT
fi

# First run the normal unit test suite
cd $SOURCE_DIR
$RUN_TEST_UNDER $BINARY_DIR/test/envoy-test $EXTRA_TEST_ARGS

if [ "$UNIT_TEST_ONLY" = "1" ]
then
  exit 0
fi

# TODO(htuch): Clean this up when Bazelifying the hot restart test below. At the same time, restore
# some test behavior lost in #650, when we switched to 0 port binding - the hot restart tests no
# longer check socket passing. Will need to generate the second server's JSON based on the actual
# bound ports in the first server.
HOT_RESTART_JSON="$TEST_SRCDIR"/test/config/integration/hot_restart.json
cat "$TEST_TMPDIR"/test/config/integration/server.json |
  sed -e "s#{{ upstream_. }}#0#g" | \
  cat > "$HOT_RESTART_JSON"

# Now start the real server, hot restart it twice, and shut it all down as a basic hot restart
# sanity test.
echo "Starting epoch 0"
$BINARY_DIR/source/exe/envoy -c "$HOT_RESTART_JSON" \
    --restart-epoch 0 --base-id 1 --service-cluster cluster --service-node node &

FIRST_SERVER_PID=$!
sleep 3
# Send SIGUSR1 signal to the first server, this should not kill it. Also send SIGHUP which should
# get eaten.
echo "Sending SIGUSR1/SIGHUP to first server"
kill -SIGUSR1 $FIRST_SERVER_PID
kill -SIGHUP $FIRST_SERVER_PID
sleep 3

echo "Starting epoch 1"
$BINARY_DIR/source/exe/envoy -c "$HOT_RESTART_JSON" \
    --restart-epoch 1 --base-id 1 --service-cluster cluster --service-node node &

SECOND_SERVER_PID=$!
# Wait for stat flushing
sleep 7

echo "Starting epoch 2"
$BINARY_DIR/source/exe/envoy -c "$HOT_RESTART_JSON" \
    --restart-epoch 2 --base-id 1 --service-cluster cluster --service-node node &

THIRD_SERVER_PID=$!
sleep 3

# First server should already be gone.
echo "Waiting for epoch 0"
wait $FIRST_SERVER_PID
[[ $? == 0 ]]

#Send SIGUSR1 signal to the second server, this should not kill it
echo "Sending SIGUSR1 to the second server"
kill -SIGUSR1 $SECOND_SERVER_PID
sleep 3

# Now term the last server, and the other one should exit also.
echo "Killing and waiting for epoch 2"
kill $THIRD_SERVER_PID
wait $THIRD_SERVER_PID
[[ $? == 0 ]]

echo "Waiting for epoch 1"
wait $SECOND_SERVER_PID
[[ $? == 0 ]]

