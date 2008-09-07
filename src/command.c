/* the Music Player Daemon (MPD)
 * Copyright (C) 2003-2007 by Warren Dukes (warren.dukes@gmail.com)
 * This project's homepage is: http://www.musicpd.org
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
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA
 */

#include "command.h"
#include "player_control.h"
#include "playlist.h"
#include "ls.h"
#include "directory.h"
#include "volume.h"
#include "stats.h"
#include "myfprintf.h"
#include "list.h"
#include "permission.h"
#include "buffer2array.h"
#include "log.h"
#include "utils.h"
#include "storedPlaylist.h"
#include "sllist.h"
#include "ack.h"
#include "audio.h"
#include "dbUtils.h"
#include "tag.h"
#include "client.h"
#include "tag_print.h"
#include "os_compat.h"

#define COMMAND_PLAY           	"play"
#define COMMAND_PLAYID         	"playid"
#define COMMAND_STOP           	"stop"
#define COMMAND_PAUSE          	"pause"
#define COMMAND_STATUS         	"status"
#define COMMAND_KILL           	"kill"
#define COMMAND_CLOSE          	"close"
#define COMMAND_ADD            	"add"
#define COMMAND_ADDID		"addid"
#define COMMAND_DELETE         	"delete"
#define COMMAND_DELETEID       	"deleteid"
#define COMMAND_PLAYLIST       	"playlist"
#define COMMAND_SHUFFLE        	"shuffle"
#define COMMAND_CLEAR          	"clear"
#define COMMAND_SAVE           	"save"
#define COMMAND_LOAD           	"load"
#define COMMAND_LISTPLAYLIST   	"listplaylist"
#define COMMAND_LISTPLAYLISTINFO   	"listplaylistinfo"
#define COMMAND_LSINFO         	"lsinfo"
#define COMMAND_RM             	"rm"
#define COMMAND_PLAYLISTINFO   	"playlistinfo"
#define COMMAND_PLAYLISTID   	"playlistid"
#define COMMAND_FIND           	"find"
#define COMMAND_SEARCH         	"search"
#define COMMAND_UPDATE         	"update"
#define COMMAND_NEXT           	"next"
#define COMMAND_PREVIOUS       	"previous"
#define COMMAND_LISTALL        	"listall"
#define COMMAND_VOLUME         	"volume"
#define COMMAND_REPEAT         	"repeat"
#define COMMAND_RANDOM         	"random"
#define COMMAND_STATS          	"stats"
#define COMMAND_CLEAR_ERROR    	"clearerror"
#define COMMAND_LIST           	"list"
#define COMMAND_MOVE           	"move"
#define COMMAND_MOVEID         	"moveid"
#define COMMAND_SWAP           	"swap"
#define COMMAND_SWAPID      	"swapid"
#define COMMAND_SEEK           	"seek"
#define COMMAND_SEEKID         	"seekid"
#define COMMAND_LISTALLINFO	"listallinfo"
#define COMMAND_PING		"ping"
#define COMMAND_SETVOL		"setvol"
#define COMMAND_PASSWORD	"password"
#define COMMAND_CROSSFADE	"crossfade"
#define COMMAND_URL_HANDLERS   	"urlhandlers"
#define COMMAND_PLCHANGES	"plchanges"
#define COMMAND_PLCHANGESPOSID	"plchangesposid"
#define COMMAND_CURRENTSONG	"currentsong"
#define COMMAND_ENABLE_DEV	"enableoutput"
#define COMMAND_DISABLE_DEV	"disableoutput"
#define COMMAND_DEVICES		"outputs"
#define COMMAND_COMMANDS	"commands"
#define COMMAND_NOTCOMMANDS	"notcommands"
#define COMMAND_PLAYLISTCLEAR   "playlistclear"
#define COMMAND_PLAYLISTADD	"playlistadd"
#define COMMAND_PLAYLISTFIND	"playlistfind"
#define COMMAND_PLAYLISTSEARCH	"playlistsearch"
#define COMMAND_PLAYLISTMOVE	"playlistmove"
#define COMMAND_PLAYLISTDELETE	"playlistdelete"
#define COMMAND_TAGTYPES	"tagtypes"
#define COMMAND_COUNT		"count"
#define COMMAND_RENAME		"rename"

#define COMMAND_STATUS_VOLUME           "volume"
#define COMMAND_STATUS_STATE            "state"
#define COMMAND_STATUS_REPEAT           "repeat"
#define COMMAND_STATUS_RANDOM           "random"
#define COMMAND_STATUS_PLAYLIST         "playlist"
#define COMMAND_STATUS_PLAYLIST_LENGTH  "playlistlength"
#define COMMAND_STATUS_SONG             "song"
#define COMMAND_STATUS_SONGID           "songid"
#define COMMAND_STATUS_TIME             "time"
#define COMMAND_STATUS_BITRATE          "bitrate"
#define COMMAND_STATUS_ERROR            "error"
#define COMMAND_STATUS_CROSSFADE	"xfade"
#define COMMAND_STATUS_AUDIO		"audio"
#define COMMAND_STATUS_UPDATING_DB	"updating_db"

/*
 * The most we ever use is for search/find, and that limits it to the
 * number of tags we can have.  Add one for the command, and one extra
 * to catch errors clients may send us
 */
#define COMMAND_ARGV_MAX	(2+(TAG_NUM_OF_ITEM_TYPES*2))

typedef struct _CommandEntry CommandEntry;

typedef int (*CommandHandlerFunction) (struct client *, int *, int, char **);
typedef int (*CommandListHandlerFunction)
 (struct client *, int *, int, char **, struct strnode *, CommandEntry *);

/* if min: -1 don't check args *
 * if max: -1 no max args      */
struct _CommandEntry {
	const char *cmd;
	int min;
	int max;
	int reqPermission;
	CommandHandlerFunction handler;
	CommandListHandlerFunction listHandler;
};

/* this should really be "need a non-negative integer": */
static const char need_positive[] = "need a positive integer"; /* no-op */

/* FIXME: redundant error messages */
static const char check_integer[] = "\"%s\" is not a integer";
static const char need_integer[] = "need an integer";
static const char check_boolean[] = "\"%s\" is not 0 or 1";
static const char check_non_negative[] = "\"%s\" is not an integer >= 0";

static const char *current_command;
static int command_listNum;

static CommandEntry *getCommandEntryFromString(char *string, int *permission);

static List *commandList;

