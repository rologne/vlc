/*****************************************************************************
 * standard.c: standard stream output module
 *****************************************************************************
 * Copyright (C) 2003-2011 the VideoLAN team
 * $Id$
 *
 * Authors: Laurent Aimar <fenrir@via.ecp.fr>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston MA 02110-1301, USA.
 *****************************************************************************/

/*****************************************************************************
 * Preamble
 *****************************************************************************/
#ifdef HAVE_CONFIG_H
# include "config.h"
#endif

#include <vlc_common.h>
#include <vlc_plugin.h>
#include <vlc_sout.h>

#include <vlc_network.h>
#include <vlc_url.h>

/*****************************************************************************
 * Module descriptor
 *****************************************************************************/
#define ACCESS_TEXT N_("Output access method")
#define ACCESS_LONGTEXT N_( \
    "Output method to use for the stream." )
#define MUX_TEXT N_("Output muxer")
#define MUX_LONGTEXT N_( \
    "Muxer to use for the stream." )
#define DEST_TEXT N_("Output destination")
#define DEST_LONGTEXT N_( \
    "Destination (URL) to use for the stream. Overrides path and bind parameters" )
#define BIND_TEXT N_("address to bind to (helper setting for dst)")
#define BIND_LONGTEXT N_( \
  "address:port to bind vlc to listening incoming streams "\
  "helper setting for dst,dst=bind+'/'+path. dst-parameter overrides this" )
#define PATH_TEXT N_("filename for stream (helper setting for dst)")
#define PATH_LONGTEXT N_( \
  "Filename for stream "\
  "helper setting for dst, dst=bind+'/'+path, dst-parameter overrides this" )
#define NAME_TEXT N_("Session name")
#define NAME_LONGTEXT N_( \
    "This is the name of the session that will be announced in the SDP " \
    "(Session Descriptor)." )

#define GROUP_TEXT N_("Session groupname")
#define GROUP_LONGTEXT N_( \
  "This allows you to specify a group for the session, that will be announced "\
  "if you choose to use SAP." )

#define DESC_TEXT N_("Session description")
#define DESC_LONGTEXT N_( \
    "This allows you to give a short description with details about the stream, " \
    "that will be announced in the SDP (Session Descriptor)." )
#define URL_TEXT N_("Session URL")
#define URL_LONGTEXT N_( \
    "This allows you to give a URL with more details about the stream " \
    "(often the website of the streaming organization), that will " \
    "be announced in the SDP (Session Descriptor)." )
#define EMAIL_TEXT N_("Session email")
#define EMAIL_LONGTEXT N_( \
    "This allows you to give a contact mail address for the stream, that will " \
    "be announced in the SDP (Session Descriptor)." )
#define PHONE_TEXT N_("Session phone number")
#define PHONE_LONGTEXT N_( \
    "This allows you to give a contact telephone number for the stream, that will " \
    "be announced in the SDP (Session Descriptor)." )


#define SAP_TEXT N_("SAP announcing")
#define SAP_LONGTEXT N_("Announce this session with SAP.")

static int      Open    ( vlc_object_t * );
static void     Close   ( vlc_object_t * );

#define SOUT_CFG_PREFIX "sout-standard-"

vlc_module_begin ()
    set_shortname( N_("Standard"))
    set_description( N_("Standard stream output") )
    set_capability( "sout stream", 50 )
    add_shortcut( "standard", "std", "file", "http", "udp" )
    set_category( CAT_SOUT )
    set_subcategory( SUBCAT_SOUT_STREAM )

    add_string( SOUT_CFG_PREFIX "access", "", ACCESS_TEXT, ACCESS_LONGTEXT, false )
    add_string( SOUT_CFG_PREFIX "mux", "", MUX_TEXT, MUX_LONGTEXT, false )
    add_string( SOUT_CFG_PREFIX "dst", "", DEST_TEXT, DEST_LONGTEXT, false )
    add_string( SOUT_CFG_PREFIX "bind", "", BIND_TEXT, BIND_LONGTEXT, false )
    add_string( SOUT_CFG_PREFIX "path", "", PATH_TEXT, PATH_LONGTEXT, false )
    add_bool(   SOUT_CFG_PREFIX "sap", false, SAP_TEXT, SAP_LONGTEXT, true )
    add_string( SOUT_CFG_PREFIX "name", "", NAME_TEXT, NAME_LONGTEXT, true )
    add_string( SOUT_CFG_PREFIX "group", "", GROUP_TEXT, GROUP_LONGTEXT, true )
    add_string( SOUT_CFG_PREFIX "description", "", DESC_TEXT, DESC_LONGTEXT, true )
    add_string( SOUT_CFG_PREFIX "url", "", URL_TEXT, URL_LONGTEXT, true )
    add_string( SOUT_CFG_PREFIX "email", "", EMAIL_TEXT, EMAIL_LONGTEXT, true )
    add_string( SOUT_CFG_PREFIX "phone", "", PHONE_TEXT, PHONE_LONGTEXT, true )

    set_callbacks( Open, Close )
