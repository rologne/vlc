/*****************************************************************************
 * input_manager.hpp : Manage an input and interact with its GUI elements
 ****************************************************************************
 * Copyright (C) 2006-2008 the VideoLAN team
 * $Id$
 *
 * Authors: Clément Stenac <zorglub@videolan.org>
 *          Jean-Baptiste <jb@videolan.org>
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

#ifndef QVLC_INPUT_MANAGER_H_
#define QVLC_INPUT_MANAGER_H_

#ifdef HAVE_CONFIG_H
# include "config.h"
#endif

#include <vlc_input.h>

#include "qt4.hpp"
#include "util/singleton.hpp"

#include <QObject>
#include <QEvent>


enum {
    PositionUpdate_Type = QEvent::User + IMEventType + 1,
    ItemChanged_Type,
    ItemStateChanged_Type,
    ItemTitleChanged_Type,
    ItemRateChanged_Type,
    VolumeChanged_Type,
    SoundMuteChanged_Type,
    ItemEsChanged_Type,
    ItemTeletextChanged_Type,
    InterfaceVoutUpdate_Type,
    StatisticsUpdate_Type, /*10*/
    InterfaceAoutUpdate_Type,
    MetaChanged_Type,
    NameChanged_Type,
    InfoChanged_Type,
    SynchroChanged_Type,
    CachingEvent_Type,
    BookmarksChanged_Type,
    RecordingEvent_Type,
    ProgramChanged_Type,
    RandomChanged_Type,
    LoopChanged_Type,
    RepeatChanged_Type,
    LeafToParent_Type,
    EPGEvent_Type,
/*    SignalChanged_Type, */

    FullscreenControlToggle_Type = QEvent::User + IMEventType + 20,
    FullscreenControlShow_Type,
    FullscreenControlHide_Type,
    FullscreenControlPlanHide_Type,
};

enum { NORMAL,    /* loop: 0, repeat: 0 */
       REPEAT_ONE,/* loop: 0, repeat: 1 */
       REPEAT_ALL,/* loop: 1, repeat: 0 */
};

class IMEvent : public QEvent
{
friend class InputManager;
friend class MainInputManager;
    public:
    IMEvent( int type, input_item_t *p_input = NULL )
        : QEvent( (QEvent::Type)(type) )
    {
        if( (p_item = p_input) != NULL )
            vlc_gc_incref( p_item );
    }
    virtual ~IMEvent()
    {
        if( p_item )
            vlc_gc_decref( p_item );
    }

private:
    input_item_t *p_item;
};

enum PLEventTypes
{
    PLItemAppended_Type = QEvent::User + PLEventType + 1,
    PLItemRemoved_Type,
    PLEmpty_Type
};

class PLEvent : public QEvent
{
public:
    PLEvent( int t, int i, int p = 0 )
        : QEvent( (QEvent::Type)(t) ), i_item(i), i_parent(p) {}

    /* Needed for "playlist-item*" and "leaf-to-parent" callbacks
     * !! Can be a input_item_t->i_id or a playlist_item_t->i_id */
    int i_item;
    // Needed for "playlist-item-append" callback, notably
    int i_parent;
};

class InputManager : public QObject
{
    Q_OBJECT
    friend class MainInputManager;

public:
    InputManager( QObject *, intf_thread_t * );
    virtual ~InputManager();

    void delInput();
    bool hasInput()
    {
        return p_input /* We have an input */
            && !p_input->b_dead /* not dead yet, */
            && !p_input->b_eof  /* not EOF either, */
            && vlc_object_alive (p_input); /* and the VLC object is alive */
    }

    int playingStatus();
    bool hasAudio();
    bool hasVideo() { return hasInput() && b_video; }
    bool hasVisualisation();
    void requestArtUpdate();

    QString getName() { return oldName; }
    static const QString decodeArtURL( input_item_t *p_item );

private:
    intf_thread_t  *p_intf;
    input_thread_t *p_input;
    vlc_object_t   *p_input_vbi;
    input_item_t   *p_item;
    int             i_old_playing_status;
    QString         oldName;
    QString         artUrl;
    float           f_rate;
    float           f_cache;
    bool            b_video;
    mtime_t         timeA, timeB;

