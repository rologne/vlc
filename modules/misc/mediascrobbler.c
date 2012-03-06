/*****************************************************************************
 * mediascrobbler.c : watched.it media scrobbler submission plugin
 *****************************************************************************
 * Copyright © 2006-2012 the VideoLAN team
 * $Id$
 *
 *
 * Author: Lee Hambley <lee dot hambley at gmail dot com>
 *
 *         Rafaël Carré <funman at videolan org>
 *         Ilkka Ollakka <ileoo at videolan org>
 *
 * This is a modified version of the audioscrobbler.c file as written by
 * Rafaël Carré and Ilkka Ollakka.
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

#include <time.h>

#include <vlc_common.h>
#include <vlc_plugin.h>
#include <vlc_interface.h>
#include <vlc_dialog.h>
#include <vlc_meta.h>
#include <vlc_md5.h>
#include <vlc_stream.h>
#include <vlc_url.h>
#include <vlc_network.h>
#include <vlc_playlist.h>

/*****************************************************************************
 * Local prototypes
 *****************************************************************************/

#define QUEUE_MAX 50

/* Keeps track of metadata to be submitted */
typedef struct mediascrobbler_item_t
{
    char        *psz_n;      /**< item name        */
    char        *psz_u;      /**< item uri/mrl     */
    int         i_l;         /**< item length      */
    time_t      date;        /**< date since epoch */
    mtime_t     i_start;     /**< playing started  */
} mediascrobbler_item_t;

struct intf_sys_t
{
    mediascrobbler_item_t   p_queue[QUEUE_MAX]; /**< items not submitted yet*/
    int                     i_items;            /**< number of items        */

    vlc_mutex_t             lock;               /**< p_sys mutex            */
    vlc_cond_t              wait;               /**< item to submit event   */

    /* submission of played items */
    char                    *psz_submit_host;   /**< where to submit data   */
    int                     i_submit_port;      /**< port to which submit   */
    char                    *psz_submit_file;   /**< file to which submit   */

    /* data about item currently playing */
    mediascrobbler_item_t   p_current_item;     /**< item being played      */

    mtime_t                 time_pause;         /**< time when vlc paused   */
    mtime_t                 time_total_pauses;  /**< total time in pause    */

    bool                    b_submit;           /**< do we have valid data
                                                 * to to submit ?           */

    bool                    b_state_cb;         /**< if we registered the
                                                 * "state" callback         */

    bool                    b_meta_read;        /**< if we read the song's
                                                 * metadata already         */
};

static int  Open            (vlc_object_t *);
static void Close           (vlc_object_t *);
static void Run             (intf_thread_t *);
int ParseURL(char*, char**, char**, int*);

/*****************************************************************************
 * Module descriptor
 ****************************************************************************/

#define APIKEY_TEXT         N_("API Key")
#define APIKEY_LONGTEXT     N_("The username of your Watched.it account, see http://watched.it/preferences")
#define URL_TEXT            N_("Scrobbler URL")
#define URL_LONGTEXT        N_("The URL set for an alternative scrobbler engine")

/* This error value is used when media scrobbler plugin has to be unloaded. */
#define VLC_MEDIASCROBBLER_EFATAL -69

/* media scrobbler client identifier */
#define CLIENT_NAME     PACKAGE
#define CLIENT_VERSION  VERSION

vlc_module_begin ()
    set_category(CAT_INTERFACE)
    set_subcategory(SUBCAT_INTERFACE_CONTROL)
    set_shortname(N_("Watched.it Media Scrobbler"))
    set_description(N_("Submission of watched videos to Watched.it"))
    add_string("watchedit-apikey", "",
                APIKEY_TEXT, APIKEY_LONGTEXT, false)
    add_string("mediascrobbler-url", "scrobbler.watched.it",
                URL_TEXT, URL_LONGTEXT, false)
    set_capability("interface", 0)
    set_callbacks(Open, Close)
vlc_module_end ()

/*****************************************************************************
 * DeleteItem : Delete the char pointers in an item
 *****************************************************************************/
static void DeleteItem(mediascrobbler_item_t* p_item)
{
    FREENULL(p_item->psz_n);
    FREENULL(p_item->psz_u);
}


