#include <cstdio>
#include <cstdlib>
#include <cmath>
#include <cstring>
#include <cctype>
#include <iostream>
#include <memory>
#include <fstream>
#include <vector>
#include <random>

#include "DynamicKomi.h"
#include "Gtp.h"
#include "GoBoard.h"
#include "Nakade.h"
#include "UctSearch.h"
#include "UctRating.h"
#include "Message.h"
#include "Point.h"
#include "Rating.h"
#include "Simulation.h"
#include "Utility.h"
#include "ZobristHash.h"
#include "TreeToJson.h"

using namespace std;

static GTP_command_t gtpcmd[GTP_COMMAND_NUM];

static char input[BUF_SIZE], input_copy[BUF_SIZE];
static char *next_token;

static char *brank, *err_command, *err_genmove, *err_play, *err_komi;

static int player_color = 0;

static game_info_t *game;

static game_info_t *game_prev;
static game_info_t *store_game;
static uct_node_t store_node;
static double store_winning_percentage;

static unique_ptr<ofstream> stream_ptr;
static bool sim_move = false;


////////////////////////
//  void SetSimMove() //
////////////////////////
void
SetSimMove( bool flag )
{
  sim_move = flag;
}

///////////////////////
//  void GTP_main()  //
///////////////////////
void
GTP_main( void )
{
  int i;

  game = AllocateGame();
  InitializeBoard(game);

  game_prev = AllocateGame();
  InitializeBoard(game_prev);
  store_game = AllocateGame();
  InitializeBoard(store_game);

  GTP_setCommand();
  GTP_message();

  while (true) {
    if (fgets(input, sizeof(input), stdin) == NULL) {
      if (feof(stdin))
        break;
      continue;
    }
    char *command;
    bool nocommand = true;

    STRCPY(input_copy, BUF_SIZE, input);
    command = STRTOK(input, DELIM, &next_token);
    CHOMP(command);

    for (i = 0; i < GTP_COMMAND_NUM; i++) {
      if (!strcmp(command, gtpcmd[i].command)) {
	StopPondering();
	(*gtpcmd[i].function)();
	nocommand = false;
	break;
      }
    }

    if (nocommand) {
      cout << err_command << endl << endl;
    }

    fflush(stdin);
    fflush(stdout);
  }
  cerr << "EXIT" << endl;
}


///////////////////////
//  GTPの出力の設定  //
///////////////////////
void
GTP_message( void )
{
  brank = STRDUP("");
  err_command = STRDUP("? unknown command");
  err_genmove = STRDUP("genmove color");
  err_play = STRDUP("play color point");
  err_komi = STRDUP("komi float");
}


/////////////////////////////
//  void GTP_setcommand()  //
/////////////////////////////
void
GTP_setCommand( void )
{
  gtpcmd[ 0].command = STRDUP("boardsize");
  gtpcmd[ 1].command = STRDUP("clear_board");
  gtpcmd[ 2].command = STRDUP("name");
  gtpcmd[ 3].command = STRDUP("protocol_version");
  gtpcmd[ 4].command = STRDUP("genmove");
  gtpcmd[ 5].command = STRDUP("play");
  gtpcmd[ 6].command = STRDUP("known_command");
  gtpcmd[ 7].command = STRDUP("list_commands");
  gtpcmd[ 8].command = STRDUP("quit");
  gtpcmd[ 9].command = STRDUP("komi");
  gtpcmd[10].command = STRDUP("get_komi");
  gtpcmd[11].command = STRDUP("final_score");
  gtpcmd[12].command = STRDUP("time_settings");
  gtpcmd[13].command = STRDUP("time_left");
  gtpcmd[14].command = STRDUP("version");
  gtpcmd[15].command = STRDUP("genmove_black");
  gtpcmd[16].command = STRDUP("genmove_white");
  gtpcmd[17].command = STRDUP("black");
  gtpcmd[18].command = STRDUP("white");
  gtpcmd[19].command = STRDUP("showboard");
  gtpcmd[20].command = STRDUP("final_status_list");
  gtpcmd[21].command = STRDUP("fixed_handicap");
  gtpcmd[22].command = STRDUP("place_free_handicap");
  gtpcmd[23].command = STRDUP("set_free_handicap");
  gtpcmd[24].command = STRDUP("kgs-genmove_cleanup");
  gtpcmd[25].command = STRDUP("features_planes_file");
  gtpcmd[26].command = STRDUP("_clear");
  gtpcmd[27].command = STRDUP("_store");
  gtpcmd[28].command = STRDUP("_dump");
  gtpcmd[29].command = STRDUP("_stat");
  gtpcmd[30].command = STRDUP("_tree");

  gtpcmd[ 0].function = GTP_boardsize;
  gtpcmd[ 1].function = GTP_clearboard;
  gtpcmd[ 2].function = GTP_name;
  gtpcmd[ 3].function = GTP_protocolversion;
  gtpcmd[ 4].function = GTP_genmove;
  gtpcmd[ 5].function = GTP_play;
  gtpcmd[ 6].function = GTP_knowncommand;
  gtpcmd[ 7].function = GTP_listcommands;
  gtpcmd[ 8].function = GTP_quit;
  gtpcmd[ 9].function = GTP_komi;
  gtpcmd[10].function = GTP_getkomi;
  gtpcmd[11].function = GTP_finalscore;
  gtpcmd[12].function = GTP_timesettings;
  gtpcmd[13].function = GTP_timeleft;
  gtpcmd[14].function = GTP_version;
  gtpcmd[15].function = GTP_genmove;
  gtpcmd[16].function = GTP_genmove;
  gtpcmd[17].function = GTP_play;
  gtpcmd[18].function = GTP_play;
  gtpcmd[19].function = GTP_showboard;
  gtpcmd[20].function = GTP_final_status_list;
  gtpcmd[21].function = GTP_fixed_handicap;
  gtpcmd[22].function = GTP_fixed_handicap;
  gtpcmd[23].function = GTP_set_free_handicap;
  gtpcmd[24].function = GTP_kgs_genmove_cleanup;
  gtpcmd[25].function = GTP_features_planes_file;
  gtpcmd[26].function = GTP_features_clear;
  gtpcmd[27].function = GTP_features_store;
  gtpcmd[28].function = GTP_features_planes_file;
  gtpcmd[29].function = GTP_stat_po;
  gtpcmd[30].function = GTP_tree;
}


