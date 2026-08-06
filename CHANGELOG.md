# Changelog

All notable changes to this project will be documented in this file.

The format is based on Keep a Changelog and this project follows a simple date-based release workflow.

Automation helper:
- `powershell -ExecutionPolicy Bypass -File tools/changelog_add.ps1 -Type Fixed -Message "your change"`

## [Unreleased]

### Added
- Ported beam tessellation helpers from donor r_beam.c: R_BeamTess_WidthHeuristic, R_BeamTess_ReadAtlasUV (material atlas frame UV lookup), R_BeamTess_SubmitRibbon, R_BeamTess_FromParams, R_BeamTess_FromCmd_Laser (command decode + width heuristic + BGRA color pack).
- Ported shadow cookie projection pipeline from donor r_shadow_cookie.c: R_BuildShadowCookieProjection (ortho projection from sun dir + AABB), R_PickCastersByDistance (top-24 caster selection by dist/extent score), R_FlushShadowCookieAndAdapt (adaptive distance threshold with sc_blur/sc_shadowInRate/sc_shadowOutRate dvars), R_CullAabbBy5Planes (AABB frustum cull helper), R_GetShadowCookieProjection accessor.
### Changed
- Added automated changelog entry helper script at tools/changelog_add.ps1.
### Deprecated
### Removed

### Fixed
- Replaced unbounded sprintf with snprintf in CL_GlobalServers_f to prevent command[1024] overflow from long server query arguments.
- Replaced unbounded sprintf with snprintf in CL_DrawDemoRecording to prevent buf[1024] overflow from long demo name.
- Replaced unbounded strcpy with I_strncpyz in G_ShaderIndex to prevent shaderName[64] buffer overflow from long shader names.
- Replaced unbounded strcpy with I_strncpyz for key[64], value[64], and modelName[64] in CM_LoadStaticModels entity parsing loops.
- Added explicit null-terminators after strncpy for szHostName and gametype in CG_SetConfigString to prevent non-terminated strings.
- Replaced unbounded vsprintf with vsnprintf in Com_ScriptError and Com_ScriptWarning to prevent string[4096] buffer overflow from long formatted script error messages.
- Replaced unbounded sprintf with snprintf in SND_LoadSoundFile and SND_StartStream to prevent realname[256] buffer overflow from long sound file names.
- Added SOCKS buffer overflow guard in Sys_SendPacket to check length against socksBuf[4096] before memcpy.
- Added buffer overflow guard in NET_OutOfBandData to clamp len to MAX_MSGLEN-4 before copying into LargeLocal string buffer.
- Fixed SVC_Status and SVC_GameCompleteStatus: replaced strcpy with I_strncpyz for infostring and status buffers to prevent overflow from Dvar_InfoString.
- Replaced unbounded strcpy with I_strncpyz in SV_AuthorizeRequest for game buffer overflow prevention.
- Fixed NET_AdrToString to handle all address types (localhost/bad address) instead of returning stale buffer for type 0/unknown.
- Replaced unbounded strcpy with I_strncpyz in Com_Error for szUnlocedMsg copy from com_errorMessage.
- Replaced unbounded strcpy calls with I_strncpyz in FS_GetModList for descPath and listbuf buffers.
- Replaced unbounded strcpy with I_strncpyz in FS_iwIwd to prevent buffer overflow of szFile[64].
- Fixed off-by-4 overflow in Netchan_Process fragment reassembly: fragmentLength+4 now checked against msg->maxsize before writing sequence header.
- Added buffer overflow guard in NET_OutOfBandVoiceData to clamp len to 0x8000 before memcpy into stack buffer.
- Added loopback buffer overflow guard in NET_SendPacket to clamp length to loopmsg_t.data size (1400 bytes).
- Added null guard in SV_Netchan_AddOOBProfilePacket after profile preparation.
- Hardened CL_Netchan_SendOOBPacket with null payload guard and safer OOB profile pointer handling.
- Corrected g_main_mp dvar string usage for g_log/g_password and fixed logfile warning format argument.
- Added null guards for client OOB profiling pointers in CL_Netchan_AddOOBProfilePacket and CL_Netchan_SendOOBPacket.
- Hardened CL_Netchan_SendOOBPacket against short packets before reading OOB marker.
- Fixed CL_Netchan_PrintProfileStats UI output to avoid drawing a stale duplicate line before Total and aligned Total row rendering.
- Fixed varargs type mismatches in src/PC/server_mp/sv_net_chan_mp.c by passing int values to %i format slots in Com_sprintf network profile lines.
- Aligned NetProf_PrepProfiling declaration/cast in src/PC/server_mp/sv_net_chan_mp.c to use netProfileInfo_t**.
- `src/PC/qcommon/net_chan_mp.c`: fixed incorrect `Com_Printf` format specifier in `NetProf_PrepProfiling` (`%s` -> `%i`) for `g_qport`.

### Security

## [2026-06-28]

### Added
- Started maintaining a project changelog.
