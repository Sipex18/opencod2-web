#ifdef __EMSCRIPTEN__

#include "web_master.h"

#include "common_types.h"
#include "imports.h"

#include <emscripten.h>
#include <stdio.h>
#include <string.h>
#include <stdlib.h>

extern void *imp_cls;
extern void Com_Printf(const char *fmt, ...);
extern void Com_Memset(void *dest, int val, int count);
extern void I_strncpyz(char *dest, const char *src, int destsize);
extern void CL_SortGlobalServers(void);
extern void CL_SetServerInfo(serverInfo_t *server, const char *info, int ping);
extern void Info_SetValueForKey(char *s, const char *key, const char *value);
extern const char *va(const char *format, ...);

/* UI rebuild after async master fill (declared in ui_main). */
extern void UI_WebMaster_NotifyServersReady(int source);

static int s_webMasterBusy;

/*
 * Player rosters from the master snapshot, keyed by server address. The UI
 * rebuilds and re-sorts the server list on every refresh, so an index into it
 * would go stale; the address does not. Slots are recycled round-robin.
 */
#define WEB_MASTER_PLAYER_SLOTS 128
#define WEB_MASTER_PLAYER_BYTES 768

static byte s_playerKey[WEB_MASTER_PLAYER_SLOTS][6]; /* ip[4] + net-order port */
static byte s_playerUsed[WEB_MASTER_PLAYER_SLOTS];
static char s_playerText[WEB_MASTER_PLAYER_SLOTS][WEB_MASTER_PLAYER_BYTES];
static int s_playerNext;

static int CL_WebMaster_PlayerSlot(const byte ip[4], unsigned short netPort, int create)
{
    int i;
    int slot = -1;

    for (i = 0; i < WEB_MASTER_PLAYER_SLOTS; i++) {
        if (!s_playerUsed[i])
            continue;
        if (s_playerKey[i][0] == ip[0] && s_playerKey[i][1] == ip[1] &&
            s_playerKey[i][2] == ip[2] && s_playerKey[i][3] == ip[3] &&
            s_playerKey[i][4] == (byte)(netPort & 0xff) &&
            s_playerKey[i][5] == (byte)(netPort >> 8))
            return i;
    }

    if (!create)
        return -1;

    slot = s_playerNext;
    s_playerNext = (s_playerNext + 1) % WEB_MASTER_PLAYER_SLOTS;
    s_playerUsed[slot] = 1;
    s_playerKey[slot][0] = ip[0];
    s_playerKey[slot][1] = ip[1];
    s_playerKey[slot][2] = ip[2];
    s_playerKey[slot][3] = ip[3];
    s_playerKey[slot][4] = (byte)(netPort & 0xff);
    s_playerKey[slot][5] = (byte)(netPort >> 8);
    s_playerText[slot][0] = '\0';
    return slot;
}

/*
 * The Server Info popup needs the roster of the server the player selected.
 * It has a display index, not an address, so resolve it here.
 */
const char *CL_WebMaster_GetPlayersForServer(int source, int index)
{
    clientStatic_t *cls = (clientStatic_t *)imp_cls;
    serverInfo_t *srv;
    int slot;

    if (!cls)
        return "";
    if (source == 1) {
        if (index < 0 || index >= cls->numglobalservers)
            return "";
        srv = &cls->globalServers[index];
    } else if (source == 0) {
        if (index < 0 || index >= cls->numlocalservers)
            return "";
        srv = &cls->localServers[index];
    } else {
        return "";
    }

    slot = CL_WebMaster_PlayerSlot(srv->adr.ip, srv->adr.port, 0);
    return slot < 0 ? "" : s_playerText[slot];
}

static int CL_WebMaster_ParseIPv4(const char *ip, byte out[4])
{
    unsigned a, b, c, d;
    char tail;

    if (!ip || !out)
        return 0;
    if (sscanf(ip, "%u.%u.%u.%u%c", &a, &b, &c, &d, &tail) != 4)
        return 0;
    if (a > 255 || b > 255 || c > 255 || d > 255)
        return 0;
    out[0] = (byte)a;
    out[1] = (byte)b;
    out[2] = (byte)c;
    out[3] = (byte)d;
    return 1;
}

static unsigned short CL_WebMaster_HostPortToNet(int hostPort)
{
    unsigned short p = (unsigned short)(hostPort & 0xffff);
    return (unsigned short)(((p & 0xff) << 8) | ((p >> 8) & 0xff));
}

