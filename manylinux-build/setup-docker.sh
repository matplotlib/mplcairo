#!/bin/bash

docker run -it \
    --mount type=bind,source="$(dirname "$(readlink -f "$0")")",target=/io \
    quay.io/pypa/manylinux1_x86_64 bash
