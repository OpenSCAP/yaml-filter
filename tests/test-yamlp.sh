#!/bin/bash

# SPDX-License-Identifier: MIT
# Copyright (c) 2020 Red Hat Inc., Durham, North Carolina.

yamlp_test()
{
	echo "$1:"
	echo -n "	($2) "
	out=$("${BINARY_DIR:-../build}/yamlp" -F -f "$1" "$2") || return 1
	echo -n "-> $out"
	if [ "$out" != "$3" ]; then
		echo ": FAILED, expected result: $3"
		return 2
	else
		echo ": OK"
	fi
}

yamlp_test "${SOURCE_DIR:-..}/res/openshift-logging.yaml" ".spec.pipelines[:].inputSource" "[logs.app, logs.infra, logs.audit]"
res=$((res+$?))

yamlp_test "${SOURCE_DIR:-..}/res/openshift-upgradeable.yaml" ".status.conditions[:]['status','type']" \
           '[{status: "False", type: Degraded}, {status: "False", type: Progressing}, {status: "True", type: Available}, {status: "True", type: Upgradeable}]'
res=$((res+$?))

exit $res
