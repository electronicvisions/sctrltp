#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <signal.h>
#include <unistd.h>
#include <sys/wait.h>
#include "sctrltp/libhostarq.h"

static bool fail = false;

int check_file(char const* filename, bool exists) {
	char command[256];
	if(snprintf(command, sizeof(command), "test -e /dev/shm/%s", filename) >= (int)sizeof(command)) {
			fprintf(stderr, "filename %s too long\n", filename);
	} else {
		if (system(command) == exists) {
			fprintf(stderr, "%s does %sexist\n", filename, exists ? "not " : "");
			fail = true;
			return 1; // fail
		} else {
			return 0; // ok
		}
	}
	return -1; // WTF
}


int check_pid(pid_t pid, bool exists) {
	waitpid(-1, NULL, WNOHANG);
	int ret = kill(pid, 0);
	if (ret == -1 && !exists)
		return 0;
	if (ret == 0 && exists)
		return 0;
	fprintf(stderr, "PID %d does %sexist: %d %d\n", pid, exists ? "not " : "", ret, exists);
	fail = true;
	return 1;
}


void test_timeout() {
	struct hostarq_handle a, b, c;
	const char a_ip[] = "127.0.0.1";
	const char b_ip[] = "127.0.0.2";
	const char c_ip[] = "127.0.0.3";


	hostarq_create_handle(&a, a_ip, a_ip, 1);
	hostarq_open(&a);
	check_pid(a.pid, true);
	hostarq_create_handle(&b, b_ip, b_ip, 1);
	hostarq_open(&b);
	check_pid(b.pid, true);
	hostarq_create_handle(&c, c_ip, c_ip, 1);
	hostarq_open(&c);
	check_pid(c.pid, true);

	check_file(a_ip, true);
	check_file(b_ip, true);
	check_file(c_ip, true);

	sleep(10);

	check_file(a_ip, false);
	check_file(b_ip, false);
	check_file(c_ip, false);

	check_pid(a.pid, false);
	check_pid(b.pid, false);
	check_pid(c.pid, false);

	hostarq_close(&a);
	hostarq_close(&b);
	hostarq_close(&c);

	hostarq_free_handle(&a);
	hostarq_free_handle(&b);
	hostarq_free_handle(&c);
}



void test_closing() {
	struct hostarq_handle a, b, c;
	const char a_ip[] = "127.0.0.4";
	const char b_ip[] = "127.0.0.5";
	const char c_ip[] = "127.0.0.6";

	hostarq_create_handle(&a, a_ip, a_ip, 0);
	hostarq_create_handle(&b, b_ip, b_ip, 0);
	hostarq_create_handle(&c, c_ip, c_ip, 0);
	hostarq_open(&a);
	hostarq_open(&b);
	hostarq_open(&c);
	sleep(1);
	check_file(a_ip, true);
	check_file(b_ip, true);
	check_file(c_ip, true);

	hostarq_close(&a);
	sleep(1);
	check_file(a_ip, false);
	check_file(b_ip, true);
	check_file(c_ip, true);
	check_pid(a.pid, false);
	check_pid(b.pid, true);
	check_pid(c.pid, true);

	hostarq_close(&b);
	sleep(1);
	check_file(a_ip, false);
	check_file(b_ip, false);
	check_file(c_ip, true);
	check_pid(a.pid, false);
	check_pid(b.pid, false);
	check_pid(c.pid, true);

	hostarq_close(&c);
	sleep(1);
	check_file(a_ip, false);
	check_file(b_ip, false);
	check_file(c_ip, false);
	check_pid(a.pid, false);
	check_pid(b.pid, false);
	check_pid(c.pid, false);

	hostarq_free_handle(&a);
	hostarq_free_handle(&b);
	hostarq_free_handle(&c);
}


int main() {

	fprintf(stdout, "TESTING Timeout Behavior\n");
	test_timeout();
	if (fail)
		return EXIT_FAILURE;
	fprintf(stderr, "\n\n");

	fprintf(stdout, "TESTING Closing Behavior\n");
	test_closing();
	if (fail)
		return EXIT_FAILURE;

	fprintf(stdout, "ALL DONE\n");
	return EXIT_SUCCESS;
}