    void customEvent( QEvent * );

    void addCallbacks();
    void delCallbacks();

    void UpdateRate();
    void UpdateName();
    void UpdateStatus();
    void UpdateNavigation();
    void UpdatePosition();
    void UpdateTeletext();
    void UpdateArt();
    void UpdateInfo();
    void UpdateMeta();
    void UpdateMeta(input_item_t *);
    void UpdateVout();
    void UpdateAout();
    void UpdateStats();
    void UpdateCaching();
    void UpdateRecord();
    void UpdateProgramEvent();
    void UpdateEPG();

public slots:
    void setInput( input_thread_t * ); ///< Our controlled input changed
    void sliderUpdate( float ); ///< User dragged the slider. We get new pos
    /* SpeedRate Rate Management */
    void reverse();
    void slower();
    void faster();
    void littlefaster();
    void littleslower();
    void normalRate();
    void setRate( int );
    /* Jumping */
    void jumpFwd();
    void jumpBwd();
    /* Menus */
    void sectionNext();
    void sectionPrev();
    void sectionMenu();
    /* Teletext */
    void telexSetPage( int );          ///< Goto teletext page
    void telexSetTransparency( bool ); ///< Transparency on teletext background
    void activateTeletext( bool );     ///< Toggle buttons after click
    /* A to B Loop */
    void setAtoB();

private slots:
    void AtoBLoop( float, int64_t, int );

signals:
    /// Send new position, new time and new length
    void positionUpdated( float , int64_t, int );
    void seekRequested( float pos );
    void rateChanged( float );
    void nameChanged( const QString& );
    /// Used to signal whether we should show navigation buttons
    void titleChanged( bool );
    void chapterChanged( bool );
    /// Statistics are updated
    void statisticsUpdated( input_item_t* );
    void infoChanged( input_item_t* );
    void currentMetaChanged( input_item_t* );
    void metaChanged( input_item_t *);
    void artChanged( QString );
    /// Play/pause status
    void playingStatusChanged( int );
    void recordingStateChanged( bool );
    /// Teletext
    void teletextPossible( bool );
    void teletextActivated( bool );
    void teletextTransparencyActivated( bool );
    void newTelexPageSet( int );
    /// Advanced buttons
    void AtoBchanged( bool, bool );
    /// Vout
    void voutChanged( bool );
    void voutListChanged( vout_thread_t **pp_vout, int i_vout );
    /// Other
    void synchroChanged();
    void bookmarksChanged();
    void cachingChanged( float );
    /// Program Event changes
    void encryptionChanged( bool );
    void epgChanged();
};

class MainInputManager : public QObject, public Singleton<MainInputManager>
{
    Q_OBJECT
    friend class Singleton<MainInputManager>;
public:
    input_thread_t *getInput() { return p_input; }
    InputManager *getIM() { return im; }
    inline input_item_t *currentInputItem()
    {
        return ( p_input ? input_GetItem( p_input ) : NULL );
    }

    vout_thread_t* getVout();
    audio_output_t *getAout();

    bool getPlayExitState();
    bool hasEmptyPlaylist();
private:
    MainInputManager( intf_thread_t * );
    virtual ~MainInputManager();

    void customEvent( QEvent * );

    InputManager            *im;
    input_thread_t          *p_input;
    intf_thread_t           *p_intf;

    void notifyRepeatLoop();
public slots:
    void togglePlayPause();
    void play();
    void pause();
    void toggleRandom();
    void stop();
    void next();
    void prev();
    void prevOrReset();
    void activatePlayQuit( bool );

    void loopRepeatLoopStatus();

signals:
    void inputChanged( input_thread_t * );
    void volumeChanged();
    void soundMuteChanged();
    void playlistItemAppended( int itemId, int parentId );
    void playlistItemRemoved( int itemId );
    void playlistNotEmpty( bool );
    void randomChanged( bool );
    void repeatLoopChanged( int );
    void leafBecameParent( int );
};

#endif