/////////////////////////////////////////////////
//  void GTP_response(char *res, int success)  //
/////////////////////////////////////////////////
void
GTP_response( const char *res, bool success )
{
  if (success){
    cout << "= " << res << endl << endl;
  } else {
    if (res != NULL) {
      cerr << res << endl;
    }
    cout << "?" << endl << endl;
  }
}


/////////////////////////////
//　 void GTP_boardsize()  //
/////////////////////////////
void
GTP_boardsize( void )
{
#if defined (_WIN32)
  FILE *fp;
#endif
  char *command;
  int size;
  char buf[1024];
  
  command = STRTOK(NULL, DELIM, &next_token);

#if defined (_WIN32)
  sscanf_s(command, "%d", &size);
  sprintf_s(buf, 1024, " ");
#else
  sscanf(command, "%d", &size);
  sprintf(buf, " ");
#endif

  if (pure_board_size != size &&
      size <= PURE_BOARD_SIZE && size > 0) {
    SetBoardSize(size);
    SetParameter();
    SetNeighbor();
    InitializeNakadeHash();
  }

  FreeGame(game);
  game = AllocateGame();
  InitializeBoard(game);
  InitializeSearchSetting();
  InitializeUctHash();

  GTP_response(brank, true);
}
  

/////////////////////////////
//  void GTP_clearboard()  //
/////////////////////////////
void
GTP_clearboard( void )
{
#if defined (_WIN32)
  FILE *fp;
#endif
  player_color = 0;
  SetHandicapNum(0);
  FreeGame(game);
  game = AllocateGame();
  InitializeBoard(game);
  InitializeSearchSetting();
  InitializeUctHash();

  GTP_response(brank, true);
}
  

///////////////////////
//  void GTP_name()  //
///////////////////////
void
GTP_name( void )
{
  GTP_response(PROGRAM_NAME, true);
}


//////////////////////////////////
//  void GTP_protocolversion()  //
//////////////////////////////////
void
GTP_protocolversion( void )
{
  GTP_response(PROTOCOL_VERSION, true);
}


//////////////////////////
//  void GTP_genmove()  //
//////////////////////////
void
GTP_genmove( void )
{
  char *command;
  char c;
  char pos[10];
  int color;
  int point = PASS;
  
  command = STRTOK(input_copy, DELIM, &next_token);
  
  CHOMP(command);
  if (!strcmp("genmove_black", command)) {
    color = S_BLACK;
  } else if (!strcmp("genmove_white", command)) {
    color = S_WHITE;
  } else {
    command = STRTOK(NULL, DELIM, &next_token);
    if (command == NULL){
      GTP_response(err_genmove, true);
      return;
    }
    CHOMP(command);
    c = (char)tolower((int)command[0]);
    if (c == 'w') {
      color = S_WHITE;
    } else if (c == 'b') {
      color = S_BLACK;
    } else {
      GTP_response(err_genmove, true);
      return;
    }
  }

  player_color = color;

  if (sim_move)
    point = SimulationGenmove(game, color);
  else
    point = UctSearchGenmove(game, color);
  if (point != RESIGN) {
    PutStone(game, point, color);
  }
  
  IntegerToString(point, pos);
  
  GTP_response(pos, true);

  UctSearchPondering(game, FLIP_COLOR(color));
}


