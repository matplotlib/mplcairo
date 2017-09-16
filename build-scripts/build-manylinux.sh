#!/bin/bash

docker run -it \
    --mount type=bind,source="$(git rev-parse --show-toplevel)",target=/io/mpl_cairo \
    quay.io/pypa/manylinux1_x86_64 \
    /io/mpl_cairo/build-scripts/build-manylinux-within-docker.sh

user="${SUDO_USER:-$USER}"
chown "$user:$(id -gn "$user")" -R .