static const char *CL_WebMaster_VersionForProtocol(const char *protocol)
{
    int p;

    if (!protocol || !protocol[0])
        return "1.3";
    if (protocol[0] == '1' && protocol[1] == '.' && protocol[2])
        return protocol;
    p = atoi(protocol);
    /*
     * UI_StartServerRefresh still sends 0x73 (115). That is 1.2-era; this
     * client is 1.3 (protocol 118). Always use 1.3 masterlist on web.
     */
    (void)p;
    return "1.3";
}

void CL_WebMaster_Request(int source, const char *protocol)
{
    static char urlBuf[256];
    const char *ver;

    if (source != 0 && source != 1)
        return;
    if (s_webMasterBusy) {
        Com_Printf((const char *)"Master proxy request already in progress\n");
        return;
    }

    ver = CL_WebMaster_VersionForProtocol(protocol);
    snprintf(urlBuf, sizeof(urlBuf), "/play/api/masterlist/cod2/%s", ver);

    s_webMasterBusy = 1;
    if (source == 1) {
        clientStatic_t *cls = (clientStatic_t *)imp_cls;
        cls->waitglobalserverresponse = 1;
        cls->pingUpdateSource = 1;
    }

    /*
     * PROXY_TO_PTHREAD: fetch on the browser UI thread. Worker EM_ASM often
     * lacks Module.cod2MasterRequest. urlBuf is static so ASYNC can read it.
     */
    MAIN_THREAD_ASYNC_EM_ASM({
        var ptr = $0;
        var source = $1;
        var url = (typeof UTF8ToString === 'function') ? UTF8ToString(ptr) : '';
        /*
         * PROXY_TO_PTHREAD: Module.cod2MasterRequest often exists only on the
         * worker. Prefer globalThis/window (shell + post-js attach both).
         */
        var fn = null;
        try {
            if (typeof Module !== 'undefined' && typeof Module.cod2MasterRequest === 'function')
                fn = Module.cod2MasterRequest;
        } catch (e0) {}
        if (!fn) {
            try {
                if (typeof globalThis !== 'undefined' && typeof globalThis.cod2MasterRequest === 'function')
                    fn = globalThis.cod2MasterRequest;
            } catch (e1) {}
        }
        if (!fn) {
            try {
                if (typeof window !== 'undefined' && typeof window.cod2MasterRequest === 'function')
                    fn = window.cod2MasterRequest;
            } catch (e2) {}
        }
        try {
            if (!fn) {
                console.error('cod2MasterRequest missing on main thread');
                if (typeof Module !== 'undefined' && typeof Module._CL_WebMaster_End === 'function')
                    Module._CL_WebMaster_End(source, 0);
                return;
            }
            fn(url, source);
        } catch (e) {
            console.error('cod2MasterRequest threw', e);
            try {
                if (typeof Module !== 'undefined' && typeof Module._CL_WebMaster_End === 'function')
                    Module._CL_WebMaster_End(source, 0);
            } catch (e3) {}
        }
    }, urlBuf, source);
}

EMSCRIPTEN_KEEPALIVE
void CL_WebMaster_Begin(int source)
{
    clientStatic_t *cls = (clientStatic_t *)imp_cls;
    int i;

    if (source == 1) {
        cls->numglobalservers = 0;
        cls->waitglobalserverresponse = 1;
        cls->pingUpdateSource = 1;
        return;
    }

    if (source == 0) {
        cls->numlocalservers = 0;
        cls->pingUpdateSource = 0;
        for (i = 0; i < 128; ++i) {
            byte dirty = cls->localServers[i].dirty;
            Com_Memset(&cls->localServers[i], 0, sizeof(cls->localServers[i]));
            cls->localServers[i].dirty = dirty;
        }
    }
}

