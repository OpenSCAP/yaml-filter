#!/bin/bash

yamlp_test()
{
	echo -n "$1 ($2) "
	out=$(./yamlp -F -f "$1" "$2") || return 1
	echo -n "-> $out"
	if [ "$out" != "$3" ]; then
		echo ": FAILED, expected result: $3"
		return 2
	else
		echo ": OK"
	fi
}

yamlp_test "../openshift-logging.yaml" ".spec.pipelines[:].inputSource" "[logs.app, logs.infra, logs.audit]"
res=$((res+$?))

exit $res
