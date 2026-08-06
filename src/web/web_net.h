#ifndef COD2_WEB_NET_H
#define COD2_WEB_NET_H

#ifdef __EMSCRIPTEN__

#include "common_types.h"

/*
 * WebSocket ↔ UDP relay bridge for browser builds.
 *
 * Sys_SendPacket and Sys_GetPacket call these when the target/source
 * address type is NA_IP (i.e. non-loopback, non-bot).
 */

/* Ensure the WS relay is connected to ip:port, then send. */
Bool Web_Net_Send(int length, const void *data, netadr_t to);

/* Drain one packet from the JS receive queue into msg. Returns 1 if got one. */
qboolean Web_Net_Recv(netadr_t *net_from, msg_t *net_message);

#endif /* __EMSCRIPTEN__ */

#endif /* COD2_WEB_NET_H */
