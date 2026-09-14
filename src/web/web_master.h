#ifndef COD2_WEB_MASTER_H
#define COD2_WEB_MASTER_H

#ifdef __EMSCRIPTEN__

/*
 * Browser master discovery (no UDP).
 * JS fetches a same-origin HTTP proxy JSON masterlist and feeds LAN_* via
 * globalServers / localServers so Join Game feeder 2 + gametype work.
 */
void CL_WebMaster_Request(int source, const char *protocol);

/* Called from web_master.js (EMSCRIPTEN_KEEPALIVE). */
void CL_WebMaster_Begin(int source);
void CL_WebMaster_AddServer(int source, const char *ip, int hostPort,
                            const char *hostname, const char *mapname,
                            const char *gametype, const char *game,
                            int clients, int maxClients, int pswrd, int pure,
                            int ping, int hardware, int mod, const char *players);
void CL_WebMaster_UpdatePing(int source, int ipPacked, int hostPort, int ping);
void CL_WebMaster_End(int source, int ok);

/* getstatus-style roster ("score ping \"name\"", backslash separated) for the
 * server at (source, index), or "" when the snapshot had none. */
const char *CL_WebMaster_GetPlayersForServer(int source, int index);

#endif /* __EMSCRIPTEN__ */

#endif /* COD2_WEB_MASTER_H */