EMSCRIPTEN_KEEPALIVE
void CL_WebMaster_AddServer(int source, const char *ip, int hostPort,
                            const char *hostname, const char *mapname,
                            const char *gametype, const char *game,
                            int clients, int maxClients, int pswrd, int pure,
                            int ping, int hardware, int mod, const char *players)
{
    clientStatic_t *cls = (clientStatic_t *)imp_cls;
    serverInfo_t *srv;
    netadr_t adr;
    char info[1024];
    int usePing;
    int slot;

    if (!ip || hostPort <= 0 || hostPort > 65535)
        return;

    memset(&adr, 0, sizeof(adr));
    adr.type = NA_IP;
    if (!CL_WebMaster_ParseIPv4(ip, adr.ip))
        return;
    adr.port = CL_WebMaster_HostPortToNet(hostPort);

    if (players && players[0]) {
        slot = CL_WebMaster_PlayerSlot(adr.ip, adr.port, 1);
        if (slot >= 0)
            I_strncpyz(s_playerText[slot], players, WEB_MASTER_PLAYER_BYTES);
    }

    /* Must be > 0 so UI_BuildServerDisplayList does not skip the row. */
    usePing = ping;
    if (usePing <= 0)
        usePing = 999;

    if (source == 1) {
        if (cls->numglobalservers >= 20000)
            return;
        srv = &cls->globalServers[cls->numglobalservers];
        memset(srv, 0, sizeof(*srv));
        srv->adr = adr;
        cls->numglobalservers++;
    } else if (source == 0) {
        if (cls->numlocalservers >= 128)
            return;
        srv = &cls->localServers[cls->numlocalservers];
        memset(srv, 0, sizeof(*srv));
        srv->adr = adr;
        cls->numlocalservers++;
    } else {
        return;
    }

    info[0] = '\0';
    Info_SetValueForKey(info, "hostname", hostname ? hostname : "");
    Info_SetValueForKey(info, "mapname", mapname ? mapname : "");
    Info_SetValueForKey(info, "clients", va("%i", clients));
    Info_SetValueForKey(info, "sv_maxclients", va("%i", maxClients));
    Info_SetValueForKey(info, "gametype", gametype ? gametype : "");
    Info_SetValueForKey(info, "game", game ? game : "");
    Info_SetValueForKey(info, "pswrd", va("%i", pswrd ? 1 : 0));
    Info_SetValueForKey(info, "pure", va("%i", pure ? 1 : 0));
    Info_SetValueForKey(info, "nettype", "1");
    Info_SetValueForKey(info, "hw", va("%i", hardware));
    Info_SetValueForKey(info, "mod", va("%i", mod));
    Info_SetValueForKey(info, "minping", "0");
    Info_SetValueForKey(info, "maxping", "0");
    Info_SetValueForKey(info, "sv_allowAnonymous", "0");
    Info_SetValueForKey(info, "con_disabled", "0");
    Info_SetValueForKey(info, "ff", "0");
    Info_SetValueForKey(info, "kc", "0");
    Info_SetValueForKey(info, "voice", "0");

    CL_SetServerInfo(srv, info, usePing);
    srv->dirty = 1;
    srv->requestCount = 0;
    srv->netType = 1;
}

/*
 * Live latency refresh. A full master response rebuilds the display list, which
 * would reset the selection and scroll position, so the periodic proxy poll
 * lands here instead and only rewrites the ping of an already-listed server.
 * The address is packed into an int to keep this allocation-free: it runs for
 * every server every couple of seconds, on the browser UI thread.
 */
EMSCRIPTEN_KEEPALIVE
void CL_WebMaster_UpdatePing(int source, int ipPacked, int hostPort, int ping)
{
    clientStatic_t *cls = (clientStatic_t *)imp_cls;
    serverInfo_t *list;
    unsigned short netPort;
    byte ip[4];
    int count;
    int i;

    if (hostPort <= 0 || hostPort > 65535)
        return;

    if (source == 1) {
        list = cls->globalServers;
        count = cls->numglobalservers;
    } else if (source == 0) {
        list = cls->localServers;
        count = cls->numlocalservers;
    } else {
        return;
    }

    /* UI_BuildServerDisplayList drops anything with ping <= 0. */
    if (ping <= 0 || ping > 999)
        ping = 999;

    ip[0] = (byte)((ipPacked >> 24) & 0xff);
    ip[1] = (byte)((ipPacked >> 16) & 0xff);
    ip[2] = (byte)((ipPacked >> 8) & 0xff);
    ip[3] = (byte)(ipPacked & 0xff);
    netPort = CL_WebMaster_HostPortToNet(hostPort);

    for (i = 0; i < count; ++i) {
        if (list[i].adr.port != netPort)
            continue;
        if (memcmp(list[i].adr.ip, ip, 4) != 0)
            continue;
        if (list[i].ping != (short)ping) {
            list[i].ping = (short)ping;
            list[i].dirty = 1;
        }
        return;
    }
}

EMSCRIPTEN_KEEPALIVE
void CL_WebMaster_End(int source, int ok)
{
    clientStatic_t *cls = (clientStatic_t *)imp_cls;
    int count;

    s_webMasterBusy = 0;

    if (source == 1) {
        if (ok && cls->numglobalservers > 1)
            CL_SortGlobalServers();
        cls->waitglobalserverresponse = 0;
        count = cls->numglobalservers;
    } else {
        count = cls->numlocalservers;
    }

    if (ok)
        Com_Printf((const char *)"%d servers from HTTP master proxy\n", count);
    else
        Com_Printf((const char *)"HTTP master proxy request failed (check /play/api/)\n");

    /* Force feeder rebuild now — don't wait for the 5s refresh timer. */
    UI_WebMaster_NotifyServersReady(source);
}

#endif /* __EMSCRIPTEN__ */
