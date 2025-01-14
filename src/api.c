//  ==============================================================================================
//
//  Module: API
//
//  Description:
//  Main control/query interface for the plugins
//
//  Original Author:
//  J.S. Schroeder (schroeder-lvb@outlook.com)    2019.08.14
//
//  Released under MIT License
//  ID Authenticator: c4c5a1eda6815f65bb2eefd15c5b5058f996add99fa8800831599a7eb5c2a04c
//
//  ==============================================================================================
//

#define _GNU_SOURCE


#include <stdio.h>
#include <stdlib.h>
#include <time.h>
#include <string.h>
#include <stdarg.h>

#ifndef _WIN32
#include <unistd.h>
#else
#include "winport.h"         // sleep/usleep function
#endif


#include "util.h"
#include "bsd.h"
#include "log.h"
#include "cfs.h"
#include "ftrack.h"
#include "events.h"
#include "rdrv.h"
#include "alarm.h"
#include "sissm.h"                                              // required for sissmGetConfigPath
#include "roster.h"

#include "api.h"

#define API_R_BUFSIZE                (4*1024)
#define API_T_BUFSIZE                (4*1024)

#define API_LOG2RCON_DELAY_MICROSEC  (250000)          // system delay between log to rcon (tuned) 
#define API_LISTPLAYERS_PERIOD           (10)     // #seconds periodic for listserver roster fetch

#define API_ROSTER_STRING_MAX         (80*64)            // max size of contatenated roster string 
#define API_LINE_STRING_MAX             (256)   

//  ==============================================================================================
//  Data definition  
//
//
//
static rdrvObj *_rPtr = NULL;                    // RCON driver handle - interface to game server
static alarmObj *_apiPollAlarmPtr  = NULL;      // used to periodically poll roster (listplayers)

//  Store string concatenated roster for two consecutive iterations - previous & current.
//  The difference produces player connection and disconnection status
//  
char rosterPrevious[ API_ROSTER_STRING_MAX ];    
char rosterCurrent[ API_ROSTER_STRING_MAX ];
static long lastRosterSuccessTime = 0L;            // marks last time listplayer read was success 

//  Table of admins list - can be used by any plugins to identify if 
//  a transction is originated from an admin.  See: apiIdListCheck().
//
static idList_t adminIdList;                                                       // Admins list 
static char adminListFilePath[ API_LINE_STRING_MAX ];             // full file path to admins.txt

static wordList_t badWordsList;                                               // Bad words list
static char badWordsFilePath[ API_LINE_STRING_MAX ];             // full file path to admins.txt


//  ==============================================================================================
//  apiWordListRead
//
//  Reads a list of words from a file - words must be entered one line each.
//  This function returns number of words read.  If the file fails to open, -1 is returned.
//
int apiWordListRead( char *listFile, wordList_t wordList )
{
    int i;
    char tmpLine[1024], *w;
    FILE *fpr;

    for (i=0; i<WORDLISTMAXELEM; i++) strcpy( wordList[i], "" );

    i = -1;
    if (NULL != (fpr = fopen( listFile, "rt" ))) {
        i = 0;
        while (!feof( fpr )) { 
            if (NULL != fgets( tmpLine, 1024, fpr )) {
                tmpLine[ strlen( tmpLine ) - 1] = 0;
                strTrimInPlace( tmpLine );
                if ( 0 != strlen( tmpLine ) ) {
                    // strToLowerInPlace( tmpLine ); 
                    strlcpy( wordList[ i ], tmpLine, WORDLISTMAXSTRSZ );
                    if ( (++i) > WORDLISTMAXELEM) break;
                }
            }
        }
        fclose( fpr );
    }
    return( i );
}


//  ==============================================================================================
//  apiWordListCheck
//
//  Checks if a a word is contained in the string.  This function returns a 1 if found,
//  0 if not found.
//
int apiWordListCheck( char *stringTested, wordList_t wordList )
{
    int i;
    int isMatch = 0;

    for (i=0; i<WORDLISTMAXELEM; i++) {

        if ( wordList[i][0] == 0 ) break;
        if ( NULL != strcasestr( stringTested, wordList[i] )) {
            isMatch = 1;
            break;
        }
    }
    return( isMatch );
}