static CommandEntry *newCommandEntry(void)
{
	CommandEntry *cmd = xmalloc(sizeof(CommandEntry));
	cmd->cmd = NULL;
	cmd->min = 0;
	cmd->max = 0;
	cmd->handler = NULL;
	cmd->listHandler = NULL;
	cmd->reqPermission = 0;
	return cmd;
}

static void command_error_va(int fd, int error, const char *fmt, va_list args)
{
	if (current_command && fd != STDERR_FILENO) {
		fdprintf(fd, "ACK [%i@%i] {%s} ",
		         (int)error, command_listNum, current_command);
		vfdprintf(fd, fmt, args);
		fdprintf(fd, "\n");
		current_command = NULL;
	} else {
		fdprintf(STDERR_FILENO, "ACK [%i@%i] ",
		         (int)error, command_listNum);
		vfdprintf(STDERR_FILENO, fmt, args);
		fdprintf(STDERR_FILENO, "\n");
	}
}

void command_success(struct client *client)
{
	client_puts(client, "OK\n");
}

static void command_error_v(struct client *client, int error,
			    const char *fmt, va_list args)
{
	assert(client != NULL);
	assert(current_command != NULL);

	client_printf(client, "ACK [%i@%i] {%s} ",
		      (int)error, command_listNum, current_command);
	client_vprintf(client, fmt, args);
	client_puts(client, "\n");

	current_command = NULL;
}

mpd_fprintf_ void command_error(struct client *client, int error,
				const char *fmt, ...)
{
	va_list args;
	va_start(args, fmt);
	command_error_v(client, error, fmt, args);
	va_end(args);
}

static int mpd_fprintf__ check_uint32(struct client *client, mpd_uint32 *dst,
                                      const char *s, const char *fmt, ...)
{
	char *test;

	*dst = strtoul(s, &test, 10);
	if (*test != '\0') {
		va_list args;
		va_start(args, fmt);
		command_error_v(client, ACK_ERROR_ARG, fmt, args);
		va_end(args);
		return -1;
	}
	return 0;
}

static int mpd_fprintf__ check_int(struct client *client, int *dst,
                                   const char *s, const char *fmt, ...)
{
	char *test;

	*dst = strtol(s, &test, 10);
	if (*test != '\0' ||
	    (fmt == check_boolean && *dst != 0 && *dst != 1) ||
	    (fmt == check_non_negative && *dst < 0)) {
		va_list args;
		va_start(args, fmt);
		command_error_v(client, ACK_ERROR_ARG, fmt, args);
		va_end(args);
		return -1;
	}
	return 0;
}

static int print_playlist_result(struct client *client,
				 enum playlist_result result)
{
	switch (result) {
	case PLAYLIST_RESULT_SUCCESS:
		return 0;

	case PLAYLIST_RESULT_ERRNO:
		command_error(client, ACK_ERROR_SYSTEM, strerror(errno));
		return -1;

	case PLAYLIST_RESULT_NO_SUCH_SONG:
		command_error(client, ACK_ERROR_NO_EXIST, "No such song");
		return -1;

	case PLAYLIST_RESULT_NO_SUCH_LIST:
		command_error(client, ACK_ERROR_NO_EXIST, "No such playlist");
		return -1;

	case PLAYLIST_RESULT_LIST_EXISTS:
		command_error(client, ACK_ERROR_NO_EXIST,
			      "Playlist already exists");
		return -1;

	case PLAYLIST_RESULT_BAD_NAME:
		command_error(client, ACK_ERROR_ARG,
			      "playlist name is invalid: "
			      "playlist names may not contain slashes,"
			      " newlines or carriage returns");
		return -1;

	case PLAYLIST_RESULT_BAD_RANGE:
		command_error(client, ACK_ERROR_ARG, "Bad song index");
		return -1;

	case PLAYLIST_RESULT_NOT_PLAYING:
		command_error(client, ACK_ERROR_PLAYER_SYNC, "Not playing");
		return -1;

	case PLAYLIST_RESULT_TOO_LARGE:
		command_error(client, ACK_ERROR_PLAYLIST_MAX,
			      "playlist is at the max size");
		return -1;
	}

	assert(0);
}

static void addCommand(const char *name,
		       int reqPermission,
		       int minargs,
		       int maxargs,
		       CommandHandlerFunction handler_func,
		       CommandListHandlerFunction listHandler_func)
{
	CommandEntry *cmd = newCommandEntry();
	cmd->cmd = name;
	cmd->min = minargs;
	cmd->max = maxargs;
	cmd->handler = handler_func;
	cmd->listHandler = listHandler_func;
	cmd->reqPermission = reqPermission;

	insertInList(commandList, cmd->cmd, cmd);
}

static int handleUrlHandlers(struct client *client, mpd_unused int *permission,
			     mpd_unused int argc, mpd_unused char *argv[])
{
	return printRemoteUrlHandlers(client_get_fd(client));
}

static int handleTagTypes(struct client *client, mpd_unused int *permission,
			  mpd_unused int argc, mpd_unused char *argv[])
{
	tag_print_types(client);
	return 0;
}

static int handlePlay(struct client *client, mpd_unused int *permission,
		      int argc, char *argv[])
{
	int song = -1;
	enum playlist_result result;

	if (argc == 2 && check_int(client, &song, argv[1], need_positive) < 0)
		return -1;
	result = playPlaylist(song, 0);
	return print_playlist_result(client, result);
}

static int handlePlayId(struct client *client, mpd_unused int *permission,
			int argc, char *argv[])
{
	int id = -1;
	enum playlist_result result;

	if (argc == 2 && check_int(client, &id, argv[1], need_positive) < 0)
		return -1;

	result = playPlaylistById(id, 0);
	return print_playlist_result(client, result);
}

static int handleStop(mpd_unused struct client *client, mpd_unused int *permission,
		      mpd_unused int argc, mpd_unused char *argv[])
{
	stopPlaylist();
	return 0;
}

static int handleCurrentSong(struct client *client, mpd_unused int *permission,
			     mpd_unused int argc, mpd_unused char *argv[])
{
	int song = getPlaylistCurrentSong();
	enum playlist_result result;

	if (song < 0)
		return 0;

	result = playlistInfo(client, song);
	return print_playlist_result(client, result);
}

static int handlePause(struct client *client, mpd_unused int *permission,
		       int argc, char *argv[])
{
	if (argc == 2) {
		int pause_flag;
		if (check_int(client, &pause_flag, argv[1], check_boolean, argv[1]) < 0)
			return -1;
		playerSetPause(pause_flag);
		return 0;
	}

	playerPause();
	return 0;
}