/* *****************************************************************************/
/*  ReadMetaData : Read meta data when parsed by vlc*/
/* ****************************************************************************/
static void ReadMetaData(intf_thread_t *p_this)
{
    input_thread_t      *p_input;
    input_item_t        *p_item;

    intf_sys_t          *p_sys = p_this->p_sys;

    p_input = playlist_CurrentInput(pl_Get(p_this));

    if (!p_input)
        return;

    p_item = input_GetItem(p_input);
    if (!p_item)
    {
        vlc_object_release(p_input);
        return;
    }

#define ALLOC_ITEM_META(a, b) do { \
         char *psz_meta = input_item_Get##b(p_item); \
         if (psz_meta && *psz_meta) \
             a = encode_URI_component(psz_meta); \
         free(psz_meta); \
     } while (0)

    vlc_mutex_lock(&p_sys->lock);

    p_sys->b_meta_read = true;

    ALLOC_ITEM_META(p_sys->p_current_item.psz_n, Name);
    if (!p_sys->p_current_item.psz_n)
    {
        msg_Dbg(p_this, "No name..");
        DeleteItem(&p_sys->p_current_item);
        goto end;
    }

    ALLOC_ITEM_META(p_sys->p_current_item.psz_u, URI);
    if (!p_sys->p_current_item.psz_u)
    {
        msg_Dbg(p_this, "No URI..");
        DeleteItem(&p_sys->p_current_item);
        goto end;
    }

    /* Now we have read the mandatory meta data, so we can submit that info */
    p_sys->b_submit = true;

    /* Duration in seconds */
    p_sys->p_current_item.i_l = input_item_GetDuration(p_item) / 1000000;

#undef ALLOC_ITEM_META

end:
    vlc_mutex_unlock(&p_sys->lock);
    vlc_object_release(p_input);
}

/*****************************************************************************
 * AddToQueue: Add the played item to the queue to be submitted
 *****************************************************************************/
static void AddToQueue (intf_thread_t *p_this)
{
    mtime_t                     played_time;
    intf_sys_t                  *p_sys = p_this->p_sys;

    vlc_mutex_lock(&p_sys->lock);
    if (!p_sys->b_submit)
        goto end;

    msg_Dbg(p_this, "Adding to queue...");

    msg_Dbg(p_this, "Item: { name:  %s, uri: %s, length: %d }",
        p_sys->p_current_item.psz_n,
        p_sys->p_current_item.psz_u,
        p_sys->p_current_item.i_l);

    /* wait for the user to watch enough before submitting */
    played_time = mdate() - p_sys->p_current_item.i_start -
                            p_sys->time_total_pauses;
    played_time /= 1000000; /* µs → s */

    /*HACK: it seam that the preparsing sometime fail,
            so use the playing time as the item length */
    if (p_sys->p_current_item.i_l == 0)
        p_sys->p_current_item.i_l = played_time;

    /* Don't send item shorter than 60s */
    if (p_sys->p_current_item.i_l < 60)
    {
        msg_Dbg(p_this, "Item too short (< 60s), not submitting");
        goto end;
    }

    /* Send if the user had listen more than 600s (5m) OR half the track length */
    if ((played_time < 600) &&
        (played_time < (p_sys->p_current_item.i_l / 2)))
    {
        msg_Dbg(p_this, "Item not watched long enough, not submitting (was %lld), wanted  > 600, or > %d",
            played_time, (p_sys->p_current_item.i_l / 2));
        goto end;
    }

    /* Check that all required meta are present */
    if (!p_sys->p_current_item.psz_n)
    {
        msg_Dbg(p_this, "No name, not submitting");
        goto end;
    }

    if (!p_sys->p_current_item.psz_u)
    {
        msg_Dbg(p_this, "No URI, not submitting");
        goto end;
    }

    if (p_sys->i_items >= QUEUE_MAX)
    {
        msg_Warn(p_this, "Submission queue is full, not submitting");
        goto end;
    }

#define QUEUE_COPY(a) \
    p_sys->p_queue[p_sys->i_items].a = p_sys->p_current_item.a

#define QUEUE_COPY_NULL(a) \
    QUEUE_COPY(a); \
    p_sys->p_current_item.a = NULL

    QUEUE_COPY(i_l);
    QUEUE_COPY_NULL(psz_u);
    QUEUE_COPY_NULL(psz_n);
#undef QUEUE_COPY_NULL
#undef QUEUE_COPY

    p_sys->i_items++;

    /* signal the main loop we have something to submit */
    vlc_cond_signal(&p_sys->wait);

end:
    DeleteItem(&p_sys->p_current_item);
    vlc_mutex_unlock(&p_sys->lock);
}

