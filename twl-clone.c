#include <stdio.h>
#include <getopt.h>
#include <wlr/util/log.h>

int main(int argc, char *argv[]) {
	wlr_log_init(WLR_DEBUG, NULL);
	
	char *startup_cmd = NULL;
	int c;
	while ((c = getopt(argc, argv, "s:h")) != -1) {
		switch (c) {
		case 's':
			startup_cmd = optarg;
			break;
		default:
			printf("Usage: %s [-s startup command]\n", argv[0]);
			return 0;
		}
	}
	if (optind < argc) {
		printf("Usage: %s [-s startup command]\n", argv[0]);
		return 0;
	}

	wlr_log(WLR_INFO, "hello from wlrlog");
	return 0;
}
