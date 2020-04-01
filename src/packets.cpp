#include <string.h>
#include <stdlib.h>
#include <stdio.h>

#include "sctrltp/sctrltp_defines.h"
#include "sctrltp/packets.h"

namespace sctrltp {

void parse_mac (char *in, __u8 *out) {
	/*Parsing in in format XX:XX:XX:XX:XX:XX to format XXXXXX*/

	char * pch;
	__u8 i;

	pch = strtok (in, ":");
	for (i = 0; pch != NULL; i++) {
		out[i] = strtol(pch, NULL, 16);
		pch = strtok (NULL, ":");
	}
}

void print_mac (const char *prefix, __u8 *mac) {
	printf("%s: %02x:%02x:%02x:%02x:%02x:%02x\n", prefix,
	       (int) ((unsigned char *) mac)[0],
	       (int) ((unsigned char *) mac)[1],
	       (int) ((unsigned char *) mac)[2],
	       (int) ((unsigned char *) mac)[3],
	       (int) ((unsigned char *) mac)[4],
	       (int) ((unsigned char *) mac)[5]);
}

} // namespace sctrltp