/*****************************************************************************
 * PlayingChange: Playing status change callback
 *****************************************************************************/
static int PlayingChange(vlc_object_t *p_this, const char *psz_var,
                       vlc_value_t oldval, vlc_value_t newval, void *p_data)
{

    VLC_UNUSED(oldval);

    intf_thread_t   *p_intf = (intf_thread_t*) p_data;
    intf_sys_t      *p_sys  = p_intf->p_sys;
    input_thread_t  *p_input = (input_thread_t*)p_this;
    int             state;

    VLC_UNUSED(psz_var);

    if (newval.i_int != INPUT_EVENT_STATE) return VLC_SUCCESS;

    state = var_GetInteger(p_input, "state");

    if (!p_sys->b_meta_read && state >= PLAYING_S)
    {
        ReadMetaData(p_intf);
        return VLC_SUCCESS;
    }

    if (state >= END_S)
    {
        AddToQueue(p_intf);
    }
    else if (state == PAUSE_S)
    {
        p_sys->time_pause = mdate();
    }
    else if (p_sys->time_pause > 0 && state == PLAYING_S)
    {
        p_sys->time_total_pauses += (mdate() - p_sys->time_pause);
        p_sys->time_pause = 0;
    }

    return VLC_SUCCESS;
}

/*****************************************************************************
 * ItemChange: Playlist item change callback
 *****************************************************************************/
static int ItemChange(vlc_object_t *p_this, const char *psz_var,
                       vlc_value_t oldval, vlc_value_t newval, void *p_data)
{
    input_thread_t      *p_input;
    intf_thread_t       *p_intf     = (intf_thread_t*) p_data;
    intf_sys_t          *p_sys      = p_intf->p_sys;
    input_item_t        *p_item;

    VLC_UNUSED(p_this); VLC_UNUSED(psz_var);
    VLC_UNUSED(oldval); VLC_UNUSED(newval);

    p_sys->b_state_cb       = false;
    p_sys->b_meta_read      = false;
    p_sys->b_submit         = false;

    p_input = playlist_CurrentInput(pl_Get(p_intf));

    if (!p_input || p_input->b_dead)
        return VLC_SUCCESS;

    p_item = input_GetItem(p_input);
    if (!p_item)
    {
        vlc_object_release(p_input);
        return VLC_SUCCESS;
    }

    p_sys->time_total_pauses = 0;
    time(&p_sys->p_current_item.date);        /* to be sent to upstream */
    p_sys->p_current_item.i_start = mdate();  /* used to calculate run
                                               * time as fallback       */

    var_AddCallback(p_input, "intf-event", PlayingChange, p_intf);
    p_sys->b_state_cb = true;

    if (input_item_IsPreparsed(p_item))
        ReadMetaData(p_intf);

    vlc_object_release(p_input);
    return VLC_SUCCESS;
}

/*****************************************************************************
 * Open: initialize and create stuff
 *****************************************************************************/
static int Open(vlc_object_t *p_this)
{
    intf_thread_t   *p_intf     = (intf_thread_t*) p_this;
    intf_sys_t      *p_sys      = calloc(1, sizeof(intf_sys_t));

    msg_Dbg(p_intf, "Entering Open()");

    if (!p_sys)
        return VLC_ENOMEM;

    p_intf->p_sys = p_sys;

    vlc_mutex_init(&p_sys->lock);
    vlc_cond_init(&p_sys->wait);

    var_AddCallback(pl_Get(p_intf), "item-current", ItemChange, p_intf);

    msg_Dbg(p_intf, "Added Item Change Callback");

    p_intf->pf_run = Run;

    return VLC_SUCCESS;
}

/*****************************************************************************
 * Close: destroy interface stuff
 *****************************************************************************/
static void Close(vlc_object_t *p_this)
{

    playlist_t                  *p_playlist = pl_Get(p_this);
    input_thread_t              *p_input;
    intf_thread_t               *p_intf = (intf_thread_t*) p_this;
    intf_sys_t                  *p_sys  = p_intf->p_sys;

    msg_Dbg(p_intf, "Entering Close()");

    var_DelCallback(p_playlist, "item-current", ItemChange, p_intf);

    p_input = playlist_CurrentInput(p_playlist);
    if (p_input)
    {
        if (p_sys->b_state_cb)
            var_DelCallback(p_input, "intf-event", PlayingChange, p_intf);
        vlc_object_release(p_input);
    }

    int i;
    for (i = 0; i < p_sys->i_items; i++)
        DeleteItem(&p_sys->p_queue[i]);
    free(p_sys->psz_submit_host);
    free(p_sys->psz_submit_file);

    vlc_cond_destroy(&p_sys->wait);
    vlc_mutex_destroy(&p_sys->lock);
    free(p_sys);
}


