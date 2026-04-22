#!/bin/bash
set -e
docker build --build-arg DOCKER_BASE=ubuntu:20.04 . -t rugby_docker_test
docker run --gpus all -v /tmp/.X11-unix:/tmp/.X11-unix:rw --entrypoint bash -it rugby_docker_test -c 'set -e; for x in `find rugby_core/env -name *_test.py`; do UNITTEST_IN_DOCKER=1 PYTHONPATH=/ python3 $x; done'

docker build --build-arg DOCKER_BASE=ubuntu:18.04 . -t rugby_docker_test -f Dockerfile_examples
docker run -v /tmp/.X11-unix:/tmp/.X11-unix:rw --gpus all --entrypoint python3 -it rugby_docker_test rugby_core/examples/run_ppo2.py --level=academy_empty_goal_close --num_timesteps=10000
echo "Test successful!!!"