//  ==============================================================================================
//  apiIdListRead
//
//  Reads a list of STEAMID64 from a file.  This can be used to check for admins, moderators,
//  or any other group of clients such as friends, watch-list, etc.
//  This function returns number of clients read.  If file fails to open, -1 is returned.
//  The input list format is compatible wtih Sandstorm Admins.txt.
//
int apiIdListRead( char *listFile, idList_t idList )
{
    int i;
    char tmpLine[1024], *w;
    FILE *fpr;

    for (i=0; i<IDLISTMAXELEM; i++) strcpy( idList[i], "" );

    i = -1;
    if (NULL != (fpr = fopen( listFile, "rt" ))) {
        i = 0;
        while (!feof( fpr )) {
            if (NULL != fgets( tmpLine, 1024, fpr )) {
                tmpLine[ strlen( tmpLine ) - 1] = 0;
                if ( 0 != strlen( tmpLine ) ) {
                    w = getWord( tmpLine, 0, " \012\015\011");
                    if (strlen(w) == IDSTEAMID64LEN) {
                        strlcpy( idList[i], w, IDSTEAMID64LEN+1 );
                        i++;
                        if ( i >= (IDLISTMAXELEM-1) ) break;
                    }
                }
            }
        }
        fclose( fpr );
    }
    return( i );
}


//  ==============================================================================================
//  apiBadNameCheck
//
//  Checks if bad name.  This function returns a 1 if bad, 0 if ok
//
int apiBadNameCheck( char *nameIn )
{
    return (apiWordListCheck( nameIn, badWordsList )); 
}


//  ==============================================================================================
//  apiIdListCheck
//
//  Checks if a client GUID is contained in the idList.  This function returns a 1 if found,
//  0 if not found.
//
int apiIdListCheck( char *connectID, idList_t idList )
{
    int i;
    int isMatch = 0;

    for (i=0; i<IDLISTMAXELEM; i++) {
        if (0 == strcmp( idList[i], connectID )) {
            isMatch = 1;
            break;
        }
    }
    return( isMatch );
}

//  ==============================================================================================
//  apiIsAdmin
//
//  Returns 1 if the GUID is an admin, 0 if not 
//  
//  
int apiIsAdmin( char *connectID )
{
    return( apiIdListCheck( connectID, adminIdList ) );
}



//  ==============================================================================================
//  apiGetLastRosterTime
//
//  Returns last successful roster fetch from RCON.  This is used by plugins (such as
//  pirebooter) to detect server crash, that may lead to system restart.
//
unsigned long apiGetLastRosterTime( void )
{
    return( lastRosterSuccessTime );
}

//  ==============================================================================================
//  rosterSyntheticDelEvent
//
//  Synthetic (self generated) event that dispatches event handles of subscribed plugins
//  when a player has disconnected.
//
int rosterSyntheticDelEvent(char *playerName, char *playerIP, char *playerGUID )
{
    char strOut[API_LINE_STRING_MAX];

    snprintf( strOut, API_LINE_STRING_MAX, "~SYNTHDEL~ %s %s %s", playerGUID, playerIP, playerName );
    eventsDispatch( strOut  );
    return 0;
}


//  ==============================================================================================
//  rosterSyntheticAddEvent
//
//  Synthetic (self generated) event that dispatches event handles of subscribed plugins
//  when a player has connected.
//
int rosterSyntheticAddEvent(char *playerName, char *playerIP, char *playerGUID )
{
    char strOut[API_LINE_STRING_MAX];

    snprintf( strOut, API_LINE_STRING_MAX, "~SYNTHADD~ %s %s %s", playerGUID, playerIP, playerName );
    eventsDispatch( strOut  );
    return 0;
}