/*****************************************************************************
 * ParseURL : Split an http:// URL into host, path, and port
 *
 * Example: "62.216.251.205:80/protocol_1.2"
 *      will be split into "62.216.251.205", 80, "protocol_1.2"
 *
 * psz_url will be freed before returning
 * *psz_file & *psz_host will be freed before use
 *
 * Return value:
 *  VLC_ENOMEM      Out Of Memory
 *  VLC_EGENERIC    Invalid url provided
 *  VLC_SUCCESS     Success
 *****************************************************************************/
int ParseURL(char* url, char** host, char** path, int* port)
{

  unsigned char port_given = 0,
                path_given = 0;

  if(url == NULL)
    return -1;

  FREENULL(*host);
  FREENULL(*path);

  if(strstr(url, "http://") == url)
  {
    url = url + 7;
  }

  size_t port_separator_position = strcspn(url, ":");
  size_t path_separator_position = strcspn(url, "/");

  if(port_separator_position != strlen(url))
  {
    port_given = 1;
    *host = strndup(url, port_separator_position);
    if(*host == NULL)
      return VLC_ENOMEM;
  }

  if(path_separator_position != strlen(url))
  {
    path_given = 1;
    if(!port_given)
    {
      *host = strndup(url, path_separator_position);
      if(*host == NULL)
        return VLC_ENOMEM;
    }
  }

  if(port_given)
  {
    *port = atoi(url + port_separator_position + 1);
    *host = strndup(url, port_separator_position);
    if(*host == NULL)
      return VLC_ENOMEM;
  } else {
    *port = 80;
  }

  if(path_given)
  {
    *path = strdup(url + path_separator_position + 1);
  } else {
    *path = strdup("/");
  }
  if(*path == NULL)
    return VLC_ENOMEM;

  return VLC_SUCCESS;
}

static void HandleInterval(mtime_t *next, unsigned int *i_interval)
{
    if (*i_interval == 0)
    {
        /* first interval is 1 minute */
        *i_interval = 1;
    }
    else
    {
        /* else we double the previous interval, up to 120 minutes */
        *i_interval <<= 1;
        if (*i_interval > 120)
            *i_interval = 120;
    }
    *next = mdate() + (*i_interval * 1000000 * 60);
}

/*****************************************************************************
 * Run : call Handshake() then submit items
 *****************************************************************************/