///////////////////////
//  void GTP_play()  //
///////////////////////
void
GTP_play( void )
{
  char *command;
  char c;
  int color, pos = 0;
  
  command = STRTOK(input_copy, DELIM, &next_token);

  if (!strcmp("black", command)){
    color = S_BLACK;
  } else if (!strcmp("white", command)){
    color = S_WHITE;
  } else{
    command = STRTOK(NULL, DELIM, &next_token);
    if (command == NULL){
      GTP_response(err_play, false);
      return;
    }
    CHOMP(command);
    c = (char)tolower((int)command[0]);
    if (c == 'w') {
      color = S_WHITE;
    } else{
      color = S_BLACK;
    }
  }

  command = STRTOK(NULL, DELIM, &next_token);
  
  CHOMP(command);
  if (command == NULL){
    GTP_response(err_play, false);
    return;
  } else {
    pos = StringToInteger(command);
  }

  CopyGame(game_prev, game);
  if (pos != RESIGN) {
    PutStone(game, pos, color);
  }
  PrintBoard(game);
  
  
  GTP_response(brank, true);
}
 

/////////////////////////////
// void GTP_knowncommand() //
/////////////////////////////
void
GTP_knowncommand( void )
{
  int i;
  char *command;
  
  command = STRTOK(NULL, DELIM, &next_token);
  
  if (command == NULL){
    GTP_response("known_command command", false);
    return;
  }
  CHOMP(command);
  for (i = 0; i < GTP_COMMAND_NUM; i++){
    if (!strcmp(command, gtpcmd[i].command)) {
      GTP_response("true", true);
      return;
    }
  }
  GTP_response("false", false);
}
 
 
///////////////////////////////
//  void GTP_listcommands()  //
///////////////////////////////
void
GTP_listcommands( void )
{
  char list[2048];
  int i, j;
  unsigned int k;

  i = 0;
  list[i++] = '\n';
  for (j = 0; j < GTP_COMMAND_NUM; j++) {
    for (k = 0; k < strlen(gtpcmd[j].command); k++){
      list[i++] = gtpcmd[j].command[k];
    }
    list[i++] = '\n';
  }
  list[i++] = '\0';

  GTP_response(list, true);
}
 
 
//////////////////////
// void GTP_quit()  //
//////////////////////
void
GTP_quit( void )
{
  FinalizeUctSearch();
  GTP_response(brank, true);
  exit(0);
}
 
 
///////////////////////
//  void GTP_komi()  //
///////////////////////
void
GTP_komi( void )
{
  char* c_komi;
  
  c_komi = STRTOK(NULL, DELIM, &next_token);

  if (c_komi != NULL) {
    SetKomi(atof(c_komi));
    PrintKomiValue();
    GTP_response(brank, true);
  } else {
    GTP_response(err_komi, false);
  }
}
 

//////////////////////////
//  void GTP_getkomi()  //
//////////////////////////
void
GTP_getkomi( void )
{
  char buf[256];
  
#if defined(_WIN32)
  sprintf_s(buf, 4, "%lf", komi[0]);
#else
  sprintf(buf, "%lf", komi[0]);
#endif
  GTP_response(buf, true);
}


/////////////////////////////
//  void GTP_finalscore()  //
/////////////////////////////
void
GTP_finalscore( void )
{
  char buf[10];
  double score = 0;
  
  score = UctAnalyze(game, S_BLACK) - komi[0];

#if defined(_WIN32)  
  if (score > 0) {
    sprintf_s(buf, 10, "B+%.1lf", score);
  } else {
    sprintf_s(buf, 10, "W+%.1lf", abs(score));
  }
#else
  if (score > 0) {
    sprintf(buf, "B+%.1lf", score);
  } else {
    sprintf(buf, "W+%.1lf", abs(score));
  }
#endif

  GTP_response(buf, true);
}
 

///////////////////////////////
//  void GTP_timesettings()  //
///////////////////////////////
void
GTP_timesettings( void )
{
  GTP_response(brank, true);
}


///////////////////////////
//  void GTP_timeleft()  //
///////////////////////////
void
GTP_timeleft( void )
{
  char *str1, *str2;

  str1 = STRTOK(NULL, DELIM, &next_token);
  str2 = STRTOK(NULL, DELIM, &next_token);

  
  if (str1[0] == 'B' || str1[0] == 'b'){
    remaining_time[S_BLACK] = atof(str2);
  } else if (str1[0] == 'W' || str1[0] == 'w'){
    remaining_time[S_WHITE] = atof(str2);
  }
  
  fprintf(stderr, "%f\n", remaining_time[S_BLACK]);
  fprintf(stderr, "%f\n", remaining_time[S_WHITE]);
  GTP_response(brank, true);
}


//////////////////////////
//  void GTP_version()  //
//////////////////////////
void
GTP_version( void )
{
  GTP_response(PROGRAM_VERSION, true);
}
 
 
////////////////////////////
//  void GTP_showboard()  //
////////////////////////////
void
GTP_showboard( void )
{
  PrintBoard(game);
  GTP_response(brank, true);
}