static int commandStatus(struct client *client, mpd_unused int *permission,
			 mpd_unused int argc, mpd_unused char *argv[])
{
	const char *state = NULL;
	int updateJobId;
	int song;

	playPlaylistIfPlayerStopped();
	switch (getPlayerState()) {
	case PLAYER_STATE_STOP:
		state = COMMAND_STOP;
		break;
	case PLAYER_STATE_PAUSE:
		state = COMMAND_PAUSE;
		break;
	case PLAYER_STATE_PLAY:
		state = COMMAND_PLAY;
		break;
	}

	client_printf(client,
		      "%s: %i\n"
		      "%s: %i\n"
		      "%s: %i\n"
		      "%s: %li\n"
		      "%s: %i\n"
		      "%s: %i\n"
		      "%s: %s\n",
		      COMMAND_STATUS_VOLUME, getVolumeLevel(),
		      COMMAND_STATUS_REPEAT, getPlaylistRepeatStatus(),
		      COMMAND_STATUS_RANDOM, getPlaylistRandomStatus(),
		      COMMAND_STATUS_PLAYLIST, getPlaylistVersion(),
		      COMMAND_STATUS_PLAYLIST_LENGTH, getPlaylistLength(),
		      COMMAND_STATUS_CROSSFADE,
		      (int)(getPlayerCrossFade() + 0.5),
		      COMMAND_STATUS_STATE, state);

	song = getPlaylistCurrentSong();
	if (song >= 0) {
		client_printf(client, "%s: %i\n%s: %i\n",
			      COMMAND_STATUS_SONG, song,
			      COMMAND_STATUS_SONGID, getPlaylistSongId(song));
	}
	if (getPlayerState() != PLAYER_STATE_STOP) {
		client_printf(client,
			      "%s: %i:%i\n"
			      "%s: %li\n"
			      "%s: %u:%i:%i\n",
			      COMMAND_STATUS_TIME,
			      getPlayerElapsedTime(), getPlayerTotalTime(),
			      COMMAND_STATUS_BITRATE, getPlayerBitRate(),
			      COMMAND_STATUS_AUDIO,
			      getPlayerSampleRate(), getPlayerBits(),
			      getPlayerChannels());
	}

	if ((updateJobId = isUpdatingDB())) {
		client_printf(client, "%s: %i\n",
			      COMMAND_STATUS_UPDATING_DB, updateJobId);
	}

	if (getPlayerError() != PLAYER_ERROR_NOERROR) {
		client_printf(client, "%s: %s\n",
			      COMMAND_STATUS_ERROR, getPlayerErrorStr());
	}

	return 0;
}

static int handleKill(mpd_unused struct client *client, mpd_unused int *permission,
		      mpd_unused int argc, mpd_unused char *argv[])
{
	return COMMAND_RETURN_KILL;
}

static int handleClose(mpd_unused struct client *client, mpd_unused int *permission,
		       mpd_unused int argc, mpd_unused char *argv[])
{
	return COMMAND_RETURN_CLOSE;
}

static int handleAdd(struct client *client, mpd_unused int *permission,
		     mpd_unused int argc, char *argv[])
{
	char *path = argv[1];
	enum playlist_result result;

	if (isRemoteUrl(path))
		return addToPlaylist(path, NULL);

	result = addAllIn(path);
	if (result == (enum playlist_result)-1) {
		command_error(client, ACK_ERROR_NO_EXIST,
			      "directory or file not found");
		return -1;
	}

	return print_playlist_result(client, result);
}

static int handleAddId(struct client *client, mpd_unused int *permission,
		       int argc, char *argv[])
{
	int added_id;
	enum playlist_result result = addToPlaylist(argv[1], &added_id);

	if (result == PLAYLIST_RESULT_SUCCESS)
		return result;

	if (argc == 3) {
		int to;
		if (check_int(client, &to, argv[2],
			      check_integer, argv[2]) < 0)
			return -1;
		result = moveSongInPlaylistById(added_id, to);
		if (result != PLAYLIST_RESULT_SUCCESS) {
			int ret = print_playlist_result(client, result);
			deleteFromPlaylistById(added_id);
			return ret;
		}
	}

	client_printf(client, "Id: %d\n", added_id);
	return result;
}

static int handleDelete(struct client *client, mpd_unused int *permission,
			mpd_unused int argc, char *argv[])
{
	int song;
	enum playlist_result result;

	if (check_int(client, &song, argv[1], need_positive) < 0)
		return -1;

	result = deleteFromPlaylist(song);
	return print_playlist_result(client, result);
}

static int handleDeleteId(struct client *client, mpd_unused int *permission,
			  mpd_unused int argc, char *argv[])
{
	int id;
	enum playlist_result result;

	if (check_int(client, &id, argv[1], need_positive) < 0)
		return -1;

	result = deleteFromPlaylistById(id);
	return print_playlist_result(client, result);
}

static int handlePlaylist(struct client *client, mpd_unused int *permission,
			  mpd_unused int argc, mpd_unused char *argv[])
{
	showPlaylist(client_get_fd(client));
	return 0;
}

static int handleShuffle(mpd_unused struct client *client,
			 mpd_unused int *permission,
			 mpd_unused int argc, mpd_unused char *argv[])
{
	shufflePlaylist();
	return 0;
}

static int handleClear(mpd_unused struct client *client, mpd_unused int *permission,
		       mpd_unused int argc, mpd_unused char *argv[])
{
	clearPlaylist();
	return 0;
}

static int handleSave(struct client *client, mpd_unused int *permission,
		      mpd_unused int argc, char *argv[])
{
	enum playlist_result result;

	result = savePlaylist(argv[1]);
	return print_playlist_result(client, result);
}

static int handleLoad(struct client *client, mpd_unused int *permission,
		      mpd_unused int argc, char *argv[])
{
	enum playlist_result result;

	result = loadPlaylist(client_get_fd(client), argv[1]);
	return print_playlist_result(client, result);
}

static int handleListPlaylist(struct client *client, mpd_unused int *permission,
			      mpd_unused int argc, char *argv[])
{
	int ret;

	ret = PlaylistInfo(client, argv[1], 0);
	if (ret == -1)
		command_error(client, ACK_ERROR_NO_EXIST, "No such playlist");

	return ret;
}