//  ==============================================================================================
//  _apiPollAlarmCB (local function)
//
//  Call-back function dispatched by self-resetting periodic alarm (system alarm dispatcher),
//  however, this routine is also called 1) when Client-Add (connection) event activated, 
//  and 2) when client-Del (disconection) event is activated (see below).  
//
//  This method uses RCON interface to fetch the player roster list from
//  the game server, determines if any clients have connected or disconnected, then generates
//  a synthetic event to invoke registered plugin callbacks.
//
int _apiPollAlarmCB( char *strIn )
{
    char rconCmd[API_T_BUFSIZE], rconResp[API_R_BUFSIZE];
    int  bytesRead, errCode = 0;

    logPrintf( LOG_LEVEL_RAWDUMP, "api", "Roster update alarm callback ::%s::", strIn );

    // Fetch the roster
    //
    snprintf( rconCmd, API_T_BUFSIZE, "listplayers" );
    memset( rconResp, 0, API_R_BUFSIZE );
    errCode = rdrvCommand( _rPtr, 2, rconCmd, rconResp, &bytesRead );
    if ( !errCode ) {
        rosterParse( rconResp, bytesRead );
        // logPrintf( LOG_LEVEL_DEBUG, "api", "Listplayer success, player count is %d", rosterCount());
        lastRosterSuccessTime = apiTimeGet();             // record this to check for dead servers
	strlcpy( rosterCurrent, rosterPlayerList( 4, "\011" ), API_ROSTER_STRING_MAX );
	rosterSyntheticChangeEvent( rosterPrevious, rosterCurrent, rosterSyntheticDelEvent ); 
	rosterSyntheticChangeEvent( rosterCurrent, rosterPrevious, rosterSyntheticAddEvent ); 
	strlcpy( rosterPrevious, rosterCurrent, API_ROSTER_STRING_MAX );

    }
    else {
        logPrintf( LOG_LEVEL_INFO, "api", 
            "Listplayer retrieve failure on apiPollAlarmCB, playercount is %d", rosterCount() );
    }
    // reset the alarm for the next iteration
    //
    alarmReset( _apiPollAlarmPtr, API_LISTPLAYERS_PERIOD );

    return 0;
}

	
//  ==============================================================================================
//  _apiPlayerConnectedCB
//
//  Call-back function dispatched by system event dispatcher when the game logfile 
//  indicates a new player connection.
//
int _apiPlayerConnectedCB( char *strIn )
{
    logPrintf( LOG_LEVEL_RAWDUMP, "api", "Player Connected callback ::%s::", strIn );

    // Add delay here because there is a time lag between log file reporting 'add connection'
    // until the new player shows up on RCON roster read via the listplayer command.
    // The delay becomes more pronounced on a busy (CPU loaded) server state.
    //
    usleep( API_LOG2RCON_DELAY_MICROSEC );

    // Update the roster which also resets the schedule for next refresh
    // This routine resets the next alarm time 
    // 
    _apiPollAlarmCB("PlayerConnected");

    return 0;
}


//  ==============================================================================================
//  _apiPlayerDisconnectedCB
//
//  Call-back function dispatched by system event dispatcher when the game logfile 
//  indicates a player has disconnected.
//
int _apiPlayerDisconnectedCB( char *strIn )
{
    logPrintf( LOG_LEVEL_RAWDUMP, "api", "Player Disconnected callback ::%s::", strIn );

    // Update the roster which also resets the schedule for next refresh
    // This routine resets the next alarm time
    //
    _apiPollAlarmCB("PlayerDisconnected");

    return 0;
}

//  ==============================================================================================
//  _apiMapChangeCB
//
//  Call-back function dispatched when the game system log file indicates a map change.
//  Simply update the name which can be read back from Plugins via the API at later time.
//
int _apiMapChangeCB( char *strIn )
{
    char _currMap[API_LINE_STRING_MAX];

    rosterParseMapname( strIn, API_LINE_STRING_MAX, _currMap );
    rosterSetMapName( _currMap );
    return 0;
}