/////////////////////////////////
//  void GTP_fixed_handicap()  //
/////////////////////////////////
void
GTP_fixed_handicap( void )
{
  char *command;
  int num;
  char buf[1024];
  int handi[9];

  command = STRTOK(NULL, DELIM, &next_token);
  
#if defined (_WIN32)
  sscanf_s(command, "%d", &num);
  sprintf_s(buf, 1024, " ");
#else
  sscanf(command, "%d", &num);
  sprintf(buf, " ");
#endif

  handi[0] = POS(board_start + 3, board_start + 3);
  handi[1] = POS(board_start + 9, board_start + 3);
  handi[2] = POS(board_start + 15, board_start + 3);
  handi[3] = POS(board_start + 3, board_start + 9);
  handi[4] = POS(board_start + 9, board_start + 9);
  handi[5] = POS(board_start + 15, board_start + 9);
  handi[6] = POS(board_start + 3, board_start + 15);
  handi[7] = POS(board_start + 9, board_start + 15);
  handi[8] = POS(board_start + 15, board_start + 15);
  
  switch (num) {
    case 2:
      PutStone(game, handi[2], S_BLACK);
      PutStone(game, handi[6], S_BLACK);
#if defined (_WIN32)
      sprintf_s(buf, 1024, "%c%d %c%d",
		GOGUI_X(handi[2]), GOGUI_Y(handi[2]), GOGUI_X(handi[6]), GOGUI_Y(handi[6]));
#else
      sprintf(buf, "%c%d %c%d",
	      GOGUI_X(handi[2]), GOGUI_Y(handi[2]), GOGUI_X(handi[6]), GOGUI_Y(handi[6]));
#endif
      break;
    case 3:
      PutStone(game, handi[0], S_BLACK);
      PutStone(game, handi[2], S_BLACK);
      PutStone(game, handi[6], S_BLACK);
#if defined (_WIN32)
      sprintf_s(buf, 1024, "%c%d %c%d %c%d",
		GOGUI_X(handi[0]), GOGUI_Y(handi[0]), GOGUI_X(handi[2]), GOGUI_Y(handi[2]),
		GOGUI_X(handi[6]), GOGUI_Y(handi[6]));
#else
      sprintf(buf, "%c%d %c%d %c%d",
	      GOGUI_X(handi[0]), GOGUI_Y(handi[0]), GOGUI_X(handi[2]), GOGUI_Y(handi[2]),
	      GOGUI_X(handi[6]), GOGUI_Y(handi[6]));
#endif
      break;
    case 4:
      PutStone(game, handi[0], S_BLACK);
      PutStone(game, handi[2], S_BLACK);
      PutStone(game, handi[6], S_BLACK);
      PutStone(game, handi[8], S_BLACK);
#if defined (_WIN32)
      sprintf_s(buf, 1024, "%c%d %c%d %c%d %c%d",
		GOGUI_X(handi[0]), GOGUI_Y(handi[0]), GOGUI_X(handi[2]), GOGUI_Y(handi[2]),
		GOGUI_X(handi[6]), GOGUI_Y(handi[6]), GOGUI_X(handi[8]), GOGUI_Y(handi[8]));
#else
      sprintf(buf, "%c%d %c%d %c%d %c%d",
	      GOGUI_X(handi[0]), GOGUI_Y(handi[0]), GOGUI_X(handi[2]), GOGUI_Y(handi[2]),
	      GOGUI_X(handi[6]), GOGUI_Y(handi[6]), GOGUI_X(handi[8]), GOGUI_Y(handi[8]));
#endif
      break;
    case 5:
      PutStone(game, handi[0], S_BLACK);
      PutStone(game, handi[2], S_BLACK);
      PutStone(game, handi[4], S_BLACK);
      PutStone(game, handi[6], S_BLACK);
      PutStone(game, handi[8], S_BLACK);
#if defined (_WIN32)
      sprintf_s(buf, 1024, "%c%d %c%d %c%d %c%d %c%d", GOGUI_X(handi[0]), GOGUI_Y(handi[0]),
		GOGUI_X(handi[2]), GOGUI_Y(handi[2]), GOGUI_X(handi[4]), GOGUI_Y(handi[4]),
		GOGUI_X(handi[6]), GOGUI_Y(handi[6]), GOGUI_X(handi[8]), GOGUI_Y(handi[8]));
#else
      sprintf(buf, "%c%d %c%d %c%d %c%d %c%d", GOGUI_X(handi[0]), GOGUI_Y(handi[0]),
	      GOGUI_X(handi[2]), GOGUI_Y(handi[2]), GOGUI_X(handi[4]), GOGUI_Y(handi[4]),
	      GOGUI_X(handi[6]), GOGUI_Y(handi[6]), GOGUI_X(handi[8]), GOGUI_Y(handi[8]));
#endif
      break;
    case 6:
      PutStone(game, handi[0], S_BLACK);
      PutStone(game, handi[2], S_BLACK);
      PutStone(game, handi[3], S_BLACK);
      PutStone(game, handi[5], S_BLACK);
      PutStone(game, handi[6], S_BLACK);
      PutStone(game, handi[8], S_BLACK);
#if defined (_WIN32)
      sprintf_s(buf, 1024, "%c%d %c%d %c%d %c%d %c%d %c%d",
		GOGUI_X(handi[0]), GOGUI_Y(handi[0]), GOGUI_X(handi[2]), GOGUI_Y(handi[2]),
		GOGUI_X(handi[3]), GOGUI_Y(handi[3]), GOGUI_X(handi[5]), GOGUI_Y(handi[5]),
		GOGUI_X(handi[6]), GOGUI_Y(handi[6]), GOGUI_X(handi[8]), GOGUI_Y(handi[8]));
#else
      sprintf(buf, "%c%d %c%d %c%d %c%d %c%d %c%d",
	      GOGUI_X(handi[0]), GOGUI_Y(handi[0]), GOGUI_X(handi[2]), GOGUI_Y(handi[2]),
	      GOGUI_X(handi[3]), GOGUI_Y(handi[3]), GOGUI_X(handi[5]), GOGUI_Y(handi[5]),
	      GOGUI_X(handi[6]), GOGUI_Y(handi[6]), GOGUI_X(handi[8]), GOGUI_Y(handi[8]));
#endif
      break;
    case 7:
      PutStone(game, handi[0], S_BLACK);
      PutStone(game, handi[2], S_BLACK);
      PutStone(game, handi[3], S_BLACK);
      PutStone(game, handi[4], S_BLACK);
      PutStone(game, handi[5], S_BLACK);
      PutStone(game, handi[6], S_BLACK);
      PutStone(game, handi[8], S_BLACK);
#if defined (_WIN32)
      sprintf_s(buf, 1024, "%c%d %c%d %c%d %c%d %c%d %c%d %c%d",
		GOGUI_X(handi[0]), GOGUI_Y(handi[0]), GOGUI_X(handi[2]), GOGUI_Y(handi[2]),
		GOGUI_X(handi[3]), GOGUI_Y(handi[3]), GOGUI_X(handi[4]), GOGUI_Y(handi[4]),
		GOGUI_X(handi[5]), GOGUI_Y(handi[5]), GOGUI_X(handi[6]), GOGUI_Y(handi[6]),
		GOGUI_X(handi[8]), GOGUI_Y(handi[8]));
#else
      sprintf(buf, "%c%d %c%d %c%d %c%d %c%d %c%d %c%d",
	      GOGUI_X(handi[0]), GOGUI_Y(handi[0]), GOGUI_X(handi[2]), GOGUI_Y(handi[2]),
	      GOGUI_X(handi[3]), GOGUI_Y(handi[3]), GOGUI_X(handi[4]), GOGUI_Y(handi[4]),
	      GOGUI_X(handi[5]), GOGUI_Y(handi[5]), GOGUI_X(handi[6]), GOGUI_Y(handi[6]),
	      GOGUI_X(handi[8]), GOGUI_Y(handi[8]));
#endif
      break;
    case 8:
      PutStone(game, handi[0], S_BLACK);
      PutStone(game, handi[1], S_BLACK);
      PutStone(game, handi[2], S_BLACK);
      PutStone(game, handi[3], S_BLACK);
      PutStone(game, handi[5], S_BLACK);
      PutStone(game, handi[6], S_BLACK);
      PutStone(game, handi[7], S_BLACK);
      PutStone(game, handi[8], S_BLACK);
#if defined (_WIN32)
      sprintf_s(buf, 1024, "%c%d %c%d %c%d %c%d %c%d %c%d %c%d %c%d",
		GOGUI_X(handi[0]), GOGUI_Y(handi[0]), GOGUI_X(handi[1]), GOGUI_Y(handi[2]),
		GOGUI_X(handi[2]), GOGUI_Y(handi[2]), GOGUI_X(handi[3]), GOGUI_Y(handi[3]),
		GOGUI_X(handi[5]), GOGUI_Y(handi[5]), GOGUI_X(handi[6]), GOGUI_Y(handi[6]),
		GOGUI_X(handi[7]), GOGUI_Y(handi[7]), GOGUI_X(handi[8]), GOGUI_Y(handi[8]));
#else
      sprintf(buf, "%c%d %c%d %c%d %c%d %c%d %c%d %c%d %c%d",
	      GOGUI_X(handi[0]), GOGUI_Y(handi[0]), GOGUI_X(handi[1]), GOGUI_Y(handi[2]),
	      GOGUI_X(handi[2]), GOGUI_Y(handi[2]), GOGUI_X(handi[3]), GOGUI_Y(handi[3]),
	      GOGUI_X(handi[5]), GOGUI_Y(handi[5]), GOGUI_X(handi[6]), GOGUI_Y(handi[6]),
	      GOGUI_X(handi[7]), GOGUI_Y(handi[7]), GOGUI_X(handi[8]), GOGUI_Y(handi[8]));
#endif
      break;
    case 9:
      PutStone(game, handi[0], S_BLACK);
      PutStone(game, handi[1], S_BLACK);
      PutStone(game, handi[2], S_BLACK);
      PutStone(game, handi[3], S_BLACK);
      PutStone(game, handi[4], S_BLACK);
      PutStone(game, handi[5], S_BLACK);
      PutStone(game, handi[6], S_BLACK);
      PutStone(game, handi[7], S_BLACK);
      PutStone(game, handi[8], S_BLACK);
#if defined (_WIN32)
      sprintf_s(buf, 1024, "%c%d %c%d %c%d %c%d %c%d %c%d %c%d %c%d %c%d",
		GOGUI_X(handi[0]), GOGUI_Y(handi[0]), GOGUI_X(handi[1]), GOGUI_Y(handi[1]),
		GOGUI_X(handi[2]), GOGUI_Y(handi[2]), GOGUI_X(handi[3]), GOGUI_Y(handi[3]),
		GOGUI_X(handi[4]), GOGUI_Y(handi[4]), GOGUI_X(handi[5]), GOGUI_Y(handi[5]),
		GOGUI_X(handi[6]), GOGUI_Y(handi[6]), GOGUI_X(handi[7]), GOGUI_Y(handi[7]),
		GOGUI_X(handi[8]), GOGUI_Y(handi[8]));
#else
      sprintf(buf, "%c%d %c%d %c%d %c%d %c%d %c%d %c%d %c%d %c%d",
	      GOGUI_X(handi[0]), GOGUI_Y(handi[0]), GOGUI_X(handi[1]), GOGUI_Y(handi[1]),
	      GOGUI_X(handi[2]), GOGUI_Y(handi[2]), GOGUI_X(handi[3]), GOGUI_Y(handi[3]),
	      GOGUI_X(handi[4]), GOGUI_Y(handi[4]), GOGUI_X(handi[5]), GOGUI_Y(handi[5]),
	      GOGUI_X(handi[6]), GOGUI_Y(handi[6]), GOGUI_X(handi[7]), GOGUI_Y(handi[7]),
	      GOGUI_X(handi[8]), GOGUI_Y(handi[8]));
#endif
      break;
  }
  SetKomi(0.5);
  SetHandicapNum(num);
  GTP_response(buf, true);
}