static int handleListPlaylistInfo(struct client *client, mpd_unused int *permission,
				  mpd_unused int argc, char *argv[])
{
	int ret;

	ret = PlaylistInfo(client, argv[1], 1);
	if (ret == -1)
		command_error(client, ACK_ERROR_NO_EXIST, "No such playlist");

	return ret;
}

static int handleLsInfo(struct client *client, mpd_unused int *permission,
			int argc, char *argv[])
{
	const char *path = "";

	if (argc == 2)
		path = argv[1];

	if (printDirectoryInfo(client, path) < 0) {
		command_error(client, ACK_ERROR_NO_EXIST,
			      "directory not found");
		return -1;
	}

	if (isRootDirectory(path))
		return lsPlaylists(client_get_fd(client), path);

	return 0;
}

static int handleRm(struct client *client, mpd_unused int *permission,
		    mpd_unused int argc, char *argv[])
{
	enum playlist_result result;

	result = deletePlaylist(argv[1]);
	return print_playlist_result(client, result);
}

static int handleRename(struct client *client, mpd_unused int *permission,
			mpd_unused int argc, char *argv[])
{
	enum playlist_result result;

	result = renameStoredPlaylist(argv[1], argv[2]);
	return print_playlist_result(client, result);
}

static int handlePlaylistChanges(struct client *client, mpd_unused int *permission,
				 mpd_unused int argc, char *argv[])
{
	mpd_uint32 version;

	if (check_uint32(client, &version, argv[1], need_positive) < 0)
		return -1;
	return playlistChanges(client, version);
}

static int handlePlaylistChangesPosId(struct client *client, mpd_unused int *permission,
				      mpd_unused int argc, char *argv[])
{
	mpd_uint32 version;

	if (check_uint32(client, &version, argv[1], need_positive) < 0)
		return -1;
	return playlistChangesPosId(client_get_fd(client), version);
}

static int handlePlaylistInfo(struct client *client, mpd_unused int *permission,
			      int argc, char *argv[])
{
	int song = -1;
	enum playlist_result result;

	if (argc == 2 && check_int(client, &song, argv[1], need_positive) < 0)
		return -1;

	result = playlistInfo(client, song);
	return print_playlist_result(client, result);
}

static int handlePlaylistId(struct client *client, mpd_unused int *permission,
			    int argc, char *argv[])
{
	int id = -1;
	enum playlist_result result;

	if (argc == 2 && check_int(client, &id, argv[1], need_positive) < 0)
		return -1;

	result = playlistId(client, id);
	return print_playlist_result(client, result);
}

static int handleFind(struct client *client, mpd_unused int *permission,
		      int argc, char *argv[])
{
	int ret;

	LocateTagItem *items;
	int numItems = newLocateTagItemArrayFromArgArray(argv + 1,
							 argc - 1,
							 &items);

	if (numItems <= 0) {
		command_error(client, ACK_ERROR_ARG, "incorrect arguments");
		return -1;
	}

	ret = findSongsIn(client, NULL, numItems, items);
	if (ret == -1)
		command_error(client, ACK_ERROR_NO_EXIST,
			      "directory or file not found");

	freeLocateTagItemArray(numItems, items);

	return ret;
}

static int handleSearch(struct client *client, mpd_unused int *permission,
			int argc, char *argv[])
{
	int ret;

	LocateTagItem *items;
	int numItems = newLocateTagItemArrayFromArgArray(argv + 1,
							 argc - 1,
							 &items);

	if (numItems <= 0) {
		command_error(client, ACK_ERROR_ARG, "incorrect arguments");
		return -1;
	}

	ret = searchForSongsIn(client, NULL, numItems, items);
	if (ret == -1)
		command_error(client, ACK_ERROR_NO_EXIST,
			      "directory or file not found");

	freeLocateTagItemArray(numItems, items);

	return ret;
}

static int handleCount(struct client *client, mpd_unused int *permission,
		       int argc, char *argv[])
{
	int ret;

	LocateTagItem *items;
	int numItems = newLocateTagItemArrayFromArgArray(argv + 1,
							 argc - 1,
							 &items);

	if (numItems <= 0) {
		command_error(client, ACK_ERROR_ARG, "incorrect arguments");
		return -1;
	}

	ret = searchStatsForSongsIn(client, NULL, numItems, items);
	if (ret == -1)
		command_error(client, ACK_ERROR_NO_EXIST,
			      "directory or file not found");

	freeLocateTagItemArray(numItems, items);

	return ret;
}

static int handlePlaylistFind(struct client *client, mpd_unused int *permission,
			      int argc, char *argv[])
{
	LocateTagItem *items;
	int numItems = newLocateTagItemArrayFromArgArray(argv + 1,
							 argc - 1,
							 &items);

	if (numItems <= 0) {
		command_error(client, ACK_ERROR_ARG, "incorrect arguments");
		return -1;
	}

	findSongsInPlaylist(client, numItems, items);

	freeLocateTagItemArray(numItems, items);

	return 0;
}

static int handlePlaylistSearch(struct client *client, mpd_unused int *permission,
				int argc, char *argv[])
{
	LocateTagItem *items;
	int numItems = newLocateTagItemArrayFromArgArray(argv + 1,
							 argc - 1,
							 &items);

	if (numItems <= 0) {
		command_error(client, ACK_ERROR_ARG, "incorrect arguments");
		return -1;
	}

	searchForSongsInPlaylist(client, numItems, items);

	freeLocateTagItemArray(numItems, items);

	return 0;
}

static int handlePlaylistDelete(struct client *client, mpd_unused int *permission,
				mpd_unused int argc, char *argv[]) {
	char *playlist = argv[1];
	int from;
	enum playlist_result result;

	if (check_int(client, &from, argv[2], check_integer, argv[2]) < 0)
		return -1;

	result = removeOneSongFromStoredPlaylistByPath(playlist, from);
	return print_playlist_result(client, result);
}

static int handlePlaylistMove(struct client *client, mpd_unused int *permission,
			      mpd_unused mpd_unused int argc, char *argv[])
{
	char *playlist = argv[1];
	int from, to;
	enum playlist_result result;

	if (check_int(client, &from, argv[2], check_integer, argv[2]) < 0)
		return -1;
	if (check_int(client, &to, argv[3], check_integer, argv[3]) < 0)
		return -1;

	result = moveSongInStoredPlaylistByPath(playlist, from, to);
	return print_playlist_result(client, result);
}