static void Run(intf_thread_t *p_intf)
{

    msg_Dbg(p_intf, "Entering Run()");

    uint8_t                 p_buffer[1024];
    int                     parse_url_ret;
    int                     canc = vlc_savecancel();

    char                    *psz_scrobbler_url;

    mtime_t                 next_exchange;      /**< when can we send data  */
    unsigned int            i_interval;         /**< waiting interval (secs)*/

    intf_sys_t *p_sys = p_intf->p_sys;

    psz_scrobbler_url = var_InheritString(p_intf, "mediascrobbler-url");
    if(!psz_scrobbler_url)
      return;

    msg_Dbg(p_intf, "Scrobbler URL: %s", psz_scrobbler_url);

    /* main loop */
    for (;;)
    {
        vlc_restorecancel(canc);
        vlc_mutex_lock(&p_sys->lock);
        mutex_cleanup_push(&p_sys->lock);

        msg_Dbg(p_intf, "Next exchange %lld (current: %lld)",
            next_exchange, mdate());

        do {
            vlc_cond_wait(&p_sys->wait, &p_sys->lock);
        } while (mdate() < next_exchange);

        vlc_cleanup_run();
        canc = vlc_savecancel();

        msg_Dbg(p_intf, "Going to submit some data...");
        char *psz_submit;
        if (asprintf(&psz_submit, "s=%s", psz_scrobbler_url) == -1)
            return;

        msg_Dbg(p_intf, "Going to parse URL (%s)", psz_scrobbler_url);
        parse_url_ret = ParseURL(psz_scrobbler_url, &p_sys->psz_submit_host,
                                &p_sys->psz_submit_file, &p_sys->i_submit_port);

        if(parse_url_ret != VLC_SUCCESS) {
            msg_Err(p_intf, "Couldn't process URL, can't continue");
            return;
        }

        msg_Dbg(p_intf, "Submit Host: %s", p_sys->psz_submit_host);

        msg_Dbg(p_intf, "Preparing to submit %d items", p_sys->i_items);

        /* forge the HTTP POST request */
        vlc_mutex_lock(&p_sys->lock);
        mediascrobbler_item_t *p_item;
        for (int i_item = 0 ; i_item < p_sys->i_items ; i_item++)
        {
            char *psz_submit_item, *psz_submit_tmp;
            p_item = &p_sys->p_queue[i_item];
            if (asprintf(&psz_submit_item,
                    "&n%%5B%d%%5D=%s"
                    "&u%%5B%d%%5D=%s"
                    "&d%%5B%d%%5D=%ju"
                    "&l%%5B%d%%5D=%d",
                    i_item, p_item->psz_n,
                    i_item, p_item->psz_u,
                    i_item, p_item->date,
                    i_item, p_item->i_l
           ) == -1)
            {   /* Out of memory */
                vlc_mutex_unlock(&p_sys->lock);
                return;
            }
            psz_submit_tmp = psz_submit;
            if (asprintf(&psz_submit, "%s%s",
                    psz_submit_tmp, psz_submit_item) == -1)
            {   /* Out of memory */
                free(psz_submit_tmp);
                free(psz_submit_item);
                vlc_mutex_unlock(&p_sys->lock);
                return;
            }
            free(psz_submit_item);
            free(psz_submit_tmp);
        }
        vlc_mutex_unlock(&p_sys->lock);

        int i_post_socket = net_ConnectTCP(p_intf, p_sys->psz_submit_host, p_sys->i_submit_port);

        if (i_post_socket == -1)
        {
            msg_Warn(p_intf, "Couldn't talk to the API, waiting to try again. (%d)", i_interval);
            HandleInterval(&next_exchange, &i_interval);
            free(psz_submit);
            continue;
        }

        /* we transmit the data */
        int i_net_ret = net_Printf(p_intf, i_post_socket, NULL,
            "POST %s HTTP/1.1\r\n"
            "Accept-Encoding: identity\r\n"
            "Content-length: %zu\r\n"
            "Connection: close\r\n"
            "Content-type: application/x-www-form-urlencoded\r\n"
            "Host: %s\r\n"
            "User-agent: VLC media player/"VERSION"\r\n"
            "\r\n"
            "%s\r\n"
            "\r\n",
            p_sys->psz_submit_file, strlen(psz_submit),
            p_sys->psz_submit_host, psz_submit
       );

        free(psz_submit);
        if (i_net_ret == -1)
        {
            /* If connection fails, back off the timer, and try again */
            HandleInterval(&next_exchange, &i_interval);
            continue;
        }

        i_net_ret = net_Read(p_intf, i_post_socket, NULL,
                    p_buffer, sizeof(p_buffer) - 1, false);
        if (i_net_ret <= 0)
        {
            /* if we get no answer, something went wrong : try again */
            continue;
        }

        net_Close(i_post_socket);
        p_buffer[i_net_ret] = '\0';

        char *failed = strstr((char *) p_buffer, "FAILED");
        if (failed)
        {
            msg_Warn(p_intf, "%s", failed);
            HandleInterval(&next_exchange, &i_interval);
            continue;
        }

        if (strstr((char *) p_buffer, "BADAPIKEY"))
        {
            msg_Err(p_intf, "Authentication failed (BADAPIKEY), are you sure your API key is valid?");
            HandleInterval(&next_exchange, &i_interval);
            continue;
        }

        if (strstr((char *) p_buffer, "OK"))
        {
            for (int i = 0; i < p_sys->i_items; i++)
                DeleteItem(&p_sys->p_queue[i]);
            p_sys->i_items = 0;
            i_interval = 0;
            next_exchange = mdate();
            msg_Dbg(p_intf, "Submission successful!");
        }
        else
        {
            msg_Err(p_intf, "Authentication failed, trying again (%s)",
                             p_buffer);
            HandleInterval(&next_exchange, &i_interval);
        }
    }
    vlc_restorecancel(canc);
}