////////////////////////////////////
//  void GTP_set_free_handicap()  //
////////////////////////////////////
void
GTP_set_free_handicap( void )
{
  char *command;
  int pos, num = 0;
  
  while (1){
    command = STRTOK(NULL, DELIM, &next_token);

    if (command == NULL){
      SetHandicapNum(num);
      SetKomi(0.5);
      GTP_response(brank, true);
      return;
    }
    
    pos = StringToInteger(command);
    
    if (pos > 0 && pos < board_max && IsLegal(game, pos, S_BLACK)) {
      PutStone(game, pos, S_BLACK);
      num++;
    }
  }
}


////////////////////////////////////
//  void GTP_final_status_list()  //
////////////////////////////////////
void
GTP_final_status_list( void )
{
  char dead[2048] = { 0 };
  char pos[5];
  int owner[BOARD_MAX]; 
  int x, y;
  char *command;
  
  OwnerCopy(owner);
  
  command = STRTOK(NULL, DELIM, &next_token);

  CHOMP(command);
  
  if (!strcmp(command, "dead")){
    for (y = board_start; y <= board_end; y++) {
      for (x = board_start; x <= board_end; x++) {
	if ((game->board[POS(x, y)] == player_color && owner[POS(x, y)] <= 30) ||
	    (game->board[POS(x, y)] == FLIP_COLOR(player_color) && owner[POS(x, y)] >= 70)) {
#if defined (_WIN32)
	  sprintf_s(pos, 5, "%c%d ", GOGUI_X(POS(x, y)), GOGUI_Y(POS(x, y)));
	  strcat_s(dead, 2048, pos);
#else
	  sprintf(pos, "%c%d ", GOGUI_X(POS(x, y)), GOGUI_Y(POS(x, y)));
	  strcat(dead, pos);
#endif
	}
      }
    }
  } else if (!strcmp(command, "alive")){
    for (y = board_start; y <= board_end; y++) {
      for (x = board_start; x <= board_end; x++) {
	if ((game->board[POS(x, y)] == player_color && owner[POS(x, y)] >= 70) ||
	    (game->board[POS(x, y)] == FLIP_COLOR(player_color) && owner[POS(x, y)] <= 30)) {
#if defined (_WIN32)
	  sprintf_s(pos, 5, "%c%d ", GOGUI_X(POS(x, y)), GOGUI_Y(POS(x, y)));
	  strcat_s(dead, 2048, pos);
#else
	  sprintf(pos, "%c%d ", GOGUI_X(POS(x, y)), GOGUI_Y(POS(x, y)));
	  strcat(dead, pos);
#endif
	}
      }
    }
  }
  
  GTP_response(dead, true);
}