//  ==============================================================================================
//  apiInit
//
//  One-time initialization of the API module.  Like the plugins, this module installs several
//  event and alarm handlers for internal use.
//
int apiInit( void )
{
    cfsPtr cP;
    char   myIP[API_LINE_STRING_MAX], myRconPassword[API_LINE_STRING_MAX];
    int    myPort, adminCount, badWordsCount;
    char   serverName[API_LINE_STRING_MAX], webFileName[API_LINE_STRING_MAX];

    // Read the "sissm" systems configuration variables
    //
    cP = cfsCreate( sissmGetConfigPath() );

    myPort = (int) cfsFetchNum( cP, "sissm.RconPort", 27015 );            
    strlcpy( myIP, cfsFetchStr( cP, "sissm.RconIP", "127.0.0.1"), API_LINE_STRING_MAX );
    strlcpy( myRconPassword, cfsFetchStr( cP, "sissm.RconPassword", ""), API_LINE_STRING_MAX );
    strlcpy( adminListFilePath, cfsFetchStr( cP, "sissm.adminListFilePath", "Admins.txt"), API_LINE_STRING_MAX );

    strlcpy( serverName, cfsFetchStr( cP, "sissm.ServerName", "Unknown Server" ), API_LINE_STRING_MAX );

    // read the Bad Words filename
    //
    strlcpy( badWordsFilePath, cfsFetchStr( cP, "sissm.badWordsFilePath", "" ), CFS_FETCH_MAX );

    cfsDestroy( cP );

    // Set map to unknown
    //
    rosterSetMapName( "Unknown" );

    // Initialize the RCON TCP/IP interface
    //
    _rPtr = rdrvInit( myIP, myPort, myRconPassword );

    // Setup callbacks for player entering and leaving
    // 
    eventsRegister( SISSM_EV_CLIENT_ADD, _apiPlayerConnectedCB );
    eventsRegister( SISSM_EV_CLIENT_DEL, _apiPlayerDisconnectedCB );
    eventsRegister( SISSM_EV_MAPCHANGE,  _apiMapChangeCB );

    // Setup Alarm (periodic callbacks) for fetching roster from RCON
    // 
    _apiPollAlarmPtr = alarmCreate( _apiPollAlarmCB );
    alarmReset( _apiPollAlarmPtr, API_LISTPLAYERS_PERIOD );

    // Clear the Roster module that keeps track of players
    //
    rosterInit();
    rosterSetServerName( serverName );
    lastRosterSuccessTime = apiTimeGet();      
    rosterReset();

    // Read the admin list
    //
    adminCount = apiIdListRead( adminListFilePath, adminIdList );
    logPrintf( LOG_LEVEL_CRITICAL, "api", "Admin list %d clients from file %s", adminCount, adminListFilePath );

    // Read the Bad Words list
    //
    badWordsCount = apiWordListRead( badWordsFilePath, badWordsList );
    logPrintf( LOG_LEVEL_CRITICAL, "api", "BadWords list %d words from file %s", badWordsCount, badWordsFilePath );

    return( _rPtr == NULL );
}


//  ==============================================================================================
//  apiDestroy
//
//  Terminate this API module
//
int apiDestroy( void )
{
    rdrvDestroy( _rPtr );
    _rPtr = NULL; 
    return 0;
}

//  ==============================================================================================
//  apiServeRestart
//
//  Called from a Plugin, this method restarts the game server. 
//  The lastRosterSuccessTime is reset so that the invoked restart will zero out the 'busy'
//  timeout, tracked by pirebooter plugin and other equivalent up-time aware plugin.
//
int apiServerRestart( void )
{
    sissmServerRestart();
    lastRosterSuccessTime = apiTimeGet();      
    return 0;
}

//  ==============================================================================================
//  apiTimeGet
//
//  Called from a Plugin, this method fetches EPOC time, resolution 1 second.
//
unsigned int apiTimeGet( void )
{
    return( time(NULL) );
}

//  ==============================================================================================
//  apiTimeGetHuman
//
//  Called from a Plugin, this method fetches human readable time.
//
char *apiTimeGetHuman( void )
{
    static char humanTime[API_LINE_STRING_MAX];
    time_t current_time;

    current_time = time( NULL );
    strlcpy( humanTime, ctime( &current_time ), API_LINE_STRING_MAX );
    return( humanTime );
}

//  ==============================================================================================
//  apiGameModePropertySet
//
//  Called from a Plugin, this method sets gamemodeproperty (cvar) on game server.
//
int apiGameModePropertySet( char *gameModeProperty, char *value )
{
    char rconCmd[API_T_BUFSIZE], rconResp[API_R_BUFSIZE];
    int bytesRead;

    snprintf( rconCmd, API_T_BUFSIZE, "gamemodeproperty %s %s", gameModeProperty, value );
    rdrvCommand( _rPtr, 2, rconCmd, rconResp, &bytesRead );

    return( 0 );
}

