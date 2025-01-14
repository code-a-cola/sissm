//  ==============================================================================================
//
//  Module: EVENTS
//
//  Description:
//  Event-driven engine with callback features: init, register, dispatch
//
//  Original Author:
//  J.S. Schroeder (schroeder-lvb@outlook.com)    2019.08.14
//
//  Released under MIT License
//  ID Authenticator: c4c5a1eda6815f65bb2eefd15c5b5058f996add99fa8800831599a7eb5c2a04c
//
//  ==============================================================================================


#define SISSM_MAXEVENTS                     (24)
#define SISSM_MAXPLUGINS                    (24)

#define SISSM_EV_INIT                       ( 0)    // order of the indeces must match
#define SISSM_EV_RESTART                    ( 1)    // the sequence of eventsTable[]
#define SISSM_EV_CLIENT_ADD                 ( 2)
#define SISSM_EV_CLIENT_DEL                 ( 3)
#define SISSM_EV_MAPCHANGE                  ( 4)
#define SISSM_EV_GAME_START                 ( 5)
#define SISSM_EV_GAME_END                   ( 6)
#define SISSM_EV_ROUND_START                ( 7)
#define SISSM_EV_ROUND_END                  ( 8)
#define SISSM_EV_OBJECTIVE_CAPTURED         ( 9)
#define SISSM_EV_PERIODIC                   (10)
#define SISSM_EV_CLIENT_ADD_SYNTH           (11)
#define SISSM_EV_CLIENT_DEL_SYNTH           (12)
#define SISSM_EV_SHUTDOWN                   (13)
#define SISSM_EV_CHAT                       (14)
#define SISSM_EV_SIGTERM                    (15)


// Following substring in log file triggers an event
//
#define SS_SUBSTR_ROUND_START  "Match State Changed from PreRound to RoundActive"
#define SS_SUBSTR_ROUND_END    "Match State Changed from RoundWon to PostRound"
#define SS_SUBSTR_GAME_START   "Match State Changed from GameStarting to PreRound"
#define SS_SUBSTR_GAME_END     "Match State Changed from GameOver to LeavingMap"
#define SS_SUBSTR_REGCLIENT    "LogNet: Join succeeded:"
#define SS_SUBSTR_UNREGCLIENT  "LogNet: UChannel::Close:"
#define SS_SUBSTR_OBJECTIVE    "LogGameMode: Display: Advancing spawns for faction"
#define SS_SUBSTR_MAPCHANGE    "SeamlessTravel to:"
#define SS_SUBSTR_CAPTURE      "LogSpawning: Spawnzone '"
#define SS_SUBSTR_SHUTDOWN     "LogExit: Game engine shut down"
#define SS_SUBSTR_CHAT         "LogChat: Display:"

extern int eventsInit( void );
extern int eventsRegister( int eventID, int (*callBack)( char * ));
extern int eventsDispatch( char *strBuffer );