//////////////////////////////////////
//  void GTP_kgs_genmove_cleanup()  //
//////////////////////////////////////
void
GTP_kgs_genmove_cleanup( void )
{
  char *command;
  char c;
  char pos[10];
  int color;
  int point = PASS;
  
  command = STRTOK(input_copy, DELIM, &next_token);
  
  CHOMP(command);
  if (!strcmp("genmove_black", command)) {
    color = S_BLACK;
  } else if (!strcmp("genmove_white", command)) {
    color = S_WHITE;
  } else {
    command = STRTOK(NULL, DELIM, &next_token);
    if (command == NULL){
      GTP_response(err_genmove, true);
      return;
    }
    CHOMP(command);
    c = (char)tolower((int)command[0]);
    if (c == 'w') {
      color = S_WHITE;
    } else if (c == 'b') {
      color = S_BLACK;
    } else {
      GTP_response(err_genmove, true);
      return;
    }
  }
  
  player_color = color;
  
  point = UctSearchGenmoveCleanUp(game, color);
  if (point != RESIGN) {
    PutStone(game, point, color);
  }
  
  IntegerToString(point, pos);
  
  GTP_response(pos, true);
}
 
static int features_turn_count = 0;
static int features_turn_next = 1;
void DumpFeature(const uct_node_t& node, int color, int move, int win);

