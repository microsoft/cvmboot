#!/bin/sh
# cvmboot cmdline hook for dracut

type getarg >/dev/null 2>&1 || . /lib/dracut-lib.sh

echo "CVMBoot: Processing kernel command line"