static int listHandleUpdate(struct client *client,
			    mpd_unused int *permission,
			    mpd_unused int argc,
			    char *argv[],
			    struct strnode *cmdnode, CommandEntry * cmd)
{
	static List *pathList;
	CommandEntry *nextCmd = NULL;
	struct strnode *next = cmdnode->next;

	if (!pathList)
		pathList = makeList(NULL, 1);

	if (argc == 2)
		insertInList(pathList, argv[1], NULL);
	else
		insertInList(pathList, "", NULL);

	if (next)
		nextCmd = getCommandEntryFromString(next->data, permission);

	if (cmd != nextCmd) {
		int ret = updateInit(pathList);
		freeList(pathList);
		pathList = NULL;

		switch (ret) {
		case 0:
			command_error(client, ACK_ERROR_UPDATE_ALREADY,
				      "already updating");
			break;

		case -1:
			command_error(client, ACK_ERROR_SYSTEM,
				      "problems trying to update");
			break;

		default:
			client_printf(client, "updating_db: %i\n", ret);
			ret = 0;
			break;
		}

		return ret;
	}

	return 0;
}

static int handleUpdate(struct client *client, mpd_unused int *permission,
			mpd_unused int argc, char *argv[])
{
	int ret;

	if (argc == 2) {
		List *pathList = makeList(NULL, 1);
		insertInList(pathList, argv[1], NULL);
		ret = updateInit(pathList);
		freeList(pathList);
	} else
		ret = updateInit(NULL);

	switch (ret) {
	case 0:
		command_error(client, ACK_ERROR_UPDATE_ALREADY,
			      "already updating");
		ret = -1;
		break;

	case -1:
		command_error(client, ACK_ERROR_SYSTEM,
			      "problems trying to update");
		break;

	default:
		client_printf(client, "updating_db: %i\n", ret);
		ret = 0;
		break;
	}

	return ret;
}

static int handleNext(mpd_unused struct client *client, mpd_unused int *permission,
		      mpd_unused int argc, mpd_unused char *argv[])
{
	nextSongInPlaylist();
	return 0;
}

static int handlePrevious(mpd_unused struct client *client, mpd_unused int *permission,
			  mpd_unused int argc, mpd_unused char *argv[])
{
	previousSongInPlaylist();
	return 0;
}

static int handleListAll(struct client *client, mpd_unused int *permission,
			 mpd_unused int argc, char *argv[])
{
	char *directory = NULL;
	int ret;

	if (argc == 2)
		directory = argv[1];

	ret = printAllIn(client, directory);
	if (ret == -1)
		command_error(client, ACK_ERROR_NO_EXIST,
			      "directory or file not found");

	return ret;
}

static int handleVolume(struct client *client, mpd_unused int *permission,
			mpd_unused int argc, char *argv[])
{
	int change, ret;

	if (check_int(client, &change, argv[1], need_integer) < 0)
		return -1;

	ret = changeVolumeLevel(change, 1);
	if (ret == -1)
		command_error(client, ACK_ERROR_SYSTEM,
			      "problems setting volume");

	return ret;
}

static int handleSetVol(struct client *client, mpd_unused int *permission,
			mpd_unused int argc, char *argv[])
{
	int level, ret;

	if (check_int(client, &level, argv[1], need_integer) < 0)
		return -1;

	ret = changeVolumeLevel(level, 0);
	if (ret == -1)
		command_error(client, ACK_ERROR_SYSTEM,
			      "problems setting volume");

	return ret;
}

static int handleRepeat(struct client *client, mpd_unused int *permission,
			mpd_unused int argc, char *argv[])
{
	int status;

	if (check_int(client, &status, argv[1], need_integer) < 0)
		return -1;

	if (status != 0 && status != 1) {
		command_error(client, ACK_ERROR_ARG,
			      "\"%i\" is not 0 or 1", status);
		return -1;
	}

	setPlaylistRepeatStatus(status);
	return 0;
}

static int handleRandom(struct client *client, mpd_unused int *permission,
			mpd_unused int argc, char *argv[])
{
	int status;

	if (check_int(client, &status, argv[1], need_integer) < 0)
		return -1;

	if (status != 0 && status != 1) {
		command_error(client, ACK_ERROR_ARG,
			      "\"%i\" is not 0 or 1", status);
		return -1;
	}

	setPlaylistRandomStatus(status);
	return 0;
}

static int handleStats(struct client *client, mpd_unused int *permission,
		       mpd_unused int argc, mpd_unused char *argv[])
{
	return printStats(client_get_fd(client));
}

static int handleClearError(mpd_unused struct client *client, mpd_unused int *permission,
			    mpd_unused int argc, mpd_unused char *argv[])
{
	clearPlayerError();
	return 0;
}

static int handleList(struct client *client, mpd_unused int *permission,
		      int argc, char *argv[])
{
	int numConditionals;
	LocateTagItem *conditionals = NULL;
	int tagType = getLocateTagItemType(argv[1]);
	int ret;

	if (tagType < 0) {
		command_error(client, ACK_ERROR_ARG, "\"%s\" is not known", argv[1]);
		return -1;
	}

	if (tagType == LOCATE_TAG_ANY_TYPE) {
		command_error(client, ACK_ERROR_ARG,
			      "\"any\" is not a valid return tag type");
		return -1;
	}

	/* for compatibility with < 0.12.0 */
	if (argc == 3) {
		if (tagType != TAG_ITEM_ALBUM) {
			command_error(client, ACK_ERROR_ARG,
				      "should be \"%s\" for 3 arguments",
				      mpdTagItemKeys[TAG_ITEM_ALBUM]);
			return -1;
		}
		conditionals = newLocateTagItem(mpdTagItemKeys[TAG_ITEM_ARTIST],
						argv[2]);
		numConditionals = 1;
	} else {
		numConditionals =
		    newLocateTagItemArrayFromArgArray(argv + 2,
						      argc - 2, &conditionals);

		if (numConditionals < 0) {
			command_error(client, ACK_ERROR_ARG,
				      "not able to parse args");
			return -1;
		}
	}

	ret = listAllUniqueTags(client, tagType, numConditionals, conditionals);

	if (conditionals)
		freeLocateTagItemArray(numConditionals, conditionals);

	if (ret == -1)
		command_error(client, ACK_ERROR_NO_EXIST,
			      "directory or file not found");

	return ret;
}