vlc_module_end ()


/*****************************************************************************
 * Exported prototypes
 *****************************************************************************/
static const char *const ppsz_sout_options[] = {
    "access", "mux", "url", "dst",
    "sap", "name", "group", "description", "url", "email", "phone",
    "bind", "path", NULL
};

#define DEFAULT_PORT 1234

struct sout_stream_sys_t
{
    sout_mux_t           *p_mux;
    session_descriptor_t *p_session;
};

struct sout_stream_id_t
{
};

static sout_stream_id_t * Add( sout_stream_t *p_stream, es_format_t *p_fmt )
{
    return (sout_stream_id_t*)sout_MuxAddStream( p_stream->p_sys->p_mux, p_fmt );
}

static int Del( sout_stream_t *p_stream, sout_stream_id_t *id )
{
    sout_MuxDeleteStream( p_stream->p_sys->p_mux, (sout_input_t*)id );
    return VLC_SUCCESS;
}

static int Send( sout_stream_t *p_stream, sout_stream_id_t *id,
                 block_t *p_buffer )
{
    sout_MuxSendBuffer( p_stream->p_sys->p_mux, (sout_input_t*)id, p_buffer );
    return VLC_SUCCESS;
}
static void create_SDP(sout_stream_t *p_stream, sout_access_out_t *p_access)
{
    sout_stream_sys_t   *p_sys = p_stream->p_sys;

    static const struct addrinfo hints = {
        .ai_family = AF_UNSPEC,
        .ai_socktype = SOCK_DGRAM,
        .ai_protocol = 0,
        .ai_flags = AI_NUMERICHOST | AI_NUMERICSERV
    };
    char *shost = var_GetNonEmptyString (p_access, "src-addr");
    char *dhost = var_GetNonEmptyString (p_access, "dst-addr");
    int sport = var_GetInteger (p_access, "src-port");
    int dport = var_GetInteger (p_access, "dst-port");
    struct sockaddr_storage src, dst;
    socklen_t srclen = 0, dstlen = 0;
    struct addrinfo *res;

    if (!vlc_getaddrinfo ( VLC_OBJECT(p_stream), dhost, dport, &hints, &res))
    {
        memcpy (&dst, res->ai_addr, dstlen = res->ai_addrlen);
        freeaddrinfo (res);
    }

    if (!vlc_getaddrinfo ( VLC_OBJECT(p_stream), shost, sport, &hints, &res))
    {
        memcpy (&src, res->ai_addr, srclen = res->ai_addrlen);
        freeaddrinfo (res);
    }

    char *head = vlc_sdp_Start (VLC_OBJECT (p_stream), SOUT_CFG_PREFIX,
            (struct sockaddr *)&src, srclen,
            (struct sockaddr *)&dst, dstlen);
    free (shost);

    if (head != NULL)
    {
        char *psz_sdp = NULL;
        if (asprintf (&psz_sdp, "%s"
                    "m=video %d udp mpeg\r\n", head, dport) == -1)
            psz_sdp = NULL;
        free (head);

        /* Register the SDP with the SAP thread */
        if (psz_sdp)
        {
            msg_Dbg (p_stream, "Generated SDP:\n%s", psz_sdp);
            p_sys->p_session =
                sout_AnnounceRegisterSDP (p_stream->p_sout, psz_sdp, dhost);
            free( psz_sdp );
        }
    }
    free (dhost);
}

