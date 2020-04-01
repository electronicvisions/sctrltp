#pragma once
#include <net/if.h>

namespace sctrltp {

void print_core3 (void);
void print_core2 (void);
void print_core  (void);
void exit_handler (void);
void termination_handler (int signum);

#define MAX_IFS 64
int  getInterface   (const char *ifName, struct ifreq *ifHW);
void printInterfaces(void);

} // namespace sctrltp