static int handleMove(struct client *client, mpd_unused int *permission,
		      mpd_unused int argc, char *argv[])
{
	int from, to;
	enum playlist_result result;

	if (check_int(client, &from, argv[1], check_integer, argv[1]) < 0)
		return -1;
	if (check_int(client, &to, argv[2], check_integer, argv[2]) < 0)
		return -1;
	result = moveSongInPlaylist(from, to);
	return print_playlist_result(client, result);
}

static int handleMoveId(struct client *client, mpd_unused int *permission,
			mpd_unused int argc, char *argv[])
{
	int id, to;
	enum playlist_result result;

	if (check_int(client, &id, argv[1], check_integer, argv[1]) < 0)
		return -1;
	if (check_int(client, &to, argv[2], check_integer, argv[2]) < 0)
		return -1;
	result = moveSongInPlaylistById(id, to);
	return print_playlist_result(client, result);
}

static int handleSwap(struct client *client, mpd_unused int *permission,
		      mpd_unused int argc, char *argv[])
{
	int song1, song2;
	enum playlist_result result;

	if (check_int(client, &song1, argv[1], check_integer, argv[1]) < 0)
		return -1;
	if (check_int(client, &song2, argv[2], check_integer, argv[2]) < 0)
		return -1;
	result = swapSongsInPlaylist(song1, song2);
	return print_playlist_result(client, result);
}

static int handleSwapId(struct client *client, mpd_unused int *permission,
			mpd_unused int argc, char *argv[])
{
	int id1, id2;
	enum playlist_result result;

	if (check_int(client, &id1, argv[1], check_integer, argv[1]) < 0)
		return -1;
	if (check_int(client, &id2, argv[2], check_integer, argv[2]) < 0)
		return -1;
	result = swapSongsInPlaylistById(id1, id2);
	return print_playlist_result(client, result);
}

static int handleSeek(struct client *client, mpd_unused int *permission,
		      mpd_unused int argc, char *argv[])
{
	int song, seek_time;
	enum playlist_result result;

	if (check_int(client, &song, argv[1], check_integer, argv[1]) < 0)
		return -1;
	if (check_int(client, &seek_time, argv[2], check_integer, argv[2]) < 0)
		return -1;

	result = seekSongInPlaylist(song, seek_time);
	return print_playlist_result(client, result);
}

static int handleSeekId(struct client *client, mpd_unused int *permission,
			mpd_unused int argc, char *argv[])
{
	int id, seek_time;
	enum playlist_result result;

	if (check_int(client, &id, argv[1], check_integer, argv[1]) < 0)
		return -1;
	if (check_int(client, &seek_time, argv[2], check_integer, argv[2]) < 0)
		return -1;

	result = seekSongInPlaylistById(id, seek_time);
	return print_playlist_result(client, result);
}

static int handleListAllInfo(struct client *client, mpd_unused int *permission,
			     mpd_unused int argc, char *argv[])
{
	char *directory = NULL;
	int ret;

	if (argc == 2)
		directory = argv[1];

	ret = printInfoForAllIn(client, directory);
	if (ret == -1)
		command_error(client, ACK_ERROR_NO_EXIST,
			      "directory or file not found");

	return ret;
}

static int handlePing(mpd_unused struct client *client, mpd_unused int *permission,
		      mpd_unused int argc, mpd_unused char *argv[])
{
	return 0;
}

static int handlePassword(struct client *client, mpd_unused int *permission,
			  mpd_unused int argc, char *argv[])
{
	if (getPermissionFromPassword(argv[1], permission) < 0) {
		command_error(client, ACK_ERROR_PASSWORD, "incorrect password");
		return -1;
	}

	return 0;
}

static int handleCrossfade(struct client *client, mpd_unused int *permission,
			   mpd_unused int argc, char *argv[])
{
	int xfade_time;

	if (check_int(client, &xfade_time, argv[1], check_non_negative, argv[1]) < 0)
		return -1;
	setPlayerCrossFade(xfade_time);

	return 0;
}

static int handleEnableDevice(struct client *client, mpd_unused int *permission,
			      mpd_unused int argc, char *argv[])
{
	int device, ret;

	if (check_int(client, &device, argv[1], check_non_negative, argv[1]) < 0)
		return -1;

	ret = enableAudioDevice(device);
	if (ret == -1)
		command_error(client, ACK_ERROR_NO_EXIST,
			      "No such audio output");

	return ret;
}

static int handleDisableDevice(struct client *client, mpd_unused int *permission,
			       mpd_unused int argc, char *argv[])
{
	int device, ret;

	if (check_int(client, &device, argv[1], check_non_negative, argv[1]) < 0)
		return -1;

	ret = disableAudioDevice(device);
	if (ret == -1)
		command_error(client, ACK_ERROR_NO_EXIST,
			      "No such audio output");

	return ret;
}

static int handleDevices(struct client *client, mpd_unused int *permission,
			 mpd_unused int argc, mpd_unused char *argv[])
{
	printAudioDevices(client_get_fd(client));

	return 0;
}

/* don't be fooled, this is the command handler for "commands" command */
static int handleCommands(struct client *client, mpd_unused int *permission,
			  mpd_unused int argc, mpd_unused char *argv[])
{
	ListNode *node = commandList->firstNode;
	CommandEntry *cmd;

	while (node != NULL) {
		cmd = (CommandEntry *) node->data;
		if (cmd->reqPermission == (*permission & cmd->reqPermission)) {
			client_printf(client, "command: %s\n", cmd->cmd);
		}

		node = node->nextNode;
	}

	return 0;
}

static int handleNotcommands(struct client *client, mpd_unused int *permission,
			     mpd_unused int argc, mpd_unused char *argv[])
{
	ListNode *node = commandList->firstNode;
	CommandEntry *cmd;

	while (node != NULL) {
		cmd = (CommandEntry *) node->data;

		if (cmd->reqPermission != (*permission & cmd->reqPermission)) {
			client_printf(client, "command: %s\n", cmd->cmd);
		}

		node = node->nextNode;
	}

	return 0;
}

static int handlePlaylistClear(struct client *client, mpd_unused int *permission,
			       mpd_unused int argc, char *argv[])
{
	enum playlist_result result;

	result = clearStoredPlaylist(argv[1]);
	return print_playlist_result(client, result);
}