static const char *getMuxFromAlias( const char *psz_alias )
{
    static struct { const char alias[6]; const char mux[32]; } mux_alias[] =
    {
        { "avi", "avi" },
        { "ogg", "ogg" },
        { "ogm", "ogg" },
        { "ogv", "ogg" },
        { "flac","raw" },
        { "mp3", "raw" },
        { "mp4", "mp4" },
        { "mov", "mov" },
        { "moov","mov" },
        { "asf", "asf" },
        { "wma", "asf" },
        { "wmv", "asf" },
        { "trp", "ts" },
        { "ts",  "ts" },
        { "mpg", "ps" },
        { "mpeg","ps" },
        { "ps",  "ps" },
        { "mpeg1","mpeg1" },
        { "wav", "wav" },
        { "flv", "ffmpeg{mux=flv}" },
        { "mkv", "ffmpeg{mux=matroska}"},
        { "webm", "ffmpeg{mux=webm}"},
    };

    if( !psz_alias )
        return NULL;

    for( size_t i = 0; i < sizeof mux_alias / sizeof *mux_alias; i++ )
        if( !strcasecmp( psz_alias, mux_alias[i].alias ) )
            return mux_alias[i].mux;

    return NULL;
}

static int fixAccessMux( sout_stream_t *p_stream, char **ppsz_mux,
                          char **ppsz_access, const char *psz_url )
{
    char *psz_mux = *ppsz_mux;
    char *psz_access = *ppsz_access;
    if( !psz_mux )
    {
        const char *psz_ext = psz_url ? strrchr( psz_url, '.' ) : NULL;
        if( psz_ext )
            psz_ext++; /* use extension */
        const char *psz_mux_byext = getMuxFromAlias( psz_ext );

        if( !psz_access )
        {
            if( !psz_mux_byext )
            {
                msg_Err( p_stream, "no access _and_ no muxer" );
                return 1;
            }

            msg_Warn( p_stream,
                    "no access _and_ no muxer, extension gives file/%s",
                    psz_mux_byext );
            *ppsz_access = strdup("file");
            *ppsz_mux    = strdup(psz_mux_byext);
        }
        else
        {
            if( !strncmp( psz_access, "mmsh", 4 ) )
                *ppsz_mux = strdup("asfh");
            else if (!strcmp (psz_access, "udp"))
                *ppsz_mux = strdup("ts");
            else if( psz_mux_byext )
                *ppsz_mux = strdup(psz_mux_byext);
            else
            {
                msg_Err( p_stream, "no mux specified or found by extension" );
                return 1;
            }
        }
    }
    else if( !psz_access )
    {
        if( !strncmp( psz_mux, "asfh", 4 ) )
            *ppsz_access = strdup("mmsh");
        else /* default file */
            *ppsz_access = strdup("file");
    }
    return 0;
}

static void checkAccessMux( sout_stream_t *p_stream, char *psz_access,
                            char *psz_mux )
{
    if( !strncmp( psz_access, "mmsh", 4 ) && strncmp( psz_mux, "asfh", 4 ) )
        msg_Err( p_stream, "mmsh output is only valid with asfh mux" );
    else if( strncmp( psz_access, "file", 4 ) &&
            ( !strncmp( psz_mux, "mov", 3 ) || !strncmp( psz_mux, "mp4", 3 ) ) )
        msg_Err( p_stream, "mov and mp4 mux are only valid with file output" );
    else if( !strncmp( psz_access, "udp", 3 ) )
    {
        if( !strncmp( psz_mux, "ffmpeg", 6 ) )
        {   /* why would you use ffmpeg's ts muxer ? YOU DON'T LOVE VLC ??? */
            char *psz_ffmpeg_mux = var_CreateGetString( p_stream, "ffmpeg-mux" );
            if( !psz_ffmpeg_mux || strncmp( psz_ffmpeg_mux, "mpegts", 6 ) )
                msg_Err( p_stream, "UDP output is only valid with TS mux" );
            free( psz_ffmpeg_mux );
        }
        else if( strncmp( psz_mux, "ts", 2 ) )
            msg_Err( p_stream, "UDP output is only valid with TS mux" );
    }
}

/*****************************************************************************
 * Open:
 *****************************************************************************/
