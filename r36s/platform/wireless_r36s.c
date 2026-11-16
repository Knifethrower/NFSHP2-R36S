// Local Wireless on the R36S: there is none. wireless.h's contract for a
// console with its wireless switched off -- wireless_start answers 0 and
// says so -- so the lobby shows what the 3DS shows then. A LAN (UDP)
// version could stand behind this same interface later.

#include <stddef.h>
#include <string.h>

#include "wireless.h"
#include "wirelesstest.h"

int wireless_start(WirelessLogFn log, uint32_t nowMs)
{
    (void)nowMs;
    if (log != NULL) log("wireless: none on this device");
    return 0;
}

void wireless_stop(void) {}
int  wireless_radio_off(void) { return 1; }
int  wireless_host(int consoles, const void* appdata, size_t n)
{
    (void)consoles; (void)appdata; (void)n;
    return 0;
}
void wireless_set_open(int open) { (void)open; }
void wireless_set_rejoin(int on, WirelessRejoinFn ok) { (void)on; (void)ok; }
int  wireless_rejoin(void) { return 0; }
void wireless_test_deaf(uint32_t ms) { (void)ms; }
int  wireless_scan(WirelessHost* out, int max) { (void)out; (void)max; return -1; }
int  wireless_join(int scanIndex) { (void)scanIndex; return 0; }
void wireless_leave(void) {}
void wireless_pump(uint32_t nowMs) { (void)nowMs; }
WirelessState wireless_state(void) { return WIRELESS_OFF; }
int  wireless_is_host(void) { return 0; }
int  wireless_peer_count(void) { return 0; }
int  wireless_peer_alive(int i) { (void)i; return 0; }
uint16_t wireless_peer_node(int i) { (void)i; return 0; }
const char* wireless_peer_name(int i) { (void)i; return ""; }
WlLink* wireless_peer_link(int i) { (void)i; return NULL; }
void wireless_drop(int i) { (void)i; }
void wireless_forget_lost(void) {}
void wireless_set_patience(uint32_t ms) { (void)ms; }
void wireless_set_peer_patience(int i, uint32_t ms) { (void)i; (void)ms; }
const char* wireless_self_name(void) { return "R36S"; }
void wireless_stats(WirelessStats* out) { if (out) memset(out, 0, sizeof *out); }

int wirelesstest_run(int mode, int seconds, int consoles, WirelessLogFn log)
{
    (void)mode; (void)seconds; (void)consoles;
    if (log != NULL) log("wirelesstest: no wireless on this device");
    return 0;
}