static int handlePlaylistAdd(struct client *client, mpd_unused int *permission,
			     mpd_unused int argc, char *argv[])
{
	char *playlist = argv[1];
	char *path = argv[2];
	enum playlist_result result;

	if (isRemoteUrl(path))
		result = addToStoredPlaylist(path, playlist);
	else
		result = addAllInToStoredPlaylist(path, playlist);

	if (result == (enum playlist_result)-1) {
		command_error(client, ACK_ERROR_NO_EXIST,
			      "directory or file not found");
		return -1;
	}

	return print_playlist_result(client, result);
}

void initCommands(void)
{
	commandList = makeList(free, 1);

	/* addCommand(name,                  permission,         min, max, handler,                    list handler); */
	addCommand(COMMAND_PLAY,             PERMISSION_CONTROL, 0,   1,   handlePlay,                 NULL);
	addCommand(COMMAND_PLAYID,           PERMISSION_CONTROL, 0,   1,   handlePlayId,               NULL);
	addCommand(COMMAND_STOP,             PERMISSION_CONTROL, 0,   0,   handleStop,                 NULL);
	addCommand(COMMAND_CURRENTSONG,      PERMISSION_READ,    0,   0,   handleCurrentSong,          NULL);
	addCommand(COMMAND_PAUSE,            PERMISSION_CONTROL, 0,   1,   handlePause,                NULL);
	addCommand(COMMAND_STATUS,           PERMISSION_READ,    0,   0,   commandStatus,              NULL);
	addCommand(COMMAND_KILL,             PERMISSION_ADMIN,   -1,  -1,  handleKill,                 NULL);
	addCommand(COMMAND_CLOSE,            PERMISSION_NONE,    -1,  -1,  handleClose,                NULL);
	addCommand(COMMAND_ADD,              PERMISSION_ADD,     1,   1,   handleAdd,                  NULL);
	addCommand(COMMAND_ADDID,            PERMISSION_ADD,     1,   2,   handleAddId,                NULL);
	addCommand(COMMAND_DELETE,           PERMISSION_CONTROL, 1,   1,   handleDelete,               NULL);
	addCommand(COMMAND_DELETEID,         PERMISSION_CONTROL, 1,   1,   handleDeleteId,             NULL);
	addCommand(COMMAND_PLAYLIST,         PERMISSION_READ,    0,   0,   handlePlaylist,             NULL);
	addCommand(COMMAND_PLAYLISTID,       PERMISSION_READ,    0,   1,   handlePlaylistId,           NULL);
	addCommand(COMMAND_SHUFFLE,          PERMISSION_CONTROL, 0,   0,   handleShuffle,              NULL);
	addCommand(COMMAND_CLEAR,            PERMISSION_CONTROL, 0,   0,   handleClear,                NULL);
	addCommand(COMMAND_SAVE,             PERMISSION_CONTROL, 1,   1,   handleSave,                 NULL);
	addCommand(COMMAND_LOAD,             PERMISSION_ADD,     1,   1,   handleLoad,                 NULL);
	addCommand(COMMAND_LISTPLAYLIST,     PERMISSION_READ,    1,   1,   handleListPlaylist,         NULL);
	addCommand(COMMAND_LISTPLAYLISTINFO, PERMISSION_READ,    1,   1,   handleListPlaylistInfo,     NULL);
	addCommand(COMMAND_LSINFO,           PERMISSION_READ,    0,   1,   handleLsInfo,               NULL);
	addCommand(COMMAND_RM,               PERMISSION_CONTROL, 1,   1,   handleRm,                   NULL);
	addCommand(COMMAND_PLAYLISTINFO,     PERMISSION_READ,    0,   1,   handlePlaylistInfo,         NULL);
	addCommand(COMMAND_FIND,             PERMISSION_READ,    2,   -1,  handleFind,                 NULL);
	addCommand(COMMAND_SEARCH,           PERMISSION_READ,    2,   -1,  handleSearch,               NULL);
	addCommand(COMMAND_UPDATE,           PERMISSION_ADMIN,   0,   1,   handleUpdate,               listHandleUpdate);
	addCommand(COMMAND_NEXT,             PERMISSION_CONTROL, 0,   0,   handleNext,                 NULL);
	addCommand(COMMAND_PREVIOUS,         PERMISSION_CONTROL, 0,   0,   handlePrevious,             NULL);
	addCommand(COMMAND_LISTALL,          PERMISSION_READ,    0,   1,   handleListAll,              NULL);
	addCommand(COMMAND_VOLUME,           PERMISSION_CONTROL, 1,   1,   handleVolume,               NULL);
	addCommand(COMMAND_REPEAT,           PERMISSION_CONTROL, 1,   1,   handleRepeat,               NULL);
	addCommand(COMMAND_RANDOM,           PERMISSION_CONTROL, 1,   1,   handleRandom,               NULL);
	addCommand(COMMAND_STATS,            PERMISSION_READ,    0,   0,   handleStats,                NULL);
	addCommand(COMMAND_CLEAR_ERROR,      PERMISSION_CONTROL, 0,   0,   handleClearError,           NULL);
	addCommand(COMMAND_LIST,             PERMISSION_READ,    1,   -1,  handleList,                 NULL);
	addCommand(COMMAND_MOVE,             PERMISSION_CONTROL, 2,   2,   handleMove,                 NULL);
	addCommand(COMMAND_MOVEID,           PERMISSION_CONTROL, 2,   2,   handleMoveId,               NULL);
	addCommand(COMMAND_SWAP,             PERMISSION_CONTROL, 2,   2,   handleSwap,                 NULL);
	addCommand(COMMAND_SWAPID,           PERMISSION_CONTROL, 2,   2,   handleSwapId,               NULL);
	addCommand(COMMAND_SEEK,             PERMISSION_CONTROL, 2,   2,   handleSeek,                 NULL);
	addCommand(COMMAND_SEEKID,           PERMISSION_CONTROL, 2,   2,   handleSeekId,               NULL);
	addCommand(COMMAND_LISTALLINFO,      PERMISSION_READ,    0,   1,   handleListAllInfo,          NULL);
	addCommand(COMMAND_PING,             PERMISSION_NONE,    0,   0,   handlePing,                 NULL);
	addCommand(COMMAND_SETVOL,           PERMISSION_CONTROL, 1,   1,   handleSetVol,               NULL);
	addCommand(COMMAND_PASSWORD,         PERMISSION_NONE,    1,   1,   handlePassword,             NULL);
	addCommand(COMMAND_CROSSFADE,        PERMISSION_CONTROL, 1,   1,   handleCrossfade,            NULL);
	addCommand(COMMAND_URL_HANDLERS,     PERMISSION_READ,    0,   0,   handleUrlHandlers,          NULL);
	addCommand(COMMAND_PLCHANGES,        PERMISSION_READ,    1,   1,   handlePlaylistChanges,      NULL);
	addCommand(COMMAND_PLCHANGESPOSID,   PERMISSION_READ,    1,   1,   handlePlaylistChangesPosId, NULL);
	addCommand(COMMAND_ENABLE_DEV,       PERMISSION_ADMIN,   1,   1,   handleEnableDevice,         NULL);
	addCommand(COMMAND_DISABLE_DEV,      PERMISSION_ADMIN,   1,   1,   handleDisableDevice,        NULL);
	addCommand(COMMAND_DEVICES,          PERMISSION_READ,    0,   0,   handleDevices,              NULL);
	addCommand(COMMAND_COMMANDS,         PERMISSION_NONE,    0,   0,   handleCommands,             NULL);
	addCommand(COMMAND_NOTCOMMANDS,      PERMISSION_NONE,    0,   0,   handleNotcommands,          NULL);
	addCommand(COMMAND_PLAYLISTCLEAR,    PERMISSION_CONTROL, 1,   1,   handlePlaylistClear,        NULL);
	addCommand(COMMAND_PLAYLISTADD,      PERMISSION_CONTROL, 2,   2,   handlePlaylistAdd,          NULL);
	addCommand(COMMAND_PLAYLISTFIND,     PERMISSION_READ,    2,   -1,  handlePlaylistFind,         NULL);
	addCommand(COMMAND_PLAYLISTSEARCH,   PERMISSION_READ,    2,   -1,  handlePlaylistSearch,       NULL);
	addCommand(COMMAND_PLAYLISTMOVE,     PERMISSION_CONTROL, 3,   3,   handlePlaylistMove,         NULL);
	addCommand(COMMAND_PLAYLISTDELETE,   PERMISSION_CONTROL, 2,   2,   handlePlaylistDelete,       NULL);
	addCommand(COMMAND_TAGTYPES,         PERMISSION_READ,    0,   0,   handleTagTypes,             NULL);
	addCommand(COMMAND_COUNT,            PERMISSION_READ,    2,   -1,  handleCount,                NULL);
	addCommand(COMMAND_RENAME,           PERMISSION_CONTROL, 2,   2,   handleRename,               NULL);

	sortList(commandList);
}

