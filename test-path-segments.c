#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "yaml-path.h"


#define PATH_STRING_LEN 1024

static int
test_result = 0;

static char
yp_s[PATH_STRING_LEN] = {0};


#define ASCII_ERR "\033[0;33m"
#define ASCII_RST "\033[0;0m"

void
yp_test (char *p, int expected_failure)
{
	yaml_path_t *yp = yaml_path_create();
	printf("%s", p);
	if (!yaml_path_parse(yp, p)) {
		yaml_path_snprint(yp, yp_s, PATH_STRING_LEN);
		if (expected_failure) {
			printf(ASCII_ERR);
			test_result++;
		}
		printf(" -> %s: %s\n", yp_s, expected_failure ? ASCII_RST"FAILED" : "OK");
	} else {
		const yaml_path_error_t *ype = yaml_path_error_get(yp);
		if (!expected_failure) {
			printf(ASCII_ERR);
			test_result++;
		}
		printf(" -X %s (at pos: %zu): %s\n", ype->message, ype->pos, !expected_failure ? ASCII_RST"FAILED" : "OK");
	}
	yaml_path_destroy(yp);
}

#define yp_test_good(p)    yp_test(p, 0)
#define yp_test_invalid(p) yp_test(p, 1)


int main (int argc, char *argv[])
{
	yp_test_good(".first");
	yp_test_good(".first[0]");
	yp_test_good(".first.second[0].third");
	yp_test_good(".first.0");
	yp_test_good("$.jsonpath.something");
	yp_test_good("unprefixed.key[0]");
	yp_test_good("$[0]");
	yp_test_good("[0]");
	yp_test_good("0");
	yp_test_good("!");
	yp_test_good("$");

	yp_test_good("[0:0]");
	yp_test_good("[0:0:1]");
	yp_test_good("[100:]");
	yp_test_good("[100::]");
	yp_test_good("[:100]");
	yp_test_good("[:100:]");
	yp_test_good("[:]");
	yp_test_good("[::]");
	yp_test_good("[-03:-200:+500]");

	yp_test_good("&anc");
	yp_test_good("&anc[0]");
	yp_test_good("&anc[0].zzz");

	yp_test_good("el['key']");
	yp_test_good("el['key'].other[0]['key']");

	yp_test_invalid("$.");
	yp_test_invalid("");
	yp_test_invalid(".");
	yp_test_invalid("element[");

	yp_test_invalid("[0:0:0]");
	yp_test_invalid("[::-1]");
	yp_test_invalid("[0.key[0]");

	yp_test_invalid("el[&]");
	yp_test_invalid("el[&");
	yp_test_invalid("el[&wrong.");

	yp_test_invalid("el[&anchor]");
	yp_test_invalid("el[&anchor].key");
	yp_test_invalid("el[&anchor][100]");

	yp_test_invalid("el[']");
	yp_test_invalid("el['key].wrong");
	yp_test_invalid("el['key.wrong");
	yp_test_invalid("el['key'");

	return test_result;
}