static int Open( vlc_object_t *p_this )
{
    sout_stream_t       *p_stream = (sout_stream_t*)p_this;
    sout_instance_t     *p_sout = p_stream->p_sout;
    sout_stream_sys_t   *p_sys;
    char *psz_mux, *psz_access, *psz_url;
    sout_access_out_t   *p_access;
    int                 ret = VLC_EGENERIC;

    config_ChainParse( p_stream, SOUT_CFG_PREFIX, ppsz_sout_options,
                   p_stream->p_cfg );

    psz_mux = var_GetNonEmptyString( p_stream, SOUT_CFG_PREFIX "mux" );

    psz_access = var_GetNonEmptyString( p_stream, SOUT_CFG_PREFIX "access" );
    if( !psz_access )
    {
        if( !strcmp( p_stream->psz_name, "http" ) )
            psz_access = strdup("http");
        else if (!strcmp (p_stream->psz_name, "udp"))
            psz_access = strdup("udp");
        else if (!strcmp (p_stream->psz_name, "file"))
            psz_access = strdup("file");
    }

    psz_url = var_GetNonEmptyString( p_stream, SOUT_CFG_PREFIX "dst" );
    if (!psz_url)
    {
        char *psz_bind = var_GetNonEmptyString( p_stream, SOUT_CFG_PREFIX "bind" );
        if( psz_bind )
        {
            char *psz_path = var_GetNonEmptyString( p_stream, SOUT_CFG_PREFIX "path" );
            if( psz_path )
            {
                if( asprintf( &psz_url, "%s/%s", psz_bind, psz_path ) == -1 )
                    psz_url = NULL;
                free(psz_bind);
                free( psz_path );
            }
            else
                psz_url = psz_bind;
        }
    }

    p_sys = p_stream->p_sys = malloc( sizeof( sout_stream_sys_t) );
    if( !p_sys )
    {
        ret = VLC_ENOMEM;
        goto end;
    }
    p_sys->p_session = NULL;

    if( fixAccessMux( p_stream, &psz_mux, &psz_access, psz_url ) )
        goto end;

    checkAccessMux( p_stream, psz_access, psz_mux );

    p_access = sout_AccessOutNew( p_sout, psz_access, psz_url );
    if( p_access == NULL )
    {
        msg_Err( p_stream, "no suitable sout access module for `%s/%s://%s'",
                 psz_access, psz_mux, psz_url );
        goto end;
    }

    p_sys->p_mux = sout_MuxNew( p_sout, psz_mux, p_access );
    if( !p_sys->p_mux )
    {
        const char *psz_mux_guess = getMuxFromAlias( psz_mux );
        if( psz_mux_guess && strcmp( psz_mux_guess, psz_mux ) )
        {
            msg_Dbg( p_stream, "Couldn't open mux `%s', trying `%s' instead",
                psz_mux, psz_mux_guess );
            p_sys->p_mux = sout_MuxNew( p_sout, psz_mux_guess, p_access );
        }

        if( !p_sys->p_mux )
        {
            msg_Err( p_stream, "no suitable sout mux module for `%s/%s://%s'",
                psz_access, psz_mux, psz_url );

            sout_AccessOutDelete( p_access );
            goto end;
        }
    }

    if( var_GetBool( p_stream, SOUT_CFG_PREFIX"sap" ) )
        create_SDP( p_stream, p_access );

    if( !sout_AccessOutCanControlPace( p_access ) )
        p_sout->i_out_pace_nocontrol++;

    p_stream->pf_add    = Add;
    p_stream->pf_del    = Del;
    p_stream->pf_send   = Send;

    ret = VLC_SUCCESS;

    msg_Dbg( p_this, "using `%s/%s://%s'", psz_access, psz_mux, psz_url );

end:
    if( ret != VLC_SUCCESS )
        free( p_sys );
    free( psz_access );
    free( psz_mux );
    free( psz_url );

    return ret;
}

/*****************************************************************************
 * Close:
 *****************************************************************************/
static void Close( vlc_object_t * p_this )
{
    sout_stream_t     *p_stream = (sout_stream_t*)p_this;
    sout_stream_sys_t *p_sys    = p_stream->p_sys;
    sout_access_out_t *p_access = p_sys->p_mux->p_access;

    if( p_sys->p_session != NULL )
        sout_AnnounceUnRegister( p_stream->p_sout, p_sys->p_session );

    sout_MuxDelete( p_sys->p_mux );
    if( !sout_AccessOutCanControlPace( p_access ) )
        p_stream->p_sout->i_out_pace_nocontrol--;
    sout_AccessOutDelete( p_access );

    free( p_sys );
}