void GTP_features_planes_file(void)
{
  char *command;

  command = STRTOK(NULL, DELIM, &next_token);
  if (command == NULL || game->moves == 0) {
    GTP_response(err_genmove, true);
    return;
  }

  int color = game->record[game->moves - 1].color;
  int move = game->record[game->moves - 1].pos;

  CHOMP(command);
  char c = (char)tolower((int)command[0]);
  int win;
  if (c == 'w') {
    win = color == S_WHITE ? 1 : -1;
#if 0
    if (store_winning_percentage > 0.40) {
      cerr << "####### SKIP " << c << " " << store_winning_percentage << endl;
      //GTP_response(brank, true);
      //return;
    } else {
      cerr << "####### DUMP " << c << " " << store_winning_percentage << endl;
    }
#endif
  } else if (c == 'b') {
    win = color == S_BLACK ? 1 : -1;
#if 0
    if (store_winning_percentage < 0.60) {
      cerr << "####### SKIP " << c << " " << store_winning_percentage << endl;
      //GTP_response(brank, true);
      //return;
    } else {
      cerr << "####### DUMP " << c << " " << store_winning_percentage << endl;
    }
#endif
  } else {
    GTP_response(err_genmove, true);
    return;
  }

  if (move == RESIGN || move == PASS) {
    GTP_response(brank, true);
    return;
  }

  DumpFeature(store_node, color, move, win);

  GTP_response(brank, true);
}

void DumpFeature(const uct_node_t& node, int color, int move, int win)
{
  double rate[PURE_BOARD_MAX];
  AnalyzePoRating(game_prev, color, rate);

  std::vector<float> data, data2;
  int moveT;
  int t = rand() / 11 % 8;
  //static int t = 0; t++;
  WritePlanes(data, &data2, game_prev, &node, move, &moveT, color, t);

  int x = CORRECT_X(moveT) - 1;
  int y = CORRECT_Y(moveT) - 1;
  int label = x + y * pure_board_size;
  if (label < 0 || label > 19 * 19) {
    cerr << "bad label " << x << " " << y << endl;
    abort();
  }
  if (isnan(data2[0])) {
    cerr << "bad stat" << endl;
    return;
  }

  if (!stream_ptr) {
    stream_ptr = make_unique<ofstream>("data.txt", std::ios::app | std::ios::binary);
  }
  auto& stream = *stream_ptr;

  stream << "|win " << win;
  stream << "|move " << label << ":1";
  stream << "|features ";
  //stream << '\n';
  for (auto i = 0; i < data.size(); i++) {
    //if (i % 19 == 0) stream << '\n';
    float f = data[i];
    if (f != 0) {
      stream << i << ':' << f << ' ';
    }
  }
#if 1
  stream << "|statistic ";
  for (auto i = 0; i < data2.size(); i++) {
    stream << data2[i] << ' ';
    /*
    float f = data2[i];
    if (f != 0) {
      stream << i << ':' << f << ' ';
    }
    */
  }
#endif
  stream << endl;
}

void GTP_features_clear(void)
{
  GTP_response(brank, true);
  return;
}

void GTP_features_store(void)
{
  char *command;

  int color = FLIP_COLOR(game->record[game->moves - 1].color);

  CopyGame(store_game, game);
  UctSearchStat(store_game, color, 100);

  const uct_node_t *root = &uct_node[current_root];
  memcpy(&store_node, root, sizeof(uct_node_t));
  double winning_percentage = (double)root->win / root->move_count;
  if (color == S_BLACK) {
    store_winning_percentage = winning_percentage;
  } else {
    store_winning_percentage = 1 - winning_percentage;
  }

  GTP_response(brank, true);
}


