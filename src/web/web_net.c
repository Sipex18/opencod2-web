#ifdef __EMSCRIPTEN__

#include "web_net.h"
#include "common_types.h"
#include "imports.h"

#include <emscripten.h>
#include <string.h>

extern void Com_Printf(const char *fmt, ...);

static byte s_recvBuf[16384];
static byte s_addrIp[4];
static byte s_addrPort[2];

Bool Web_Net_Send(int length, const void *data, netadr_t to)
{
    if (to.type != NA_IP && to.type != NA_BROADCAST)
        return 0;
    if (length <= 0 || length > (int)sizeof(s_recvBuf))
        return 0;

    /*
     * Ensure the relay WS is connected to this target, then send.
     * cod2NetConnect is a no-op if already connected to the same endpoint.
     * cod2NetSend copies the buffer and sends as a binary WS frame.
     *
     * MAIN_THREAD_EM_ASM: these calls must run on the browser thread
     * because WebSocket lives there (PROXY_TO_PTHREAD builds).
     */
    MAIN_THREAD_EM_ASM({
        if (typeof cod2NetConnect === 'function')
            cod2NetConnect($0, $1, $2, $3, $4);
    }, (int)to.ip[0], (int)to.ip[1], (int)to.ip[2], (int)to.ip[3],
       (int)(unsigned short)to.port);

    int ok = MAIN_THREAD_EM_ASM_INT({
        if (typeof cod2NetSend === 'function')
            return cod2NetSend($0, $1);
        return 0;
    }, (int)(intptr_t)data, length);

    return ok ? 1 : 0;
}

qboolean Web_Net_Recv(netadr_t *net_from, msg_t *net_message)
{
    int len;

    len = MAIN_THREAD_EM_ASM_INT({
        if (typeof cod2NetRecv === 'function')
            return cod2NetRecv($0, $1, $2, $3);
        return 0;
    }, (int)(intptr_t)s_recvBuf, (int)sizeof(s_recvBuf),
       (int)(intptr_t)s_addrIp, (int)(intptr_t)s_addrPort);

    if (len <= 0)
        return 0;

    if (len > net_message->maxsize) {
        Com_Printf("Web_Net_Recv: oversize packet %d\n", len);
        return 0;
    }

    memset(net_from, 0, sizeof(*net_from));
    net_from->type = NA_IP;
    net_from->ip[0] = s_addrIp[0];
    net_from->ip[1] = s_addrIp[1];
    net_from->ip[2] = s_addrIp[2];
    net_from->ip[3] = s_addrIp[3];
    net_from->port = (unsigned short)(s_addrPort[0] | (s_addrPort[1] << 8));

    memcpy(net_message->data, s_recvBuf, len);
    net_message->cursize = len;
    net_message->readcount = 0;

    return 1;
}

#endif /* __EMSCRIPTEN__ */