//  ==============================================================================================
//  apiGameModePropertyGet
//
//  Called from a Plugin, this method reads gamemodeproperty (cvar) from the game server.
//
char *apiGameModePropertyGet( char *gameModeProperty )
{
    static char value[16*1024];
    char rconCmd[API_T_BUFSIZE], rconResp[API_R_BUFSIZE];
    int bytesRead;

    strcpy( value, "" );

    snprintf( rconCmd, API_T_BUFSIZE, "gamemodeproperty %s", gameModeProperty );
    rdrvCommand( _rPtr, 2, rconCmd, rconResp, &bytesRead );

    if ( bytesRead > strlen( gameModeProperty )) {
        strncpy( value, getWord( rconResp, 1, "\"" ), sizeof( value ));
    }

    return( value );
}

//  ==============================================================================================
//  apiSay
//
//  Called from a Plugin, this method sends text to in-game screen of all players, 
//  prefixed by the string "Admin:".
//
int apiSay( const char * format, ... )
{
    static char buffer[API_T_BUFSIZE];
    char rconCmd[API_T_BUFSIZE], rconResp[API_R_BUFSIZE];
    int bytesRead;

    va_list args;
    va_start( args, format );
    vsnprintf( buffer, API_T_BUFSIZE, format, args );

    snprintf( rconCmd, API_T_BUFSIZE, "say %s", buffer );
    if ( 0 != strlen( buffer ) )   // say only when something to be said
        rdrvCommand( _rPtr, 2, rconCmd, rconResp, &bytesRead );

    va_end (args);
    return 0;
}
  
//  ==============================================================================================
//  apiKickOrBanSay
//
//  Called from a Plugin, this method is used to kick or ban a player by SteamGUID64 identifier.
//  Optional *reason string may be attached, or call with empty string if not needed ("").
//  The player must be actively in game or else the command will fail.
//
int apiKickOrBan( int isBan, char *playerGUID, char *reason )
{
    char rconCmd[API_T_BUFSIZE], rconResp[API_R_BUFSIZE];
    int bytesRead;

    if ( isBan ) {
        snprintf( rconCmd, API_T_BUFSIZE, "ban %s -1 %s", playerGUID, reason );
    }
    else {
        snprintf( rconCmd, API_T_BUFSIZE, "kick %s %s", playerGUID, reason );
    }
    rdrvCommand( _rPtr, 2, rconCmd, rconResp, &bytesRead );

    return 0;
}


//  ==============================================================================================
//  apiRcon
//
//  Called from a Plugin, this provides a Low-level RCON send-receive exchange.
//
int apiRcon( char *commandOut, char *statusIn )
{
    int bytesRead;
    rdrvCommand( _rPtr, 2, commandOut, statusIn, &bytesRead );
    return( bytesRead );
}

//  ==============================================================================================
//  apiPlayersGetCount
//
//  Called from a plugin, this routine returns number players connected in-game.  
//  This count is derived from internally maintained roster list, which is updated 
//  periodically (time), or whenever an ADD or DEL client event is generated by 
//  monitoring the system game log file.
//  
//  In the event of RCON interface error, this routine returns the last known, last successful
//  roster count value.
//
int apiPlayersGetCount( void )
{
    return ( rosterCount() );
}

//  ==============================================================================================
//  apiPlayersRoster
//
//  Called from a plugin, this routine returns a single contatenated string of all connected 
//  players information such as name, GUID, IP, and score.  For different output format options
//  see rosterPlayerList() in roster.c.  You may specify your custom delimiter string between
//  individual player data.
//
char *apiPlayersRoster( int infoDepth, char *delimiter )
{
    return ( rosterPlayerList(infoDepth, delimiter) );
}


//  ==============================================================================================
//  apiGetServerName
//
//  Called from a plugin, this routine returns the server name specified in the sissm.log file
//  (it is not read from the game server!)
//
char *apiGetServerName( void )
{
    return ( rosterGetServerName() );
}


//  ==============================================================================================
//  apiGetMapName
//
//  Called from a plugin, this routine returns the current map name.  Currently, the map name
//  returned here is not valid until after the first map change as executed since the restart.
//
char *apiGetMapName( void )
{
    return ( rosterGetMapName() );
}