///////////////////////
//  void GTP_stat()  //
///////////////////////
void
GTP_stat(void)
{
  char *command;
  char c;
  char pos[10];
  int color;
  int point = PASS;

  command = STRTOK(input_copy, DELIM, &next_token);

  CHOMP(command);
  command = STRTOK(NULL, DELIM, &next_token);
  if (command == NULL) {
    GTP_response(err_genmove, true);
    return;
  }
  CHOMP(command);
  c = (char)tolower((int)command[0]);
  if (c == 'w') {
    color = S_WHITE;
  } else if (c == 'b') {
    color = S_BLACK;
  } else {
    GTP_response(err_genmove, true);
    return;
  }

  player_color = FLIP_COLOR(game->record[game->moves - 1].color);

  int win = player_color == color ? 1 : 0;

  //player_color = color;

  cerr << "VALUE";
  double value_sum = 0;
  for (int i = 0; i < 8; i++) {
    std::vector<float> data, data2;
    int moveT;
    int t = i;// rand() / 11 % 8;
	      //static int t = 0; t++;
    WritePlanes(data, &data2, game, &store_node, POS(10, 11), &moveT, player_color, t);

    std::vector<int> eval_node_index;
    std::vector<int> eval_node_color;
    std::vector<int> eval_node_trans;
    std::vector<int> eval_node_path;
    //std::vector<float> eval_input_data;

    eval_node_index.push_back(current_root);
    eval_node_color.push_back(player_color);
    eval_node_trans.push_back(t);
    //std::copy(req.data.begin(), req.data.end(), std::back_inserter(eval_input_data));
    //std::copy(req.path.rbegin(), req.path.rend(), std::back_inserter(eval_node_path));
    eval_node_path.push_back(-1);
#if 0
    //TODO
    uct_node[current_root].value = -1;
    EvalUctNode(eval_node_index, eval_node_color, eval_node_trans, data, eval_node_path);
    double value = uct_node[current_root].value;
    double se_value = abs(value - win);
    cerr << '\t' << se_value;
    value_sum += uct_node[current_root].value;
    uct_node[current_root].value = -1;
    //<< '\t' << data[19 * 3 + 3] << ':' << POS(10, 11) << ':' << moveT;
#endif
  }
  cerr << endl;

  point = UctSearchGenmove(game, player_color);
  if (point != RESIGN) {
    //PutStone(game, point, color);
  }


  uct_node_t *root = &uct_node[current_root];
  double winning_percentage = (double)root->win / root->move_count;
  //double value = root->value;
  double valuet = (double)root->value_win / root->value_move_count;
  double se_po = abs(winning_percentage - win);
  //double se_value = abs(value - win);
  double se_valuet = abs(valuet - win);
  double se_value8 = abs(value_sum / 8 - win);
  cerr << "STAT\t" << game->moves << "\t" << winning_percentage
    //<< "\t" << value
    << "\t" << se_po
    //<< "\t" << se_value
    << '\t' << se_value8
    << '\t' << root->value_move_count
    << '\t' << se_valuet
    << endl;


  IntegerToString(point, pos);

  GTP_response(pos, true);

  //UctSearchPondering(game, FLIP_COLOR(color));
}



////////////////////////////////
//  シミュレーションの検証        //
////////////////////////////////
void
GTP_stat_po(void)
{
  char *command;
  //char c;
  //char pos[10];
  //int color;
  //int point = PASS;

  command = STRTOK(input_copy, DELIM, &next_token);

  CHOMP(command);
  command = STRTOK(NULL, DELIM, &next_token);
  if (command == NULL) {
    GTP_response(err_genmove, true);
    return;
  }
  /*
  CHOMP(command);
  c = (char)tolower((int)command[0]);
  if (c == 'w') {
    color = S_WHITE;
  } else if (c == 'b') {
    color = S_BLACK;
  } else {
    GTP_response(err_genmove, true);
    return;
  }
  */

  int color = game->record[game->moves - 1].color;
  int move = game->record[game->moves - 1].pos;
  //void
  //Simulation(game_info_t *game, int starting_color, std::mt19937_64 *mt)
  //{
    //int color = starting_color;
  int pos = -1;
  int length;
  int pass_count;

  // レートの初期化  
  game_prev->sum_rate[0] = game_prev->sum_rate[1] = 0;
  memset(game_prev->sum_rate_row, 0, sizeof(long long) * 2 * BOARD_SIZE);
  memset(game_prev->rate, 0, sizeof(long long) * 2 * BOARD_MAX);

  pass_count = (game_prev->record[game_prev->moves - 1].pos == PASS && game_prev->moves > 1);


  auto begin_time = ray_clock::now();
  for (int i = 0; i < 1000; i++) {
    // 黒番のレートの計算
    Rating(game_prev, S_BLACK, &game_prev->sum_rate[0], game_prev->sum_rate_row[0], game_prev->rate[0]);
    // 白番のレートの計算
    Rating(game_prev, S_WHITE, &game_prev->sum_rate[1], game_prev->sum_rate_row[1], game_prev->rate[1]);
  }
  auto finish_time = GetSpendTime(begin_time) * 1000;

  long long *rate = game_prev->rate[color - 1];

  long long max_rate = 0;
  int max_pos = PASS;
  for (int i = 0; i < pure_board_max; i++) {
    int pos = onboard_pos[i];

    if (IsLegalNotEye(game_prev, pos, color)) {
      long long r = rate[pos];
      if (r > max_rate) {
	max_rate = r;
	max_pos = pos;
      }
    }
  }

  if (max_pos == move) {
    cerr << "####### PO HIT " << finish_time << endl;
  } else {
    cerr << "####### PO MISS " << finish_time << endl;
  }
#if 0
  // 終局まで対局をシミュレート
  while (length-- && pass_count < 2) {
    // 着手を生成する
    pos = RatingMove(game, color, mt);
    // 石を置く
    PoPutStone(game, pos, color);
    // パスの確認
    pass_count = (pos == PASS) ? (pass_count + 1) : 0;
    // 手番の入れ替え
    color = FLIP_COLOR(color);
  }
#endif
}

void GTP_tree(void) {
  TreeToJson(uct_node, current_root);
}