void finishCommands(void)
{
	freeList(commandList);
}

static int checkArgcAndPermission(CommandEntry * cmd, struct client *client,
				  int permission, int argc, char *argv[])
{
	int min = cmd->min + 1;
	int max = cmd->max + 1;

	if (cmd->reqPermission != (permission & cmd->reqPermission)) {
		if (client != NULL)
			command_error(client, ACK_ERROR_PERMISSION,
				      "you don't have permission for \"%s\"",
				      cmd->cmd);
		return -1;
	}

	if (min == 0)
		return 0;

	if (min == max && max != argc) {
		if (client != NULL)
			command_error(client, ACK_ERROR_ARG,
				      "wrong number of arguments for \"%s\"",
				      argv[0]);
		return -1;
	} else if (argc < min) {
		if (client != NULL)
			command_error(client, ACK_ERROR_ARG,
				      "too few arguments for \"%s\"", argv[0]);
		return -1;
	} else if (argc > max && max /* != 0 */ ) {
		if (client != NULL)
			command_error(client, ACK_ERROR_ARG,
				      "too many arguments for \"%s\"", argv[0]);
		return -1;
	} else
		return 0;
}

static CommandEntry *getCommandEntryAndCheckArgcAndPermission(struct client *client,
							      int *permission,
							      int argc,
							      char *argv[])
{
	static char unknown[] = "";
	CommandEntry *cmd;

	current_command = unknown;

	if (argc == 0)
		return NULL;

	if (!findInList(commandList, argv[0], (void *)&cmd)) {
		if (client != NULL)
			command_error(client, ACK_ERROR_UNKNOWN,
				      "unknown command \"%s\"", argv[0]);
		return NULL;
	}

	current_command = cmd->cmd;

	if (checkArgcAndPermission(cmd, client, *permission, argc, argv) < 0) {
		return NULL;
	}

	return cmd;
}

static CommandEntry *getCommandEntryFromString(char *string, int *permission)
{
	CommandEntry *cmd;
	char *argv[COMMAND_ARGV_MAX] = { NULL };
	int argc = buffer2array(string, argv, COMMAND_ARGV_MAX);

	if (0 == argc)
		return NULL;

	cmd = getCommandEntryAndCheckArgcAndPermission(0, permission,
						       argc, argv);

	return cmd;
}

static int processCommandInternal(struct client *client,
				  mpd_unused int *permission,
				  char *commandString, struct strnode *cmdnode)
{
	int argc;
	char *argv[COMMAND_ARGV_MAX] = { NULL };
	CommandEntry *cmd;
	int ret = -1;

	argc = buffer2array(commandString, argv, COMMAND_ARGV_MAX);

	if (argc == 0)
		return 0;

	if ((cmd = getCommandEntryAndCheckArgcAndPermission(client, permission,
							    argc, argv))) {
		if (!cmdnode || !cmd->listHandler) {
			ret = cmd->handler(client, permission, argc, argv);
		} else {
			ret = cmd->listHandler(client, permission, argc, argv,
					       cmdnode, cmd);
		}
	}

	current_command = NULL;

	return ret;
}

int processListOfCommands(struct client *client, int *permission,
			  int listOK, struct strnode *list)
{
	struct strnode *cur = list;
	int ret = 0;

	command_listNum = 0;

	while (cur) {
		DEBUG("processListOfCommands: process command \"%s\"\n",
		      cur->data);
		ret = processCommandInternal(client, permission, cur->data, cur);
		DEBUG("processListOfCommands: command returned %i\n", ret);
		if (ret != 0 || client_is_expired(client))
			goto out;
		else if (listOK)
			client_puts(client, "list_OK\n");
		command_listNum++;
		cur = cur->next;
	}
out:
	command_listNum = 0;
	return ret;
}

int processCommand(struct client *client, int *permission, char *commandString)
{
	return processCommandInternal(client, permission, commandString, NULL);
}

mpd_fprintf_ void commandError(int fd, int error, const char *fmt, ...)
{
	va_list args;
	va_start(args, fmt);
	command_error_va(fd, error, fmt, args);
	va_end(args);
}
