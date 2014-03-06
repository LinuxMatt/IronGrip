/*
 * IronGrip is a frontend for Audio CD ripping and encoding.
 * Copyright(C) 2012 Matt Thirtytwo <matt59491@gmail.com>
 *
 * IronGrip's Homepage: https://github.com/LinuxMatt/IronGrip
 *
 * --------------------------------------------------------------------
 * IronGrip is a fork of the Asunder project.
 * See README.md file for details.
 * Original copyright of Asunder:
 * Copyright(C) 2005 Eric Lathrop
 * Copyright(C) 2007 Andrew Smith
 * Asunder website: http://littlesvr.ca/asunder/
 * --------------------------------------------------------------------
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
 * You should have received a copy of the GNU General Public License along
 * with this program; if not, write to the Free Software Foundation, Inc.,
 * 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA.
 *
*/

#ifdef HAVE_CONFIG_H
#  include <config.h>
#endif
#include <assert.h>
#include <cddb/cddb.h>
#include <ctype.h>
#include <dlfcn.h>
#include <errno.h>
#include <fcntl.h>
#include <gdk/gdkkeysyms.h>
#include <glib.h>
#include <glib/gprintf.h>
#include <gtk/gtk.h>
#include <libgen.h>
#include <limits.h>
#include <linux/cdrom.h>
#include <pthread.h>
#include <pwd.h>
#include <signal.h>
#include <stdarg.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/ioctl.h>
#include <sys/stat.h>
#include <sys/timeb.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <unistd.h>
#ifdef ENABLE_NLS
#  include <libintl.h>
#  undef _
#  define _(String) dgettext (PACKAGE, String)
#  ifdef gettext_noop
#    define N_(String) gettext_noop (String)
#  else
#    define N_(String) (String)
#  endif
#else
#  define textdomain(String) (String)
#  define gettext(String) (String)
#  define dgettext(Domain,Message) (Message)
#  define dcgettext(Domain,Message,Type) (Message)
#  define bindtextdomain(Domain,Directory) (Domain)
#  define _(String) (String)
#  define N_(String) (String)
#endif

#include "irongrip.icon.h"
#include "license.gpl.h"

#define AUTHOR "Matt Thirtytwo <matt59491@gmail.com>"
#define HOMEPAGE "https://github.com/LinuxMatt/IronGrip"

#define DEFAULT_PROXY_PORT 8080
#define DEFAULT_CDDB_SERVER "freedb.freedb.org"
#define DEFAULT_CDDB_SERVER_PORT 8880
#define COMPLETION_MAX 4000
#define COMPLETION_NAME_KEY "completion_name"

// Characters that should not be present in filenames.
#define	BADCHARS	"/?*|><:\"\\"

#define UTF8 "UTF-8"
#define STR_UNKNOWN "unknown"
#define STR_TEXT "text"
#define PIXMAP_PATH  PACKAGE_DATA_DIR "/" PIXMAPDIR
#define DEFAULT_LOG_FILE "/tmp/irongrip.log"
#define LAME_PRG "lame"
#define OGGENC_PRG "oggenc"
#define FLAC_PRG "flac"
#define CDPARANOIA_PRG "cdparanoia"

#define pWdg GtkWidget*
#define WDG_ALBUM_ARTIST "album_artist"
#define WDG_ALBUM_TITLE "album_title"
#define WDG_ALBUM_GENRE "album_genre"
#define WDG_ALBUM_YEAR "album_year"
#define WDG_TRACKLIST "tracklist"
#define WDG_PICK_DISC "pick_disc"
#define WDG_CDROM_DRIVES "cdrom_drives"
#define WDG_MP3_QUALITY "combo_mp3_quality"
#define WDG_OGG_QUALITY "scale_ogg_quality"
#define WDG_RIPPING "win_ripping"
#define WDG_SCROLL "scroll"
#define WDG_ABOUT "about"
#define WDG_DISC "disc"
#define WDG_CDDB "lookup"
#define WDG_SEEK "seek"
#define WDG_MAIN "main"
#define WDG_PREFS "preferences"
#define WDG_RIP "rip_button"
#define WDG_SINGLE_ARTIST "single_artist"
#define WDG_SINGLE_GENRE "single_genre"
#define WDG_STATUS "statusLbl"
#define WDG_PROGRESS_TOTAL "progress_total"
#define WDG_PROGRESS_RIP "progress_rip"
#define WDG_PROGRESS_ENCODE "progress_encode"
#define WDG_MP3VBR "mp3_vbr"
#define WDG_BITRATE "bitrate_lbl_2"
#define WDG_FMT_MUSIC "format_music"
#define WDG_FMT_PLAYLIST "format_playlist"
#define WDG_FMT_ALBUMDIR "format_albumdir"
#define WDG_LBL_ARTIST "artist_label"
#define WDG_LBL_ALBUMTITLE "albumtitle_label"
#define WDG_LBL_GENRE "genre_label"
#define WDG_LBL_QUALITY_MP3 "quality_label_mp3"
#define WDG_LBL_QUALITY_OGG "quality_label_ogg"
#define WDG_LBL_BITRATE "bitrate_label"
#define WDG_LBL_FREESPACE "freespace_label"
#define WDG_MUSIC_DIR "music_dir"

#define STR_FREE(x) g_free(x);x=NULL
#define Sleep(x) usleep(x*1000)

#define DIALOG_INFO_OK(...) \
	show_dialog(GTK_MESSAGE_INFO, GTK_BUTTONS_OK,  __VA_ARGS__)
#define DIALOG_WARNING_OK(...) \
	show_dialog(GTK_MESSAGE_WARNING, GTK_BUTTONS_OK,  __VA_ARGS__)
#define DIALOG_ERROR_OK(...) \
	show_dialog(GTK_MESSAGE_ERROR, GTK_BUTTONS_OK,  __VA_ARGS__)
#define DIALOG_INFO_CLOSE(...) \
	show_dialog(GTK_MESSAGE_INFO, GTK_BUTTONS_CLOSE,  __VA_ARGS__)
#define DIALOG_WARNING_CLOSE(...) \
	show_dialog(GTK_MESSAGE_WARNING, GTK_BUTTONS_CLOSE,  __VA_ARGS__)

#define HOOKUP(obj,wdg,name) g_object_set_data_full(G_OBJECT(obj), name,\
						gtk_widget_ref(wdg), (GDestroyNotify) gtk_widget_unref)
#define HOOKUP_NOREF(obj,wdg,name) g_object_set_data(G_OBJECT(obj), name, wdg)
#define LKP_MAIN(x) lookup_widget(win_main, x)
#define LKP_PREF(x) lookup_widget(win_prefs, x)
#define GET_PREF_TEXT(x) gtk_entry_get_text(GTK_ENTRY(LKP_PREF(x)))
#define GET_MAIN_TEXT(x) gtk_entry_get_text(GTK_ENTRY(LKP_MAIN(x)))
#define CLEAR_TEXT(x) gtk_entry_set_text(GTK_ENTRY(LKP_MAIN(x)), "")
#define SET_MAIN_TEXT(x,y) gtk_entry_set_text(GTK_ENTRY(LKP_MAIN(x)), y)
#define LKP_TRACKLIST GTK_TREE_VIEW(LKP_MAIN(WDG_TRACKLIST))
#define GTK_EXPAND_FILL (GTK_EXPAND|GTK_FILL)
#define COMBO_MP3Q GTK_COMBO_BOX(LKP_PREF(WDG_MP3_QUALITY))
#define SCALE_OGGQ GTK_RANGE(LKP_PREF(WDG_OGG_QUALITY))
#define COMBO_DRIVE GTK_COMBO_BOX(LKP_PREF(WDG_CDROM_DRIVES))
#define WIDGET_SHOW(x) gtk_widget_show(LKP_MAIN(x))

#define LOG_NONE	0	//!< Absolutely quiet. DO NOT USE.
#define LOG_FATAL	1	//!< Only errors are reported.
#define LOG_WARN	2	//!< Warnings.
#define LOG_INFO	3	//!< Informational messages
#define LOG_TRACE	4	//!< Trace messages. Debug only.
#define LOG_DEBUG	5	//!< Verbose debugging.

#define TRACEWARN(...) log_gen(__func__, LOG_WARN, __VA_ARGS__)
#define TRACERROR(...) log_gen(__func__, LOG_FATAL, __VA_ARGS__)
#define TRACEINFO(...) log_gen(__func__, LOG_INFO, __VA_ARGS__)

#define SANITIZE(x) trim_chars(x, BADCHARS); trim_whitespace(x)

#define TOGGLE_ACTIVE(x,y) \
	gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(LKP_MAIN(x)), y)

#define CONNECT_SIGNAL(wdg,signal,cb) \
	g_signal_connect((gpointer) wdg, signal, G_CALLBACK(cb), NULL)

#define BOXPACK(box,child,xpand,fill,pad) \
	gtk_box_pack_start(GTK_BOX(box), child, xpand, fill, pad)

#define GTK_REFRESH while (gtk_events_pending ()) gtk_main_iteration ()

#if GLIB_CHECK_VERSION(2,32,0)
#define g_thread_create(func, data, join, err) g_thread_new("T", func, data)
#define g_thread_init(x)
#endif

enum {
    COL_RIPTRACK,
    COL_TRACKNUM,
    COL_TRACKARTIST,
    COL_TRACKTITLE,
    COL_GENRE,
    COL_TRACKTIME,
    COL_YEAR,
    NUM_COLS
};

enum {
	NO_ACTION,
	ACTION_INIT_PBAR,
	ACTION_UPDATE_PBAR,
	ACTION_READY,
	ACTION_COMPLETION,
	ACTION_ENCODED,
	ACTION_EJECTING,
	ACTION_FREESPACE
};

enum { FLAC, LAME, OGGENC };

#define SZENTRY 256

static void szcopy(char dst[SZENTRY], const gchar *src)
{
	memset(dst,0,SZENTRY);
	if(src == NULL) return;
	strncpy(dst,src,SZENTRY-1);
}

typedef struct _prefs {
    char drive[SZENTRY]; // MANUAL, AUTO, DEFAULT, vendor+model
    char cdrom[SZENTRY]; // user-defined path
    char tmp_cdrom[SZENTRY]; // tmp user-defined path
    char cddb_server_name[SZENTRY];
    char format_albumdir[SZENTRY];
    char format_music[SZENTRY];
    char format_playlist[SZENTRY];
    char music_dir[SZENTRY];
    char server_name[SZENTRY];
    char log_file[SZENTRY];
    int make_playlist;
    int rip_wav;
    int rip_mp3;
    int rip_ogg;
    int rip_flac;
    int mp3_vbr;
    int mp3_quality;
    int ogg_quality;
    int main_window_width;
    int main_window_height;
    int eject_on_done;
    int always_overwrite;
    int use_notify;
    int mb_lookup;
    int rip_fast;
    int do_cddb_updates;
    int use_proxy;
    int port_number;
    int do_log;
    int max_log_size;
    int cddb_port;
    int cddb_nocache;
} prefs;

typedef struct _shared {
	FILE *playlist_wav;
	FILE *log_fd;
	FILE *playlist_mp3;
	FILE *playlist_ogg;
	FILE *playlist_flac;
	GCond *available;
	GCond *updated;
	GList *disc_matches;
	GList *drive_list;
	gchar device[128]; // current cdrom device
	GMutex *barrier;
	GMutex *monolock;
	GMutex *guilock;
	GThread *cddb_thread;
	GThread *ioctl_thread;
	GThread *encoder;
	GThread *ripper;
	GThread *tracker;
	bool aborted; // for canceling
	bool allDone; // for stopping the tracking thread
	bool overwriteAll;
	bool overwriteNone;
	bool trace;
	bool working; // ripping or encoding
	cddb_conn_t *cddb_conn;
	cddb_disc_t *cddb_disc;
	double mp3_percent;
	double ogg_percent;
	double flac_percent;
	double rip_percent;
	gboolean track_format[100];
	int cddb_matches;
	int cddb_thread_running;
	int ioctl_thread_running;
	int counter;
	int encode_tracks_completed;
	int numCdparanoiaFailed;
	int numCdparanoiaOk;
	int numLameFailed;
	int numOggFailed;
	int numLameOk;
	int numOggOk;
	int numFlacFailed;
	int numFlacOk;
	int rip_tracks_completed;
	int tracks_to_rip;
	int action;
	pid_t cdparanoia_pid;
	pid_t lame_pid;
	pid_t oggenc_pid;
	pid_t flac_pid;
	double prip;
	char srip[32];
	double pencode;
	char sencode[13];
	double ptotal;
	int parts;
	guint64 free_space;
	guint64 total_space;
	gchar label_space[128];
	gchar disc_id[64];
} shared;

typedef struct _cdrom {
	gchar *model;
	gchar *device;
	int fd;
} s_drive;

typedef struct {
	char *artistName;
	char *artistNameSort;
	uint8_t artistIdCount;
	char **artistId;
} mbartistcredit_t;

typedef struct {
	char *trackName;
	char *trackId;
	mbartistcredit_t trackArtist;
} mbtrack_t;

/** Representation of a MusicBrainz medium.
 * This gives information about some specific CD.
 */
typedef struct {
	/** Sequence number of the disc in the release.
     * For single CD releases, this will always be 1, otherwise it will be
     * the number of the CD in the released set.
     */
	uint16_t discNum;

	/** Title of the disc if there is one. */
	char *title;

	/** Count of tracks in the medium. */
	uint16_t trackCount;

	/** Array of the track information. */
	mbtrack_t *track;

} mbmedium_t;

typedef struct {
	char *releaseGroupId;
	char *releaseId;
	char *releaseDate;
	char *asin;
	char *albumTitle;
	char *releaseType;
	mbartistcredit_t albumArtist;

	/** Total mediums in the release i.e. number of CDs for multidisc releases. */
	uint16_t discTotal;

	mbmedium_t medium;
} mbrelease_t;

typedef struct {
	uint16_t releaseCount;
	mbrelease_t *release;
} mbresult_t;

#define M_ArrayElem(a) (sizeof(a) / sizeof(a[1]))



// Global objects
static GtkWidget *win_main = NULL;
static GtkWidget *win_prefs = NULL;
static prefs *g_prefs = NULL;
static shared *g_data = NULL;

// Functions Prototypes
static GtkWidget *ripping_bar(GtkWidget *table, char *name, int y);
static GtkWidget* create_prefs (void);
static bool prefs_are_valid(void);
static bool string_has_slashes(const char* string);
static char *prefs_get_music_dir(prefs *p);
static gchar* get_config_path(const gchar *file_suffix);
static gpointer encode_thread(gpointer data);
static gpointer track_thread(gpointer data);
static int exec_with_output(const char *args[], int toread, pid_t *p);
static int is_valid_port_number(int number);
static void disable_all_main_widgets(void);
static void disable_lookup_widgets(void);
static void disable_mp3_widgets(void);
static void disable_ogg_widgets(void);
static void dorip_mainthread();
static void enable_all_main_widgets(void);
static void enable_mp3_widgets(void);
static void enable_ogg_widgets(void);
static void get_prefs_from_widgets(prefs *p);
static void on_cancel_clicked(GtkButton *button, gpointer user_data);
static void save_prefs(prefs *p);
static void set_status(char *text);
static void fmt_status(const char *fmt, ...);
static void set_widgets_from_prefs(prefs *p);
static void sigchld();
static void set_gui_action(int action, gboolean wait);
static GtkWidget *lookup_widget(GtkWidget *widget, const gchar *name);
static bool musicbrainz_lookup(const char *discId, mbresult_t * res);
static void musicbrainz_print(FILE *fd, const mbrelease_t * rel);
static void musicbrainz_scan(const mbresult_t * res, char *path);
static void fetch_image(const mbrelease_t *rel, char *path);
static void mb_free(mbresult_t * res);

/** Compare two strings, either of which maybe NULL.  */
static int strcheck(const char *a, const char *b)
{
	if (a == NULL && b == NULL) {
		return 0;
	} else if (a == NULL) {
		return -1;
	} else if (b == NULL) {
		return 1;
	} else {
		return strcmp(a, b);
	}
}





static gboolean stat_file(const char *path) {
	struct stat sts;
	if(stat(path,&sts)!=0) {
		printf("Cannot stat file [%s], error=[%s]", path, strerror(errno));
		return FALSE;
	}
	gchar smode[11];
	memset(smode,0,sizeof(smode));
	memset(smode,'-',sizeof(smode)-1);
	if(sts.st_mode & S_IFLNK) smode[0] = 'l';
	if(sts.st_mode & S_IFSOCK) smode[0] = 's';
	if(sts.st_mode & S_IFBLK) smode[0] = 'b';
	if(sts.st_mode & S_IFCHR) smode[0] = 'c';
	if(sts.st_mode & S_IFDIR) smode[0] = 'd';
	if(sts.st_mode & S_IRUSR) smode[1] = 'r'; /* 3 bits for user  */
	if(sts.st_mode & S_IWUSR) smode[2] = 'w';
	if(sts.st_mode & S_IXUSR) smode[3] = 'x';
	if(sts.st_mode & S_IRGRP) smode[4] = 'r'; /* 3 bits for group */
	if(sts.st_mode & S_IWGRP) smode[5] = 'w';
	if(sts.st_mode & S_IXGRP) smode[6] = 'x';
	if(sts.st_mode & S_IROTH) smode[7] = 'r'; /* 3 bits for other */
	if(sts.st_mode & S_IWOTH) smode[8] = 'w';
	if(sts.st_mode & S_IXOTH) smode[9] = 'x';
	// struct passwd *pwd = getpwuid(sts.st_uid);
	// printf("MODE = [%d][%s][%s] %s\n",sts.st_mode, smode, pwd->pw_name, path);
	return TRUE;
}

static void print_fileinfo(GFileInfo *fi, const gchar *path) {
	guint64 n = g_file_info_get_attribute_uint64 (fi, G_FILE_ATTRIBUTE_FILESYSTEM_FREE);
	g_data->free_space = n;
	gchar *s1 = g_format_size_for_display(n);
	n = g_file_info_get_attribute_uint64 (fi, G_FILE_ATTRIBUTE_FILESYSTEM_SIZE);
	g_data->total_space = n;
	gchar *s2 = g_format_size_for_display(n);
	gchar *s3 = (gchar *) g_file_info_get_attribute_string(fi, G_FILE_ATTRIBUTE_FILESYSTEM_TYPE);
	gboolean readonly = g_file_info_get_attribute_boolean(fi, G_FILE_ATTRIBUTE_FILESYSTEM_READONLY);
	if(!readonly && access(path, W_OK) != 0) {
		readonly = true;
	}
	stat_file(path);
	size_t lz = sizeof(g_data->label_space);
	char *p = g_data->label_space;
	int w = snprintf(p, lz, "%s / %s", s1, s2);
	if(readonly && w < lz-1) {
		w+=snprintf(p+w, lz-w, " (<span foreground=\"red\" weight=\"bold\">read-only</span>)");
	}
	// printf("lz=%d w=%d p=[%s]\n",lz,w,p);
	g_free(s1);
	g_free(s2);
	g_free(s3);
}
static void get_fs_info_cb (GObject *src, GAsyncResult *res, gpointer data)
{
	GFileInfo *fi = g_file_query_filesystem_info_finish(G_FILE(src), res, NULL);
	if (fi) {
        print_fileinfo(fi, (gchar*) data);
		//g_object_unref (fi);
		set_gui_action(ACTION_FREESPACE, false);
	}
}
static void query_fs_async(const gchar *path, gpointer data)
{
	GFile *f = g_file_new_for_path(path);
	g_file_query_filesystem_info_async (f,"filesystem::*", 0, NULL, get_fs_info_cb, (gpointer)path);
	g_object_unref(f);
}
static gboolean gfileinfo_cb (gpointer data)
{
	int from = GPOINTER_TO_INT(data);
	// printf("gfileinfo_cb %d\n", from);
	if(from==0) { // Global
		if(g_prefs && g_prefs->music_dir) query_fs_async(g_prefs->music_dir, data);
	} else {
		query_fs_async(GET_PREF_TEXT(WDG_MUSIC_DIR), data);
	}
	return FALSE;
}

static s_drive* new_drive()
{
	s_drive *d = (s_drive *) g_malloc0(sizeof(s_drive));
	d->fd = -1; // not opened
	return d;
}

static s_drive* create_drive(gchar *device, gchar *model)
{
	s_drive *d = new_drive();
	d->model = g_strdup(model);
	d->device = g_strdup(device);
	return d;
}

static void free_drive(gpointer data)
{
	if(data == NULL) return;
	s_drive *drive = (s_drive*) data;
	g_free(drive->model);
	g_free(drive->device);
	g_free(drive);
}

static s_drive* clone_drive(gpointer data)
{
	if(data == NULL) return NULL;
	s_drive *drive = (s_drive *) data;
	s_drive *clone = new_drive();
	clone->model = g_strdup(drive->model);
	clone->device = g_strdup(drive->device);
	clone->fd = drive->fd;
	return clone;
}

static bool drive_has_device(gpointer data, gchar *device)
{
	if(data == NULL) return FALSE;
	if(device == NULL) return FALSE;
	s_drive *drive = (s_drive *) data;
	return (strcmp(drive->device, device) == 0);
}

/** Write current time in the log file. */
static void log_time(FILE *fd)
{
	struct timeb s_timeb;
	ftime(&s_timeb);
	struct tm *d = localtime(&s_timeb.time);
	fprintf(fd, "\n#%02d/%02d/%04d %02d:%02d:%02d.%03u|",
			d->tm_mday, d->tm_mon + 1, 1900 + d->tm_year,
			d->tm_hour, d->tm_min, d->tm_sec, s_timeb.millitm);
}

/*! Get current time as a formatted date string: DD/MM/YYYY hh:mm:ss
 * @param time_str character string that receives the formatted date.
*/
static char* get_time_str(char time_str[32])
{
	struct tm *d;
	struct timeb s_timeb;
	ftime(&s_timeb);
	d = localtime(&s_timeb.time);
	memset(time_str,0,32);
	snprintf(time_str, 32, "%02d/%02d/%04d %02d:%02d:%02d.%03u",
			d->tm_mday, d->tm_mon + 1, 1900 + d->tm_year,
			d->tm_hour, d->tm_min, d->tm_sec, s_timeb.millitm);
	return time_str;
}

static bool log_init()
{
	char mode_write[] = "w";
	char mode_append[] = "a";

	g_data->log_fd = NULL; // Don't write traces if no log file is opened.

	if(!g_prefs->do_log)
		return TRUE;

	char *fmode = mode_append;
	struct stat sts;
	if(stat(g_prefs->log_file,&sts)==0) {
		if(sts.st_size>g_prefs->max_log_size) { // File too big, overwrite it.
			fmode=mode_write;
		}
	}
	if( (g_data->log_fd=fopen(g_prefs->log_file,fmode)) == NULL) {
		fprintf(stderr,"Warning: cannot open log file '%s' in write mode\n",
													g_prefs->log_file);
		perror("fopen");
		return FALSE;
	}
	g_data->trace = TRUE;
	char time_str[32];
	get_time_str(time_str);
	fprintf(g_data->log_fd, "\n\n ----- %s START -----", time_str);
	fflush(g_data->log_fd);
	return TRUE;
}

static void log_end()
{
	if(g_data->log_fd == NULL) return;
	if(!g_data->trace) return;

	char time_str[32];
	get_time_str(time_str);
	fprintf(g_data->log_fd, "\n ----- %s STOP -----\n", time_str);
	fflush(g_data->log_fd);
	fclose(g_data->log_fd);
	g_data->log_fd = NULL;
}

/** convert log level to string. */
static char* strloglevel(int log_level)
{
	switch(log_level) {
		case LOG_NONE:	return "LOG_NONE ";
		case LOG_FATAL: return "LOG_FATAL";
		case LOG_WARN:	return "LOG_WARN ";
		case LOG_INFO:	return "LOG_INFO ";
		case LOG_TRACE: return "LOG_TRACE";
		case LOG_DEBUG: return "LOG_DEBUG";
		default:		return "LOG_UNKNW";
	}
}

static void log_gen(const char *func_name, int log_level, const char *fmt, ...)
{
	static int n=0; // be persistent
	FILE *fd = NULL;

	if(g_data->log_fd == NULL
			&& log_level != LOG_FATAL
			&& log_level != LOG_WARN)
		return;

	if(g_data->log_fd==NULL) {
		// printf("LOGGING to stderr... %ld\n", fd);
		fd = stderr;
	} else {
		fd = g_data->log_fd;
	}
	va_list args;
	if(fd!=stderr) {
		log_time(fd);
		fprintf(fd,"0x%04lX|%04d|%s|%s|", pthread_self(), ++n,
										strloglevel(log_level), func_name);
	}
	va_start(args,fmt);
	vfprintf(fd, fmt, args);
	va_end(args);
	fflush(fd);
	if(fd==stderr) printf("\n");
}

static gchar *action2str(int action) {
	switch(action) {
		case NO_ACTION: return "NO_ACTION";
		case ACTION_INIT_PBAR: return "ACTION_INIT_PBAR";
		case ACTION_UPDATE_PBAR: return "ACTION_UPDATE_PBAR";
		case ACTION_READY: return "ACTION_READY";
		case ACTION_COMPLETION: return "ACTION_COMPLETION";
		case ACTION_ENCODED: return "ACTION_ENCODED";
		case ACTION_EJECTING: return "ACTION_EJECTING";
		case ACTION_FREESPACE: return "ACTION_FREESPACE";
	}
	return "UNKNOWN_ACTION";
}
static void set_gui_action(int action, gboolean wait) {
	g_mutex_lock(g_data->guilock);
	g_mutex_lock(g_data->monolock);
	g_data->action = action;
	if(wait) {
		TRACEINFO("set_gui_action (%d/%s)", action, action2str(action));
		while (g_data->action) {
			TRACEINFO("set_gui_action WAIT");
			g_cond_wait(g_data->updated, g_data->monolock);
		}
		TRACEINFO("set_gui_action DONE");
	}
	g_mutex_unlock(g_data->monolock);
	g_mutex_unlock(g_data->guilock);
}

static int get_field(char *buf, char *filename, char *field, char **value)
{
#define PSIZE 128

    char data[PSIZE+2];
    char *p = NULL;
    char *s = NULL;
    char *b = NULL;
    int l = 0;
    int end = 0;
	int ret = 0;

    if(buf == NULL) return 0;
    if(filename == NULL) return 0;
    if(field == NULL) return 0;
    if(value == NULL) return 0;

    l = strlen(field);
    if(l > PSIZE - 2) return 0;
    memset(data,0,sizeof(data));
    memcpy(data, field, l);
    data[l] = '=';
    l++;
    b = g_strdup(buf);
    p = b;
    while(1) {
        while(*p) {
            if(*p!='\r' && *p!='\n') break;
            p++;
        }
        if(*p == 0) { // THE END 0
            end = 1;
            break;
        }
        s = p; // start
        while(*p && *p!='\r' && *p!='\n') {
            p++;
        }
        if(*p == 0) { // THE END
            end = 1;
        }
        *p=0;
        if(p==s) {
            break; // Nothing;
        }
        if(p-s < l+1) {
            p++;
            continue;
        }
        if(memcmp(s, data, l) == 0) {
            s+=l;
            if(p - s > PSIZE - 2) {
                TRACERROR("[%s] param value of [%s] is too long (%d)",
							filename, field, p-s+1);
				goto cleanup;
            }
            *value = g_malloc0(p-s+4);
            memset(*value, 0 , p-s+4);
            memcpy(*value, s, p-s);
            ret = 1;
			goto cleanup;
        }
        if(end) {
            break;
        }
        p++;
    }
    TRACEINFO("[%s] param %s not found", filename, field);
cleanup:
	g_free(b);
    return ret;
}

static int get_field_as_szentry(char *buf, char *filename, char *field,
							char szentry[SZENTRY])
{
	char *v = NULL;
	int r = get_field(buf, filename, field, &v);
	if(r) {
		szcopy(szentry,v);
	}
	g_free(v);
	return r;
}

// reads an entire line from a file and turns it into a number
static bool read_long(char *s, long *value)
{
	if(s==NULL) return 0;
	long ret = strtol(s, NULL, 10);
	if (ret == LONG_MIN || ret == LONG_MAX) {
		return FALSE;
	}
	*value = ret;
	return TRUE;
}

static long get_field_as_long(char *buf, char *file, char *field, int *value)
{
	char *num = NULL;
	if(!get_field(buf, file, field, &num)) {
		TRACEINFO("missing field %s", field);
		return 0;
	}
	long ret = -1;
	if(read_long(num, &ret)) {
		*value = ret;
		ret= 1;
	} else {
		ret= 0;
	}
	g_free(num);
	return ret;
}

static int load_file(char *url, char **b)
{
	if (url == NULL) return 0;
	gsize sz = 0;
	if(!g_file_get_contents(url, b, &sz, NULL)) {
		TRACEWARN("could not load config file: %s", strerror(errno));
		return 0;
	}
	return 1;
}

static int is_file(char *path) {
   struct stat st;
   if (stat(path, &st) == -1) {
      return 0;
   }
   return S_ISREG(st.st_mode);
}

static int is_directory(char *path) {
   struct stat st;
   if (stat(path, &st) == -1) {
      return 0;
   }
   return S_ISDIR(st.st_mode);
}

static void fatalError(const char *message)
{
	fprintf(stderr, "Fatal error: %s\n", message);
	exit(-1);
}

static int mp3_quality_to_bitrate(int quality, bool vbr)
{
	switch (quality) {
		case 0:
			if (vbr) return 165;
			else return 128;
		case 1:
			if (vbr) return 190;
			else return 192;
		case 2:
			if (vbr) return 225;
			else return 256;
		case 3:
			if (vbr) return 245;
			else return 320;
	}
	TRACEWARN("called with bad parameter (%d)", quality);
	if (vbr) return 225;
	else return 256;
}

// construct a filename from various parts
//
// path - the path the file is placed in (don't include a trailing '/')
// dir - the parent directory of the file (don't include a trailing '/')
// file - the filename
// extension - the suffix of a file (don't include a leading '.')
//
// NOTE: caller must free the returned string!
// NOTE: any of the parameters may be NULL to be omitted
static char *make_filename(const char *path, const char *dir, const char *file,
														const char *extension)
{
	int len = 1;
	char *ret = NULL;
	int pos = 0;

	if (path) len += strlen(path) + 1;
	if (dir) len += strlen(dir) + 1;
	if (file) len += strlen(file);
	if (extension) len += strlen(extension) + 1;

	ret = g_malloc0(sizeof(char) *len);
	if (path) {
		strncpy(&ret[pos], path, strlen(path));
		pos += strlen(path);
		ret[pos] = '/';
		pos++;
	}
	if (dir) {
		strncpy(&ret[pos], dir, strlen(dir));
		pos += strlen(dir);
		ret[pos] = '/';
		pos++;
	}
	if (file) {
		strncpy(&ret[pos], file, strlen(file));
		pos += strlen(file);
	}
	if (extension) {
		ret[pos] = '.';
		pos++;
		strncpy(&ret[pos], extension, strlen(extension));
		pos += strlen(extension);
	}
	ret[pos] = '\0';
	return ret;
}

// Based on a patch written by Jens Peter Secher <jps@debian.org>
// for Debian's Asunder 2.2 package.
// Make all letters lowercase and replace anything but letters,
// digits, and dashes with underscores.
static void string_simplify(char *str, int len)
{
	int c = 0;
    for (int i = 0; i < len; i++) {
        c = tolower(str[i]);
        if (!( (c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') || c == '-')) {
			c = '_';
		}
        str[i] = c;
    }
}

static unsigned int simplify_shift(const char *label, char *dest)
{
	if (!label) return 0;
	unsigned int l = strlen(label);
	strncpy(dest, label, l);
	string_simplify(dest, l);
	return l;
}

static unsigned int strlen_shift(const char *str)
{
	if(!str) return 0;
	return strlen(str);
}

static unsigned int copy_shift(const char* src, char *dst)
{
	if (!src) return 0;
	strcpy(dst, src);
	return strlen(src);
}

// substitute various items into a formatted string (similar to printf)
//
// format - the format of the filename
// tracknum - gets substituted for %N in format
// year - gets substituted for %Y in format
// artist - gets substituted for %A in format
// album - gets substituted for %L in format
// title - gets substituted for %T in format
//
// NOTE: caller must free the returned string!
static char *parse_format(const char *format, int tracknum, const char *year,
										const char *artist, const char *album,
										const char *genre, const char *title)
{
	unsigned i = 0;
	int len = 0;
	unsigned int format_len = strlen(format);

	for (i = 0; i < format_len; i++) {
		if ((format[i] == '%') && (i+1 < format_len)) {
			switch (format[i+1]) {
				case 'a':
				case 'A':
					len += strlen_shift(artist);
					break;
				case 'l':
				case 'L':
					len += strlen_shift(album);
					break;
				case 'n':
				case 'N':
					if ((tracknum > 0) && (tracknum < 100)) len += 2;
					break;
				case 'y':
				case 'Y':
					len += strlen_shift(year);
					break;
				case 't':
				case 'T':
					len += strlen_shift(title);
					break;
				case 'g':
				case 'G':
					len += strlen_shift(genre);
					break;
				case '%':
					len++;
					break;
			}
			i++;	// skip the character after the %
		} else {
			len++;
		}
	}
	char *ret = g_malloc0(sizeof(char) * (len + 1));
	char *p = ret;
	for (i = 0; i < format_len; i++) {
		if ((format[i] == '%') && (i + 1 < format_len)) {
			switch (format[i+1]) {
				case 'a':
					p += simplify_shift(artist, p);
					break;
				case 'A':
					p += copy_shift(artist, p);
					break;
				case 'l':
					p += simplify_shift(album, p);
					break;
				case 'L':
					p += copy_shift(album, p);
					break;
				case 'N':
					if ((tracknum > 0) && (tracknum < 100)) {
						*p = '0' + ((int)tracknum / 10); p++;
						*p = '0' + (tracknum % 10); p++;
					}
					break;
				case 'Y':
					p += copy_shift(year, p);
					break;
				case 't':
					p += simplify_shift(title, p);
					break;
				case 'T':
					p += copy_shift(title, p);
					break;
				case 'g':
					p += simplify_shift(genre, p);
					break;
				case 'G':
					p += copy_shift(genre, p);
					break;
				case '%':
					*p = '%';
					p++;
			}
			i++;	// skip the character after the %
		} else {
			*p = format[i];
			p++;
		}
	}
	*p = '\0';
	return ret;
}

// searches $PATH for the named program
// returns 1 if found, 0 otherwise
static int program_exists(const char *name)
{
	char *path = getenv("PATH");
	char *strings = g_strdup(path);
	unsigned i;
	unsigned numpaths = 1;

	for (i = 0; i < strlen(path); i++) {
		if (strings[i] == ':') {
			numpaths++;
		}
	}
	char **paths = g_malloc0(sizeof(char *) *numpaths);
	numpaths = 1;
	paths[0] = &strings[0];
	for (i = 0; i < strlen(path); i++) {
		if (strings[i] == ':') {
			strings[i] = '\0';
			paths[numpaths] = &strings[i + 1];
			numpaths++;
		}
	}
	char *filename;
	int ret = 0;
	for (i = 0; i < numpaths; i++) {
		filename = make_filename(paths[i], NULL, name, NULL);
		struct stat s;
		if (stat(filename, &s) == 0) {
			ret = 1;
			g_free(filename);
			break;
		}
		g_free(filename);
	}
	g_free(strings);
	g_free(paths);
	return ret;
}

// Uses mkdir() for every component of the path
// and returns if any of those fails with anything other than EEXIST.
static int recursive_mkdir(char *pathAndName)
{
	mode_t mode = S_IRWXU|S_IRWXG|S_IRWXO;
	int count;
	int pathAndNameLen = strlen(pathAndName);
	int rc;
	char charReplaced;

	for (count = 0; count < pathAndNameLen; count++) {
		if (pathAndName[count] == '/') {
			charReplaced = pathAndName[count + 1];
			pathAndName[count + 1] = '\0';
			rc = mkdir(pathAndName, mode);
			pathAndName[count + 1] = charReplaced;

			if (rc != 0 && !(errno == EEXIST || errno == EISDIR))
				return rc;
		}
	}
	// in case the path doesn't have a trailing slash:
	return mkdir(pathAndName, mode);
}

// removes all instances of bad characters from the string
// str - the string to trim
// bad - the sting containing all the characters to remove
static void trim_chars(char *str, const char *bad)
{
	int len = strlen(str);
	for (unsigned b = 0; b < strlen(bad); b++) {
		int pos = 0;
		for (int i = 0; i < len + 1; i++) {
			if (str[i] != bad[b]) {
				str[pos] = str[i];
				pos++;
			}
		}
	}
}

// removes leading and trailing whitespace as defined by isspace()
// str - the string to trim
static void trim_whitespace(char *str)
{
	int i;
	int pos = 0;
	int len = strlen(str);

	// trim leading space
	for (i = 0; i < len + 1; i++) {
		if (!isspace(str[i]) || (pos > 0)) {
			str[pos] = str[i];
			pos++;
		}
	}
	// trim trailing space
	len = strlen(str);
	for (i = len - 1; i >= 0; i--) {
		if (!isspace(str[i])) {
			break;
		}
		str[i] = '\0';
	}
}

static gchar *get_default_music_dir()
{
	return g_strdup_printf("%s/%s", getenv("HOME"), "Music");
}

static GtkWidget *lookup_widget(GtkWidget *widget, const gchar *name)
{
	if(widget == NULL) {
		g_warning("Lookup widget is null for '%s'", name);
		return NULL;
	}
	GtkWidget *parent = NULL;
	for (;;) {
		parent = widget->parent;
		if (parent == NULL) break;
		widget = parent;
	}
	GtkWidget *found_widget = (pWdg) g_object_get_data(G_OBJECT(widget), name);
	if (!found_widget)
		g_warning("Widget not found: %s", name);

	return found_widget;
}

static GtkListStore *get_tracklist_store()
{
	GtkTreeModel *model = gtk_tree_view_get_model(LKP_TRACKLIST);
	if(!model)
		g_warning("get_tracklist_store() failed !");

	return GTK_LIST_STORE(model);
}

static void show_dialog(GtkMessageType type, GtkButtonsType btn, char *fmt, ...)
{
    va_list args;
    va_start(args,fmt);
    gchar *txt = g_strdup_vprintf(fmt, args);
    va_end(args);
    GtkWidget* dialog = gtk_message_dialog_new(GTK_WINDOW(win_main),
					GTK_DIALOG_DESTROY_WITH_PARENT, type, btn, "%s", txt);
    gtk_dialog_run (GTK_DIALOG (dialog));
    gtk_widget_destroy (dialog);
    g_free(txt);
}

static void clear_widgets()
{
	// hide the widgets for multiple albums
	gtk_widget_hide(LKP_MAIN(WDG_DISC));
	gtk_widget_hide(LKP_MAIN(WDG_PICK_DISC));
	// clear the textboxes
	CLEAR_TEXT("album_artist");
	CLEAR_TEXT("album_title");
	CLEAR_TEXT(WDG_ALBUM_GENRE);
	CLEAR_TEXT(WDG_ALBUM_YEAR);
	// clear the tracklist
	gtk_tree_view_set_model(LKP_TRACKLIST, NULL);
	// disable the "rip" button
	gtk_widget_set_sensitive(LKP_MAIN(WDG_RIP), FALSE);
}

static GdkPixbuf *LoadMainIcon()
{
	GdkPixbufLoader *loader = gdk_pixbuf_loader_new();
	GError* error = NULL;
	gdk_pixbuf_loader_write(loader, pixmaps_irongrip_png,
							pixmaps_irongrip_png_len, &error);
	if (error) {
		g_warning("Unable to load logo : %s\n", error->message);
		g_error_free(error);
		return NULL;
	}
	gdk_pixbuf_loader_close(loader,NULL);
	GdkPixbuf* icon = gdk_pixbuf_loader_get_pixbuf(loader);
	return icon;
}

#define FMAP(tdef,fname,fptr) \
	tdef fptr = (tdef)dlsym(h,fname);\
	if(h == NULL) {\
		printf("Library function not found: %s\n",fname);\
		goto cleanup;\
	}

static void* get_notify_lib(int *version)
{
	static void *h = NULL;
	static int v = 0;
	char *libs[] = { "libnotify.so.4", "libnotify.so.1", "libnotify.so", NULL };
	int ver[] =  { 4, 1, 0};

	*version = v;
	if(h) return h;
	for(int i = 0; i < 3; i++) {
		v = *version = ver[i];
		h = dlopen(libs[i], RTLD_LAZY);
		if(h) {
			// printf("Loaded %s\n", *p);
			return h;
		}
	}
	printf("Failed to open libnotify library version 4 and 1.\n");
	return NULL;
}

static int notify(char *message)
{
	typedef void  (*ntf_init_t)(char*);
	typedef void *(*ntf_new_t)(char*,char*,char*,char*);
	typedef void  (*ntf_set_timeout_t)(void*,int);
	typedef void  (*ntf_show_t)(void*,char*);
	typedef void  (*ntf_icon_t)(void*,void*);
	int ret = 0;
	int version = 0;
	void *h = get_notify_lib(&version);
	if(!h) return ret;
	FMAP(ntf_init_t,"notify_init",nn_init);
	FMAP(ntf_new_t,"notify_notification_new",nn_new);
	FMAP(ntf_set_timeout_t,"notify_notification_set_timeout",nn_st);
	FMAP(ntf_show_t,"notify_notification_show",nn_show);
	FMAP(ntf_icon_t,"notify_notification_set_image_from_pixbuf",nn_icon);

	nn_init(PROGRAM_NAME);
	void *n = nn_new(PROGRAM_NAME, message, NULL, NULL);
	nn_st(n, 6000);
	if(version == 4) { // That would crash with libnotify version 1.
		GdkPixbuf *icon = LoadMainIcon();
		if (icon) {
			nn_icon(n, icon);
			g_object_unref(icon);
		}
	}
	nn_show(n, NULL);
	ret = 1;
cleanup:
	// dlclose(h); // Would blow up application
	return ret;
}

static GList *get_drives_to_open()
{
	GList *r = NULL;
	gchar *d = g_prefs->drive;

	if(!d) {
		TRACEWARN("No preferred drive.");
		goto end;
	}
	if(strcmp(d,"MANUAL")==0) {
		r = g_list_append(r, create_drive(g_prefs->cdrom, d));
		goto end;
	}
	if(strcmp(d,"DEFAULT")==0) {
		r = g_list_append(r, create_drive("/dev/cdrom", d));
		goto end;
	}
	GList *l = g_data->drive_list;
	if(!l || g_list_length(l) == 0) {
		TRACEINFO("NO DRIVE LIST");
		goto end;
	}
	if(strcmp(d,"AUTO")==0) {
		for (GList *c = g_list_first(l); c != NULL; c = g_list_next(c)) {
			if(drive_has_device(c->data, g_data->device))
				r = g_list_prepend(r,clone_drive(c->data));
			else
				r = g_list_append(r,clone_drive(c->data));
		}
		goto end;
	}
	for (GList *c = g_list_first(l); c != NULL; c = g_list_next(c)) {
		s_drive *s = (s_drive *) c->data;
		if(strcmp(d,s->model)==0) {
			r = g_list_append(r,clone_drive(c->data));
			break;
		}
	}

end:
	if(!r) {
		TRACEINFO("NO DRIVE TO OPEN.");
	} else {
		//TRACEINFO("%d drives to open.", g_list_length(r));
	}
	return r;
}

static gpointer ioctl_thread_run(gpointer data)
{
	/*int status =*/ ioctl(GPOINTER_TO_INT(data), CDROM_DISC_STATUS, CDSL_CURRENT);
	g_atomic_int_set(&g_data->ioctl_thread_running, 0);
	return NULL;
}

static bool check_disc()
{
	static bool alreadyCleared = true; // no clear when program just started
	static bool alreadyGood = false; // check when program just started
									// true when good disc inserted
	bool drive_opened = false;
	GList *l = get_drives_to_open();
	if(!l) {
		set_status("Error: cannot find any cdrom drive !");
		alreadyGood = false;
		return false;
	}
	for (GList *c = g_list_first(l); c != NULL; c = g_list_next(c)) {
		s_drive *s = (s_drive *) c->data;
		int	fd = open(s->device, O_RDONLY | O_NONBLOCK);
		if (fd < 0) {
			continue;
		}
		drive_opened = true;
		if(!alreadyGood) {
			set_status("<b>Reading disc contents...</b>");
			GTK_REFRESH;
		}
		g_atomic_int_set(&g_data->ioctl_thread_running, 1);
		g_data->ioctl_thread = g_thread_create(ioctl_thread_run,
											GINT_TO_POINTER(fd), TRUE, NULL);
		while (g_atomic_int_get(&g_data->ioctl_thread_running) != 0) {
			GTK_REFRESH;
			Sleep(100);
		}
		int status = ioctl(fd, CDROM_DISC_STATUS, CDSL_CURRENT);
		close(fd);
		if (status == CDS_AUDIO || status == CDS_MIXED) {
			if (!alreadyGood || strcmp(s->device,g_data->device)) {
				alreadyGood = true;	// don't return true again for this disc
				alreadyCleared = false;	// clear when disc is removed
				strcpy(g_data->device, s->device);
				return true;
			} else {
				return false; // do not re-read cddb
			}
		}
	}
	alreadyGood = false;
	if (!drive_opened) {
		set_status("Error: cannot access to the cdrom drive !");
	} else {
		Sleep(700);
		set_status("Please insert an audio disc in the cdrom drive...");
		disable_lookup_widgets();
		if (!alreadyCleared) {
			alreadyCleared = true;
			clear_widgets();
		}
	}
	return false;
}

static GtkTreeModel *create_model_from_disc(cddb_disc_t *disc)
{
	GtkListStore *store = gtk_list_store_new(NUM_COLS,
					G_TYPE_BOOLEAN,	// rip? checkbox
					G_TYPE_UINT,	// track number
					G_TYPE_STRING,	// track artist
					G_TYPE_STRING,	// track title
					G_TYPE_STRING,	// track time
					G_TYPE_STRING,	// genre
					G_TYPE_STRING	// year
	    );

	for (cddb_track_t *track = cddb_disc_get_track_first(disc);
			track != NULL; track = cddb_disc_get_track_next(disc)) {
		int seconds = cddb_track_get_length(track);
		char time[6];
		snprintf(time, 6, "%02d:%02d", seconds / 60, seconds % 60);

		char *artist = (char *)cddb_track_get_artist(track);
		SANITIZE(artist);

		char *title = (char *)cddb_track_get_title(track);
		SANITIZE(title);

		char year_str[5];
		snprintf(year_str, 5, "%d", cddb_disc_get_year(disc));
		year_str[4] = '\0';

		int number = cddb_track_get_number(track);

		GtkTreeIter iter;
		gtk_list_store_append(store, &iter);
		gtk_list_store_set(store, &iter,
					   COL_RIPTRACK, g_data->track_format[number],
					   COL_TRACKNUM, number,
					   COL_TRACKARTIST, artist,
					   COL_TRACKTITLE, title,
					   COL_TRACKTIME, time,
					   COL_GENRE, cddb_disc_get_genre(disc),
					   COL_YEAR, year_str, -1);
	}
	return GTK_TREE_MODEL(store);
}

static void eject_disc(char *cdrom)
{
	if(!cdrom) return;
	int fd = open(cdrom, O_RDONLY | O_NONBLOCK);
	if (fd < 0) {
		TRACEWARN("Couldn't open '%s'", cdrom);
		return;
	}
	ioctl(fd, CDROM_LOCKDOOR, 0);
	int eject = ioctl(fd, CDROMEJECT, 0 /* CDSL_CURRENT */);
	close(fd);
	if(eject >= 0) return;
	// perror("ioctl");
	TRACEINFO("Eject returned %d", eject);

	// use '/usr/bin/eject' on ubuntu because ioctl seems to work only as root
	gchar cmd[SZENTRY];
	snprintf(cmd, SZENTRY, "/usr/bin/eject %s", cdrom);
	g_spawn_command_line_async(cmd, NULL);
}

static gpointer cddb_thread_run(gpointer data)
{
	if (g_prefs->cddb_nocache) {
		cddb_cache_disable(g_data->cddb_conn);
	} else {
		cddb_cache_enable(g_data->cddb_conn);
	}
	g_data->cddb_matches = cddb_query(g_data->cddb_conn, g_data->cddb_disc);
	TRACEINFO("Got %d matches for CDDB QUERY.", g_data->cddb_matches);
	if (g_data->cddb_matches == -1)
		g_data->cddb_matches = 0;

	// cddb_disc_print(g_data->cddb_disc);

	// make a list of all the matches
	g_data->disc_matches = NULL;
	for (int i = 0; i < g_data->cddb_matches; i++) {
		cddb_disc_t *match = cddb_disc_clone(g_data->cddb_disc);
		if (!cddb_read(g_data->cddb_conn, match)) {
			cddb_error_print(cddb_errno(g_data->cddb_conn));
			TRACERROR("cddb_read() failed.");
			break;
		}
		// cddb_disc_print(match);
		g_data->disc_matches = g_list_append(g_data->disc_matches, match);

		// move to next match
		if (i < g_data->cddb_matches - 1) {
			if (!cddb_query_next(g_data->cddb_conn, g_data->cddb_disc))
				fatalError("Query index out of bounds.");
		}
	}
	g_atomic_int_set(&g_data->cddb_thread_running, 0);
	return NULL;
}

static void lookup_disc(cddb_disc_t *disc)
{
	// set up the connection to the cddb server
	g_data->cddb_conn = cddb_new();
	if (g_data->cddb_conn == NULL)
		fatalError("cddb_new() failed. Out of memory?");

	cddb_set_server_name(g_data->cddb_conn, g_prefs->cddb_server_name);
	cddb_set_server_port(g_data->cddb_conn, g_prefs->cddb_port);

	if (g_prefs->use_proxy) {
		cddb_set_http_proxy_server_name(g_data->cddb_conn, g_prefs->server_name);
		cddb_set_http_proxy_server_port(g_data->cddb_conn, g_prefs->port_number);
		cddb_http_proxy_enable(g_data->cddb_conn);
	}

	// Patch from Asunder 2.3: use HTTP when port is 80 (for MusicBrainz).
	if (g_prefs->cddb_port== 80)
		cddb_http_enable(g_data->cddb_conn);

	// query cddb to find similar discs
	g_atomic_int_set(&g_data->cddb_thread_running, 1);
	g_data->cddb_disc = disc;
	g_data->cddb_thread = g_thread_create(cddb_thread_run, NULL, TRUE, NULL);

	// show cddb update window
	disable_all_main_widgets();
	GtkLabel *status = GTK_LABEL(LKP_MAIN(WDG_STATUS));
	gtk_label_set_text(status, _("<b>Getting disc info from the internet...</b>"));
	gtk_label_set_use_markup(status, TRUE);

	while (g_atomic_int_get(&g_data->cddb_thread_running) != 0) {
		GTK_REFRESH;
		Sleep(200);
	}
	gtk_label_set_text(status, "CDDB query finished.");
	enable_all_main_widgets();

	cddb_destroy(g_data->cddb_conn);
}

static int read_toc_entry(int fd, int track_num, unsigned long *lba) {
	struct cdrom_tocentry te;
	int ret;

	te.cdte_track = track_num;
	te.cdte_format = CDROM_LBA;

	ret = ioctl(fd, CDROMREADTOCENTRY, &te);
	g_assert( te.cdte_format == CDROM_LBA );

	/* in case the ioctl() was successful */
	if ( ret == 0 )
		*lba = te.cdte_addr.lba;

	return ret;
}

#define XA_INTERVAL		((60 + 90 + 2) * CD_FRAMES)
static int read_leadout(int fd, unsigned long *lba) {
	struct cdrom_multisession ms;
	int ret;

	ms.addr_format = CDROM_LBA;
	ret = ioctl(fd, CDROMMULTISESSION, &ms);

	if ( ms.xa_flag ) {
		*lba = ms.addr.lba - XA_INTERVAL;
		return ret;
	}
	return read_toc_entry(fd, CDROM_LEADOUT, lba);
}

static cddb_disc_t *read_disc(char *cdrom)
{
	int fd = open(cdrom, O_RDONLY | O_NONBLOCK);
	if (fd < 0) {
		TRACEWARN("Couldn't open '%s'", cdrom);
		return NULL;
	}
	cddb_disc_t *disc = NULL;
	int status = ioctl(fd, CDROM_DISC_STATUS, CDSL_CURRENT);
	if ((status != CDS_AUDIO) && (status != CDS_MIXED)) {
		goto end;
	}
	// see if we can read the disc's table of contents (TOC).
	struct cdrom_tochdr th;
	if (ioctl(fd, CDROMREADTOCHDR, &th) != 0)
		goto end;

	TRACEINFO("starting track: %d", th.cdth_trk0);
	TRACEINFO("ending track: %d", th.cdth_trk1);
	disc = cddb_disc_new();
	if (disc == NULL)
		fatalError("cddb_disc_new() failed. Out of memory?");

	struct cdrom_tocentry te;
	te.cdte_format = CDROM_LBA;
	for (int i = th.cdth_trk0; i <= th.cdth_trk1; i++) {
		te.cdte_track = i;
		if (ioctl(fd, CDROMREADTOCENTRY, &te) != 0)
			continue;

		if (te.cdte_ctrl & CDROM_DATA_TRACK) {
			// DATA track. "rip" box not checked by default
			g_data->track_format[i] = FALSE;
		} else {
			g_data->track_format[i] = TRUE;
		}
		cddb_track_t *trk = cddb_track_new();
		if (trk == NULL)
			fatalError("cddb_track_new() failed. Out of memory?");

		cddb_track_set_frame_offset(trk, te.cdte_addr.lba + SECONDS_TO_FRAMES(2));
		char trackname[9];
		snprintf(trackname, 9, "Track %d", i);
		cddb_track_set_title(trk, trackname);
		cddb_track_set_artist(trk, "Unknown Artist");
		cddb_disc_add_track(disc, trk);
	}
	te.cdte_track = CDROM_LEADOUT;
	if (ioctl(fd, CDROMREADTOCENTRY, &te) == 0) {
		cddb_disc_set_length(disc, (te.cdte_addr.lba + SECONDS_TO_FRAMES(2))
				/ SECONDS_TO_FRAMES(1));
	}

	GChecksum *ck = g_checksum_new(G_CHECKSUM_SHA1);

	int first = th.cdth_trk0;
	int last = th.cdth_trk1;
	guchar		tmp[17]; /* for 8 hex digits (16 to avoid trouble) */
	sprintf((char*)tmp, "%02X", first);
	g_checksum_update(ck, tmp, strlen((char *)tmp));
	sprintf((char*)tmp, "%02X", last);
	g_checksum_update(ck, tmp, strlen((char *)tmp));

	int track_offsets[100];
	memset(&track_offsets,0,sizeof(track_offsets));
	unsigned long lba;
	read_leadout(fd, &lba);
	track_offsets[0] = lba + 150;
	int i;
	for (i = first; i <= last; i++) {
		read_toc_entry(fd, i, &lba);
		track_offsets[i] = lba + 150;
	}
	for (i = 0; i < 100; i++) {
		sprintf((char *)tmp, "%08X", track_offsets[i]);
		g_checksum_update(ck, tmp, strlen((char *)tmp));
	}
	guint8 buffer[48];
	gsize digest_len = 48;
	g_checksum_get_digest(ck, buffer, &digest_len);
	/*printf("SHA = [");
	for(i=0;i<digest_len;i++) {
		printf("%02X", buffer[i]);
	}
	printf("] ");*/
	gchar *b64 =g_base64_encode(buffer, digest_len);
	//printf("BASE64 = %s (len=%d)\n", b64, strlen(b64));
	gchar *p = b64;
	while(*p) {
		if(*p == '+') *p = '.';
		if(*p == '/') *p = '_';
		if(*p == '=') *p = '-';
		p++;
	}
	strcpy(g_data->disc_id, b64);
	g_free(b64);
end:
	close(fd);
	return disc;
}

static void update_tracklist(cddb_disc_t *disc)
{
	cddb_track_t *track;
	char *disc_artist = (char *)cddb_disc_get_artist(disc);
	char *disc_genre = (char *)cddb_disc_get_genre(disc);
	char *disc_title = (char *)cddb_disc_get_title(disc);

	if (disc_artist != NULL) {
		SANITIZE(disc_artist);
		SET_MAIN_TEXT("album_artist", disc_artist);

		bool singleartist= true;
		for (track = cddb_disc_get_track_first(disc);
				track != NULL; track = cddb_disc_get_track_next(disc)) {
			if (strcmp(disc_artist, cddb_track_get_artist(track)) != 0) {
				singleartist = false;
				break;
			}
		}
		TOGGLE_ACTIVE(WDG_SINGLE_ARTIST, singleartist);
	}
	if (disc_title != NULL) {
		SANITIZE(disc_title);
		SET_MAIN_TEXT(WDG_ALBUM_TITLE, disc_title);
	}
	if (disc_genre) {
		SANITIZE(disc_genre);
		SET_MAIN_TEXT(WDG_ALBUM_GENRE, disc_genre);
	} else {
		SET_MAIN_TEXT(WDG_ALBUM_GENRE, "Unknown");
	}
	unsigned disc_year = cddb_disc_get_year(disc);
	if (disc_year == 0) disc_year = 1900;

	gchar *album_year = g_strdup_printf("%d", disc_year);
	SET_MAIN_TEXT(WDG_ALBUM_YEAR, album_year);
	g_free(album_year);
	TOGGLE_ACTIVE(WDG_SINGLE_GENRE, true);
	GtkTreeModel *model = create_model_from_disc(disc);
	gtk_tree_view_set_model(LKP_TRACKLIST, model);
	g_object_unref(model);
}

static char* get_device_info(char *dev, char *field)
{
	gchar *path = g_strconcat("/sys/block/",dev,"/device/",field,NULL);
	FILE *fd = fopen (path, "r");
	g_free(path);
	if(!fd) return g_strconcat("(unknown ",field,")",NULL);
	char b[256];
	char *p = NULL;
	if(fgets(b, sizeof(b), fd) != NULL) {
		g_strstrip(b);
		p = g_strdup(b);
	}
	fclose(fd);
	// printf("%s=[%s]\n",field,p);
	return (p ? p : g_strconcat("(error in ",field,")",NULL));
}

static GList* get_cdrom_info(char *str)
{
	GList *matches = NULL;
	g_strstrip(str);
	gchar **list = g_strsplit(str,"\t",-1);
	for (gchar **ptr = list; *ptr; ptr++) {
		s_drive *drive = new_drive();
		drive->device = g_strconcat("/dev/",*ptr,NULL);
		gchar *v = get_device_info(*ptr,"vendor");
		gchar *m = get_device_info(*ptr,"model");
		drive->model = g_strconcat(v," ",m,NULL);
		g_free(v);
		g_free(m);
		matches = g_list_append(matches, drive);
	}
	g_strfreev(list);
	return matches;
}

#if ! GLIB_CHECK_VERSION(2,28,0)
static void g_list_free_full(GList *list, GDestroyNotify free_func)
{
	g_list_foreach(list, (GFunc) free_func, NULL);
	g_list_free(list);
}
#endif

static bool seek_cdrom()
{
	const char* cdinfo = "/proc/sys/dev/cdrom/info";
	struct stat s;
	if (stat(cdinfo, &s) != 0) {
		perror("stat");
		return false;
	}
	FILE *fd = fopen (cdinfo, "r");
	if (!fd) {
		perror("open cdinfo");
		return false;
	}
	const int sz = 1024;
	char buf[sz+1];
	const char* drive = "drive name:";
	int l = strlen(drive);
	while(fgets(buf, sz, fd) != NULL) {
		// printf("READ %s\n%s\n", cdinfo, buf);
		if(0 ==  memcmp(buf, drive, l)){
			if(g_data->drive_list)
				g_list_free_full(g_data->drive_list, free_drive);
			g_data->drive_list = get_cdrom_info(buf+l);
			break;
		}
	}
	fclose (fd);
	if (g_data->drive_list == NULL
			|| g_list_length(g_data->drive_list) == 0) {
		char* NOCDROM = "No CD-ROM drive found on this machine.";
		TRACEWARN(NOCDROM);
		set_status(NOCDROM);
		return false;
	}
	return true;
}

static bool refresh(int force)
{
	seek_cdrom();

	if (g_data->working) // don't do anything
		return true;

	if (!check_disc()) {
		if(!force) {
			return false;
		} else {
			// printf("Forced.\n");
		}
	}
	cddb_disc_t *disc = read_disc(g_data->device);
	if (disc == NULL) return true;
	gtk_widget_set_sensitive(LKP_MAIN(WDG_RIP), TRUE);

	// show the temporary info
	SET_MAIN_TEXT("album_artist", "Unknown Artist");
	SET_MAIN_TEXT("album_title", "Unknown Album");
	update_tracklist(disc);
	if (!g_prefs->do_cddb_updates && !force)
		return true;

	lookup_disc(disc);
	cddb_disc_destroy(disc);
	if (g_data->disc_matches == NULL) {
		TRACEWARN("No disc matches !");
		return true;
	}
	// fill in and show the album drop-down box
	if (g_list_length(g_data->disc_matches) > 1) {
		GtkListStore *store = gtk_list_store_new(2, G_TYPE_STRING, G_TYPE_STRING);
		GtkTreeIter iter;
		for (GList *curr = g_list_first(g_data->disc_matches);
				curr != NULL; curr = g_list_next(curr)) {
			cddb_disc_t *tempdisc = (cddb_disc_t *) curr->data;
			char *artist = (char *) cddb_disc_get_artist(tempdisc);
			char *title = (char *) cddb_disc_get_title(tempdisc);
			TRACEINFO("Artist: [%s] Title [%s]", artist, title);
			gtk_list_store_append(store, &iter);
			gtk_list_store_set(store, &iter, 0, artist, 1, title, -1);
		}
		GtkComboBox *pick_disc = GTK_COMBO_BOX(LKP_MAIN(WDG_PICK_DISC));
		gtk_combo_box_set_model (pick_disc, GTK_TREE_MODEL(store));
		gtk_combo_box_set_active(pick_disc, 1);
		gtk_combo_box_set_active(pick_disc, 0);
		WIDGET_SHOW(WDG_DISC);
		WIDGET_SHOW(WDG_PICK_DISC);
	}
	disc = g_list_nth_data(g_data->disc_matches, 0);
	update_tracklist(disc);
	return true;
}

static gboolean for_each_row_deselect(GtkTreeModel *model, GtkTreePath *path,
										GtkTreeIter *iter, gpointer data)
{
    gtk_list_store_set(GTK_LIST_STORE(model), iter, COL_RIPTRACK, 0, -1);
    return FALSE;
}

static gboolean for_each_row_select(GtkTreeModel *model, GtkTreePath *path,
										GtkTreeIter *iter, gpointer data)
{
    gtk_list_store_set(GTK_LIST_STORE(model), iter, COL_RIPTRACK, 1, -1);
    return FALSE;
}

static gboolean idle(gpointer data)
{
    refresh(0);
    return (data != NULL);
}

static gboolean scan_on_startup(gpointer data)
{
	notify("Welcome to " PROGRAM_NAME " !");
    TRACEINFO("scan_on_startup");
    refresh(0);
    return (data != NULL);
}

static gboolean on_cdrom_focus_out(GtkWidget *widget, GdkEventFocus *event,
														gpointer user_data)
{
	const gchar *ctext = gtk_entry_get_text(GTK_ENTRY(widget));
	strcpy(g_prefs->tmp_cdrom, ctext);
	return FALSE;
}

static gboolean on_year_focus_out(GtkWidget *widget, GdkEventFocus *event,
														gpointer user_data)
{
	const gchar *ctext = gtk_entry_get_text(GTK_ENTRY(widget));
	gchar *txt = g_malloc0(5);
	strncpy(txt, ctext, 5);
	txt[4] = '\0';
	if ((txt[0] != '1' && txt[0] != '2') || txt[1] < '0' || txt[1] > '9' ||
	    txt[2] < '0' || txt[2] > '9' || txt[3] < '0' || txt[3] > '9') {
		sprintf(txt, "1900");
	}
	gtk_entry_set_text(GTK_ENTRY(widget), txt);
	g_free(txt);
	return FALSE;
}

static gboolean on_field_focus_out_event(GtkWidget *widget)
{
	const gchar *ctext = gtk_entry_get_text(GTK_ENTRY(widget));
	gchar *text = g_strdup(ctext);
	SANITIZE(text);
	if (text[0] == '\0') {
		// gtk_entry_set_text(GTK_ENTRY(widget), STR_UNKNOWN);
	} else {
		gtk_entry_set_text(GTK_ENTRY(widget), text);
	}
	g_free(text);
	return FALSE;
}

static gboolean on_album_artist_focus_out(GtkWidget *widget,
									GdkEventFocus *event, gpointer user_data)
{
	return on_field_focus_out_event(widget);
}
static gboolean on_album_title_focus_out(GtkWidget *widget,
									GdkEventFocus *event, gpointer user_data)
{
	return on_field_focus_out_event(widget);
}
static gboolean on_album_genre_focus_out(GtkWidget *widget,
									GdkEventFocus *event, gpointer user_data)
{
	return on_field_focus_out_event(widget);
}

static void on_artist_edited(GtkCellRendererText *cell, gchar *path,
										gchar *new_text, gpointer user_data)
{
	GtkListStore *store = get_tracklist_store();
	SANITIZE(new_text);
	GtkTreeIter iter;
	gtk_tree_model_get_iter_from_string(GTK_TREE_MODEL(store), &iter, path);
	if (new_text[0] == '\0')
		gtk_list_store_set(store, &iter, COL_TRACKARTIST, STR_UNKNOWN, -1);
	else
		gtk_list_store_set(store, &iter, COL_TRACKARTIST, new_text, -1);
}

static void on_genre_edited(GtkCellRendererText *cell, gchar *path,
										gchar *new_text, gpointer user_data)
{
	GtkListStore *store = get_tracklist_store();
	SANITIZE(new_text);
	GtkTreeIter iter;
	gtk_tree_model_get_iter_from_string(GTK_TREE_MODEL(store), &iter, path);
	if (new_text[0] == '\0')
		gtk_list_store_set(store, &iter, COL_GENRE, STR_UNKNOWN, -1);
	else
		gtk_list_store_set(store, &iter, COL_GENRE, new_text, -1);
}

static void on_deselect_all_click(GtkMenuItem *menuitem, gpointer data)
{
	GtkTreeModel *model = gtk_tree_view_get_model(LKP_TRACKLIST);
	gtk_tree_model_foreach(model, for_each_row_deselect, NULL);
}

static void adjust_mp3_vbr()
{
	GtkToggleButton *vbr_button = GTK_TOGGLE_BUTTON(LKP_PREF(WDG_MP3VBR));
	gint i = gtk_combo_box_get_active(COMBO_MP3Q);
	bool vbr = gtk_toggle_button_get_active(vbr_button);
	gchar *rate = g_strdup_printf(_("%d Kbps"), mp3_quality_to_bitrate(i, vbr));
	gtk_label_set_text(GTK_LABEL(LKP_PREF(WDG_BITRATE)), rate);
}

static void on_vbr_toggled(GtkToggleButton *togglebutton, gpointer user_data)
{
	adjust_mp3_vbr();
}
static void on_mp3_quality_changed(GtkComboBox *combobox, gpointer user_data)
{
	adjust_mp3_vbr();
}

static void set_pref_text(char *widget_name, char *text)
{
	if(!text) {
		g_warning("[set_pref_text] Text is NULL for '%s'\n", widget_name);
		return;
	}
	gtk_entry_set_text(GTK_ENTRY(LKP_PREF(widget_name)), text);
}

static void on_s_drive_changed(GtkComboBox *combo, gpointer user_data)
{
	GtkTreeModel *m = gtk_combo_box_get_model(combo);
	GtkTreeIter iter;
	if(gtk_combo_box_get_active_iter(combo, &iter)) {
		gchar *device = NULL;
		gchar *model = NULL;
		gtk_tree_model_get(m, &iter, 0, &device, 1, &model, -1);
		// printf("TREE : device = %s, model = %s\n", device, model);
		if(strcmp(device,"MANUAL")==0) {
			gtk_widget_set_sensitive(LKP_PREF("cdrom"),TRUE);
			set_pref_text("cdrom", g_prefs->tmp_cdrom);
		} else if(strcmp(device,"DEFAULT")==0) {
			set_pref_text("cdrom", "/dev/cdrom");
			gtk_widget_set_sensitive(LKP_PREF("cdrom"),FALSE);
		} else if(strcmp(device,"AUTO")==0) {
			set_pref_text("cdrom", "/dev/*");
			gtk_widget_set_sensitive(LKP_PREF("cdrom"),FALSE);
		} else {
			set_pref_text("cdrom", device);
			gtk_widget_set_sensitive(LKP_PREF("cdrom"),FALSE);
		}
		g_free(device);
		g_free(model);
	} else {
		TRACEWARN("no active item.");
	}
}

static void enable_widget(char *name) {
	gtk_widget_set_sensitive(LKP_MAIN(name), TRUE);
}

static void disable_widget(char *name) {
	gtk_widget_set_sensitive(LKP_MAIN(name), FALSE);
}

static void on_pick_disc_changed(GtkComboBox *combobox, gpointer user_data)
{
	cddb_disc_t *disc = g_list_nth_data(g_data->disc_matches,
										gtk_combo_box_get_active(combobox));
	update_tracklist(disc);
}

// TODO: select handwritten folder
static void on_folder_clicked(GtkButton *button, gpointer user_data)
{
	GtkWidget *dlg = gtk_file_chooser_dialog_new(
									_("Choose a destination folder"),
									GTK_WINDOW(win_prefs),
									GTK_FILE_CHOOSER_ACTION_SELECT_FOLDER,
									GTK_STOCK_CANCEL, GTK_RESPONSE_CANCEL,
									GTK_STOCK_OPEN, GTK_RESPONSE_ACCEPT,
									NULL);

	GtkFileChooser *chooser = GTK_FILE_CHOOSER(dlg);
	// BUG : TODO set the current path the WDG_MUSIC_DIR
	gtk_file_chooser_set_current_folder(chooser, g_prefs->music_dir);
	if (gtk_dialog_run(GTK_DIALOG(chooser)) == GTK_RESPONSE_ACCEPT) {
		gchar *folder_path = gtk_file_chooser_get_filename(chooser);
		gtk_entry_set_text(GTK_ENTRY(LKP_PREF(WDG_MUSIC_DIR)), folder_path);
		g_free(folder_path);
		gfileinfo_cb((void*)1);
	}
	gtk_widget_destroy (dlg);
}

static void on_preferences_clicked(GtkToolButton *button, gpointer user_data)
{
	gtk_widget_show(win_prefs);
	gfileinfo_cb((void*)2);
}

static void on_prefs_response(GtkDialog *dialog, gint response, gpointer data)
{
	if (response == GTK_RESPONSE_OK) {
		if (!prefs_are_valid()) return;
		get_prefs_from_widgets(g_prefs);
		save_prefs(g_prefs);
		if(g_prefs->mb_lookup)
			enable_widget(WDG_SEEK);
		else
			disable_widget(WDG_SEEK);
	}
	gtk_widget_hide(GTK_WIDGET(dialog));
}

static void on_prefs_show(GtkWidget *widget, gpointer user_data)
{
	set_widgets_from_prefs(g_prefs);
}

static void on_press_f2(void)
{
	GtkWidget *tracklist = LKP_MAIN(WDG_TRACKLIST);
	if (!GTK_WIDGET_HAS_FOCUS(tracklist)) return;
	GtkTreeView *treeView = GTK_TREE_VIEW(tracklist);
	GtkTreePath *treePath;
	GtkTreeViewColumn *focusColumn;
	gtk_tree_view_get_cursor(treeView, &treePath, &focusColumn);
	if (treePath == NULL || focusColumn == NULL) return;
	gtk_tree_view_set_cursor(treeView, treePath, focusColumn, TRUE);
}

static void on_lookup_clicked(GtkToolButton *button, gpointer user_data)
{
	refresh(1);
}

static void on_seek_clicked(GtkToolButton *button, gpointer user_data)
{
	if(g_data->disc_id[0] == '\0') return;
	if(!g_prefs->music_dir) return;
	if(!is_directory(g_prefs->music_dir)) return;

	mbresult_t res;
	if(musicbrainz_lookup(g_data->disc_id, &res)) {
		musicbrainz_scan(&res, g_prefs->music_dir);
	}
	mb_free(&res);
}

static bool lookup_cdparanoia()
{
	if (program_exists(CDPARANOIA_PRG)) {
		return true;
	}
	DIALOG_ERROR_OK(_("The '%s' program was not found in your 'PATH'."
						" %s requires that program in order to rip CDs."
						" Please, install '%s' on this computer."),
						CDPARANOIA_PRG, PROGRAM_NAME, CDPARANOIA_PRG);
	return false;
}

static void on_rip_button_clicked(GtkButton *button, gpointer user_data)
{
	GtkListStore *store = get_tracklist_store();
	if (store == NULL) {
		DIALOG_ERROR_OK(_("No CD is inserted."
							" Please insert a CD into the CD-ROM drive."));
		return;
	}
	if(!lookup_cdparanoia()) return;
	dorip_mainthread();
}

static void missing_encoder_dialog(gchar *program, gchar *format)
{
	DIALOG_ERROR_OK(_(
		"The '%s' program was not found in your 'PATH'."
		" %s requires that program in order to create %s files."
	    " All %s functionality is disabled."
		" You should install '%s' on this computer if you want to convert"
		" audio tracks to %s."),
		program, PROGRAM_NAME, format, format, program, format);
}

static void on_rip_mp3_toggled(GtkToggleButton *button, gpointer user_data)
{
	if (gtk_toggle_button_get_active(button) && !program_exists(LAME_PRG)) {
		missing_encoder_dialog(LAME_PRG, "MP3");
		g_prefs->rip_mp3 = 0;
		gtk_toggle_button_set_active(button, g_prefs->rip_mp3);
	}
	if (!gtk_toggle_button_get_active(button))
		disable_mp3_widgets();
	else
		enable_mp3_widgets();
}

static void on_rip_ogg_toggled(GtkToggleButton *button, gpointer user_data)
{
	if (gtk_toggle_button_get_active(button) && !program_exists(OGGENC_PRG)) {
		missing_encoder_dialog(OGGENC_PRG, "Ogg Vorbis");
		g_prefs->rip_ogg = 0;
		gtk_toggle_button_set_active(button, g_prefs->rip_ogg);
	}
	if (!gtk_toggle_button_get_active(button))
		disable_ogg_widgets();
	else
		enable_ogg_widgets();
}

static void on_rip_flac_toggled(GtkToggleButton *button, gpointer user_data)
{
	if (gtk_toggle_button_get_active(button) && !program_exists(FLAC_PRG)) {
		missing_encoder_dialog(FLAC_PRG, "FLAC");
		g_prefs->rip_flac = 0;
		gtk_toggle_button_set_active(button, g_prefs->rip_flac);
	}
}

static void on_rip_toggled(GtkCellRendererToggle *cell, gchar *path,
														gpointer user_data)
{
	GtkListStore *store = get_tracklist_store();
	GtkTreeIter iter;
	GtkTreeModel *tree_store = GTK_TREE_MODEL(store);
	gtk_tree_model_get_iter_from_string(tree_store, &iter, path);
	int toggled;
	gtk_tree_model_get(tree_store, &iter, COL_RIPTRACK, &toggled, -1);
	gtk_list_store_set(store, &iter, COL_RIPTRACK, !toggled, -1);
}

static void on_select_all_click(GtkMenuItem *menuitem, gpointer data)
{
	GtkTreeModel *model = gtk_tree_view_get_model(LKP_TRACKLIST);
	gtk_tree_model_foreach(model, for_each_row_select, NULL);
}

static void toggle_column(GtkToggleButton *btn, int column)
{
	GtkTreeViewColumn *c = gtk_tree_view_get_column(LKP_TRACKLIST, column);
	gtk_tree_view_column_set_visible(c, !gtk_toggle_button_get_active(btn));
}

static void on_single_artist_toggled(GtkToggleButton *btn, gpointer user_data)
{
	toggle_column(btn, COL_TRACKARTIST);
}

static void on_single_genre_toggled(GtkToggleButton *btn, gpointer user_data)
{
	toggle_column(btn, COL_GENRE);
}

static void on_title_edited(GtkCellRendererText *cell, gchar *path,
								gchar *new_text, gpointer user_data)
{
	GtkListStore *store = get_tracklist_store();
	SANITIZE(new_text);
	GtkTreeIter iter;
	gtk_tree_model_get_iter_from_string(GTK_TREE_MODEL(store), &iter, path);
	if (new_text[0] == '\0')
		gtk_list_store_set(store, &iter, COL_TRACKTITLE, STR_UNKNOWN, -1);
	else
		gtk_list_store_set(store, &iter, COL_TRACKTITLE, new_text, -1);
}

static gboolean on_ripcol_clicked(GtkWidget *treeView,
								GdkEventButton *event, gpointer user_data)
{
	if (!GTK_WIDGET_SENSITIVE(LKP_MAIN(WDG_RIP))) {
		return FALSE;
	}
	GtkTreeModel *model = gtk_tree_view_get_model(LKP_TRACKLIST);
	gtk_tree_model_foreach(model, for_each_row_deselect, NULL);
	return TRUE;
}

static gboolean on_tracklist_clicked(GtkWidget *treeView,
								GdkEventButton *event, gpointer user_data)
{
	if (event->type != GDK_BUTTON_PRESS
		|| event->button != 3
		|| !GTK_WIDGET_SENSITIVE(LKP_MAIN(WDG_RIP))) {
		return FALSE;
	}
	GtkWidget *menu = gtk_menu_new();
	GtkWidget *item = gtk_menu_item_new_with_label(_("Select all for ripping"));
	CONNECT_SIGNAL(item, "activate", on_select_all_click);
	gtk_menu_shell_append(GTK_MENU_SHELL(menu), item);
	gtk_widget_show_all(menu);
	item = gtk_menu_item_new_with_label(_("Deselect all for ripping"));
	CONNECT_SIGNAL(item, "activate", on_deselect_all_click);
	gtk_menu_shell_append(GTK_MENU_SHELL(menu), item);
	gtk_widget_show_all(menu);
	gtk_menu_popup(GTK_MENU(menu), NULL, NULL, NULL, NULL,
					event->button, gdk_event_get_time((GdkEvent *) event));
	/* no need for signal to propagate */
	return TRUE;
}

static void on_window_close(GtkWidget *widget, GdkEventFocus *event,
													gpointer user_data)
{
	gtk_window_get_size(GTK_WINDOW(win_main),
				&g_prefs->main_window_width, &g_prefs->main_window_height);
	save_prefs(g_prefs);
	gtk_main_quit();
}

static bool mkdir_p(char *path) {
	if(is_directory(path)) return TRUE;
	if(g_file_test(path, G_FILE_TEST_IS_REGULAR)) return FALSE;
	mode_t mode = S_IRWXU | S_IRWXG | S_IROTH | S_IXOTH;
	int rc = g_mkdir_with_parents(path, mode);
	if(rc != 0 && errno != EEXIST) {
		perror("g_mkdir_with_parents");
		return FALSE;
	}
	return TRUE;
}

static gchar* get_cache_path(const gchar *filename, gboolean create_dirs)
{
	g_assert(filename != NULL);

	const gchar *cache = g_getenv("XDG_CACHE_HOME");
	gchar *dir= NULL;
	gchar *fullpath = NULL;
	if (cache == NULL) {
		const gchar *home = g_getenv("HOME");
		dir = g_strdup_printf("%s/.cache/%s", home, PACKAGE);
	} else {
		dir = g_strdup_printf("%s/%s", cache, PACKAGE);;
	}
	if(create_dirs && !mkdir_p(dir)) {
		goto cleanup;
	}
	fullpath = g_strdup_printf("%s/%s", dir, filename);
cleanup:
	g_free(dir);
	return fullpath;
}

static void read_completion_file(GtkListStore *list, const char *name)
{
	gchar *file = get_cache_path(name,false);
	if(!file) {
		TRACEWARN("could not get completion file path: %s", strerror(errno));
		return;
	}
	TRACEINFO("for %s in %s", name, file);
	FILE *data = fopen(file, "r");
	if (data == NULL) {
		g_free(file);
		return;
	}
	char buf[1024];
	for (int i = 0; i < COMPLETION_MAX; i++) {
		char *ptr = fgets(buf, sizeof(buf), data);
		if (ptr == NULL) break;
		g_strstrip(buf);
		if (buf[0] == '\0') continue;
		if (g_utf8_validate(buf, -1, NULL)) {
			GtkTreeIter iter;
			gtk_list_store_append(list, &iter);
			gtk_list_store_set(list, &iter, 0, buf, -1);
		}
	}
	fclose(data);
	g_free(file);
}

static void create_completion(GtkWidget *entry, const char *name)
{
	TRACEINFO("create_completion for %s", name);
	GtkListStore *list = gtk_list_store_new(1, G_TYPE_STRING);
	GtkEntryCompletion *compl = gtk_entry_completion_new();
	gtk_entry_completion_set_model(compl, GTK_TREE_MODEL(list));
	gtk_entry_completion_set_inline_completion(compl, FALSE);
	gtk_entry_completion_set_popup_completion(compl, TRUE);
	gtk_entry_completion_set_popup_set_width(compl, TRUE);
	gtk_entry_completion_set_text_column(compl, 0);
	gtk_entry_set_completion(GTK_ENTRY(entry), compl);
	g_object_set_data_full(G_OBJECT(entry), COMPLETION_NAME_KEY,
						g_strdup(name),(GDestroyNotify) g_free);
	read_completion_file(list, name);
}

static gboolean save_history_cb(GtkTreeModel *model, GtkTreePath *path,
									GtkTreeIter *iter, gpointer data)
{
	char *str;
	gtk_tree_model_get(model, iter, 0, &str, -1);
	if (str) {
		FILE *fp = (FILE *) data;
		fprintf(fp, "%s\n", str);
		g_free(str);
	}
	return FALSE;
}

static GtkTreeModel *get_tree_model(GtkWidget *entry)
{
	GtkEntryCompletion *compl = gtk_entry_get_completion(GTK_ENTRY(entry));
	if (compl == NULL) return NULL;
	GtkTreeModel *model = gtk_entry_completion_get_model(compl);
	return model;
}

static void save_completion(GtkWidget *entry)
{
	GtkTreeModel *model = get_tree_model(entry);
	if (model == NULL) return;

	const gchar *name = g_object_get_data(G_OBJECT(entry), COMPLETION_NAME_KEY);
	if (name == NULL) return;

	gchar *file = get_cache_path(name,true);
	if(!file) {
		TRACEWARN("could not get completion file path: %s", strerror(errno));
		return;
	}
	TRACEINFO("Saving completion data for %s in %s", name, file);
	FILE *fd = fopen(file, "w");
	if (fd) {
		gtk_tree_model_foreach(model, save_history_cb, fd);
		fclose(fd);
	}
	g_free(file);
}

static void add_completion(GtkWidget *entry)
{
	const char *str = gtk_entry_get_text(GTK_ENTRY(entry));
	if (str == NULL || strlen(str) == 0) return;

	GtkTreeModel *model = get_tree_model(entry);
	if (model == NULL) return;

	GtkTreeIter iter;
	gboolean found = gtk_tree_model_get_iter_first(model, &iter);
	while (found) {
		char *saved_str = NULL;
		gtk_tree_model_get(model, &iter, 0, &saved_str, -1);
		if (saved_str) {
			if (strcmp(str, saved_str) == 0) {
				gtk_list_store_remove(GTK_LIST_STORE(model), &iter);
				g_free(saved_str);
				break;
			}
			g_free(saved_str);
		}
		found = gtk_tree_model_iter_next(model, &iter);
	}
	gtk_list_store_prepend(GTK_LIST_STORE(model), &iter);
	gtk_list_store_set(GTK_LIST_STORE(model), &iter, 0, str, -1);
}

static void show_aboutbox(void)
{
	gchar license[COPYING_len+1];
	memcpy(license,COPYING,COPYING_len);
	license[COPYING_len]= 0;

	gchar* authors[] = { AUTHOR,
						"",
						"IronGrip is a fork of the Asunder program.",
						"Many thanks to the authors and contributors",
						"of the Asunder project.",
						"",
						"See file 'README.md' for details.",
						NULL };

	gchar* comments = N_("An application to save tracks from an Audio CD"
							" as WAV, Ogg Vorbis, FLAC and MP3.");

	gchar* copyright = {"Copyright 2005 Eric Lathrop (Asunder)\n"
						"Copyright 2007 Andrew Smith (Asunder)\n"
						"Copyright 2012 " AUTHOR};

	gchar* name = PROGRAM_NAME;
	gchar* version = VERSION;
	gchar* website = HOMEPAGE;
	gchar* website_label = website;

	GdkPixbuf* icon = LoadMainIcon();
	if (!icon) {
		return;
	}
	gtk_show_about_dialog (
			GTK_WINDOW(LKP_MAIN(WDG_MAIN)),
			"authors", authors,
			// "artists", artists,
			"comments", comments,
			"copyright", copyright,
			// "documenters", documenters,
			"logo", icon,
			"name", name,
			// "program-name", name, // not in GTK 2.8, since 2.12
			"version", version,
			"website", website,
			"website-label", website_label,
			// "translator-credits", translators,
			"license", license,
			NULL);
}

static void on_about_clicked(GtkToolButton *button, gpointer user_data)
{
    show_aboutbox();
}

static GtkWidget *create_main(void)
{
	GtkWidget *main_win = gtk_window_new(GTK_WINDOW_TOPLEVEL);
	gtk_window_set_title(GTK_WINDOW(main_win), PROGRAM_NAME " v." VERSION);
	gtk_window_set_default_size(GTK_WINDOW(main_win),
			g_prefs->main_window_width, g_prefs->main_window_height);
	GdkPixbuf *main_icon = LoadMainIcon();
	if (main_icon) {
		gtk_window_set_icon(GTK_WINDOW(main_win), main_icon);
		g_object_unref(main_icon);
	}
	gtk_window_set_position(GTK_WINDOW(main_win), GTK_WIN_POS_CENTER);

	GtkWidget *vbox1 = gtk_vbox_new(FALSE, 0);
	gtk_widget_show(vbox1);
	gtk_container_add(GTK_CONTAINER(main_win), vbox1);

	GtkWidget *toolbar = gtk_toolbar_new();
	gtk_widget_show(toolbar);
	BOXPACK(vbox1, toolbar, FALSE, FALSE, 0);
	gtk_toolbar_set_style(GTK_TOOLBAR(toolbar), GTK_TOOLBAR_BOTH_HORIZ);

	GtkIconSize icon_size = gtk_toolbar_get_icon_size(GTK_TOOLBAR(toolbar));

	GtkWidget *icon = gtk_image_new_from_stock(GTK_STOCK_REFRESH, icon_size);
	gtk_widget_show(icon);
	GtkWidget *lookup = (pWdg) gtk_tool_button_new(icon, _("CDDB Lookup"));
	GtkTooltips *tip = gtk_tooltips_new();
	gtk_tooltips_set_tip(tip, lookup,
		_("Look up into the CDDB for information about this audio disc."), NULL);
	gtk_widget_show(lookup);
	gtk_container_add(GTK_CONTAINER(toolbar), lookup);
	gtk_tool_item_set_is_important(GTK_TOOL_ITEM(lookup), TRUE);

	GtkWidget *seek_icon = gtk_image_new_from_stock(GTK_STOCK_EXECUTE, icon_size);
	gtk_widget_show(seek_icon);
	GtkWidget *seek_button = (pWdg) gtk_tool_button_new(seek_icon, _("IronSeek"));
	tip = gtk_tooltips_new();
	gtk_tooltips_set_tip(tip, seek_button,
		_("Look up into MusicBrainz and Amazon for metadata and covers."), NULL);
	gtk_widget_show(seek_button);
	gtk_container_add(GTK_CONTAINER(toolbar), (pWdg) seek_button);
	gtk_tool_item_set_is_important(GTK_TOOL_ITEM(seek_button), TRUE);
	if(!g_prefs->mb_lookup)
		gtk_widget_set_sensitive(seek_button, FALSE);

	GtkWidget *pref= (pWdg) gtk_tool_button_new_from_stock(GTK_STOCK_PREFERENCES);
	gtk_widget_show(pref);
	gtk_container_add(GTK_CONTAINER(toolbar), pref);
	gtk_tool_item_set_is_important(GTK_TOOL_ITEM(pref), TRUE);

	GtkWidget *sep = (pWdg) gtk_separator_tool_item_new();
	gtk_widget_show(sep);
	gtk_container_add(GTK_CONTAINER(toolbar), sep);

	GtkWidget *rip_icon = gtk_image_new_from_stock(GTK_STOCK_CDROM, icon_size);
	gtk_widget_show(rip_icon);
	GtkWidget *rip_button = (pWdg) gtk_tool_button_new(rip_icon, _("Rip"));
	gtk_widget_show(rip_button);
	gtk_container_add(GTK_CONTAINER(toolbar), (pWdg) rip_button);
	gtk_tool_item_set_is_important(GTK_TOOL_ITEM(rip_button), TRUE);
	// disable the "rip" button
	// it will be enabled when a disc is found in the drive
	gtk_widget_set_sensitive(rip_button, FALSE);

	sep= (pWdg) gtk_separator_tool_item_new();
	gtk_tool_item_set_expand (GTK_TOOL_ITEM(sep), TRUE);
	gtk_separator_tool_item_set_draw((GtkSeparatorToolItem *)sep, FALSE);
	gtk_widget_show(sep);
	gtk_container_add(GTK_CONTAINER(toolbar), sep);

	GtkWidget *about = (pWdg) gtk_tool_button_new_from_stock(GTK_STOCK_ABOUT);
	gtk_widget_show(about);
	gtk_container_add(GTK_CONTAINER(toolbar), about);
	gtk_tool_item_set_is_important(GTK_TOOL_ITEM(about), TRUE);

	GtkWidget *table = gtk_table_new(3, 3, FALSE);
	gtk_widget_show(table);
	BOXPACK(vbox1, table, FALSE, TRUE, 3);

	GtkWidget *album_artist = gtk_entry_new();
	create_completion(album_artist, WDG_ALBUM_ARTIST);
	gtk_widget_show(album_artist);

	GtkTable *tbl = GTK_TABLE(table);
	gtk_table_attach(tbl, album_artist, 1, 2, 1, 2, GTK_EXPAND_FILL, 0, 0, 0);

	GtkWidget *album_title = gtk_entry_new();
	gtk_widget_show(album_title);
	create_completion(album_title, WDG_ALBUM_TITLE);
	gtk_table_attach(tbl, album_title, 1, 2, 2, 3, GTK_EXPAND_FILL, 0, 0, 0);

	GtkWidget *pick_disc = gtk_combo_box_new();
	gtk_table_attach(tbl, pick_disc, 1, 2, 0, 1, GTK_FILL, GTK_FILL, 0, 0);

	GtkWidget *album_genre = gtk_entry_new();
	create_completion(album_genre, WDG_ALBUM_GENRE);
	gtk_widget_show(album_genre);
	gtk_table_attach(tbl, album_genre, 1, 2, 3, 4, GTK_EXPAND_FILL, 0, 0, 0);

	GtkWidget *disc = gtk_label_new(_("Disc:"));
	gtk_table_attach(tbl, disc, 0, 1, 0, 1, GTK_FILL, 0, 3, 0);
	gtk_misc_set_alignment(GTK_MISC(disc), 0, 0.49);

	GtkWidget *artist_label = gtk_label_new(_("Album Artist:"));
	gtk_misc_set_alignment(GTK_MISC(artist_label), 0, 0);
	gtk_widget_show(artist_label);
	gtk_table_attach(tbl, artist_label, 0, 1, 1, 2, GTK_FILL, 0, 3, 0);

	GtkWidget *albumtitle_label = gtk_label_new(_("Album Title:"));
	gtk_misc_set_alignment(GTK_MISC(albumtitle_label), 0, 0);
	gtk_widget_show(albumtitle_label);
	gtk_table_attach(tbl, albumtitle_label, 0, 1, 2, 3, GTK_FILL, 0, 3, 0);

	GtkWidget *single_artist = gtk_check_button_new_with_mnemonic(
														_("Single Artist"));
	gtk_widget_show(single_artist);
	gtk_table_attach(tbl, single_artist, 2, 3, 1, 2, GTK_FILL, 0, 3, 0);

	GtkWidget *genre_label = gtk_label_new(_("Genre / Year:"));
	gtk_misc_set_alignment(GTK_MISC(genre_label), 0, 0);
	gtk_widget_show(genre_label);
	gtk_table_attach(tbl, genre_label, 0, 1, 3, 4, GTK_FILL, 0, 3, 0);

	GtkWidget *single_genre = gtk_check_button_new_with_mnemonic(
														_("Single Genre"));
	//~ gtk_widget_show(single_genre);
	//~ gtk_table_attach(GTK_TABLE(table2), single_genre, 2, 3, 3, 4,
	//~  GTK_FILL, 0, 3, 0);

	GtkWidget *album_year = gtk_entry_new();
	gtk_widget_show(album_year);
	gtk_table_attach(tbl, album_year, 2, 3, 3, 4, GTK_FILL, 0, 3, 0);

	GtkWidget *scroll = gtk_scrolled_window_new(NULL, NULL);
	gtk_widget_show(scroll);
	BOXPACK(vbox1, scroll, TRUE, TRUE, 0);
	gtk_scrolled_window_set_policy(GTK_SCROLLED_WINDOW(scroll),
								GTK_POLICY_AUTOMATIC, GTK_POLICY_AUTOMATIC);
	GtkWidget *tracklist = gtk_tree_view_new();
	GtkTreeView *tracktree = GTK_TREE_VIEW(tracklist);
	gtk_widget_show(tracklist);
	gtk_container_add(GTK_CONTAINER(scroll), tracklist);
	gtk_tree_view_set_rules_hint(tracktree, TRUE);
	gtk_tree_view_set_enable_search(tracktree, FALSE);

	GtkWidget *rip_box = gtk_vbox_new(FALSE, 0);
	gtk_widget_show(rip_box);
	gtk_container_set_border_width(GTK_CONTAINER(rip_box), 0);
	table = gtk_table_new(3, 2, FALSE);
	gtk_widget_show(table);
	BOXPACK(rip_box, table, FALSE, FALSE, 0);
	GtkWidget *progress_total = ripping_bar(table,"Total progress", 0);
	GtkWidget *progress_rip = ripping_bar(table,"Ripping", 1);
	GtkWidget *progress_encode = ripping_bar(table,"Encoding", 2);

	GtkWidget *cancel = gtk_button_new_from_stock(GTK_STOCK_CANCEL);
	gtk_widget_show(cancel);
	GTK_WIDGET_SET_FLAGS(cancel, GTK_CAN_DEFAULT);
	GtkWidget *alignment_cancel= gtk_alignment_new(0.5, 0.5, 0.4, 1);
	gtk_widget_show(alignment_cancel);
	gtk_container_add(GTK_CONTAINER(alignment_cancel), cancel);
	BOXPACK(rip_box, alignment_cancel, FALSE, FALSE, 8);
	CONNECT_SIGNAL(cancel, "clicked", on_cancel_clicked);

	GtkWidget *win_ripping = gtk_frame_new(NULL);
	gtk_container_set_border_width(GTK_CONTAINER(win_ripping), 8);
	gtk_frame_set_label(GTK_FRAME(win_ripping), "Progress");
	gtk_container_add(GTK_CONTAINER(win_ripping), rip_box);
	BOXPACK(vbox1, win_ripping, TRUE, TRUE, 0);

	// Set up all the columns for the track listing widget
#define TRACK_INSERT(text,type,column) \
	gtk_tree_view_insert_column_with_attributes(\
			GTK_TREE_VIEW(tracklist), -1, text, cell, type, column, NULL)

	GtkCellRenderer *cell = NULL;
	cell = gtk_cell_renderer_toggle_new();
	g_object_set(cell, "activatable", TRUE, NULL);
	CONNECT_SIGNAL(cell, "toggled", on_rip_toggled);
	TRACK_INSERT(_("Rip"), "active", COL_RIPTRACK);
	GtkTreeViewColumn *col = gtk_tree_view_get_column(tracktree, COL_RIPTRACK);
	gtk_tree_view_column_set_clickable(col, TRUE);

	cell = gtk_cell_renderer_text_new();
	TRACK_INSERT(_("Track"), STR_TEXT, COL_TRACKNUM);

	cell = gtk_cell_renderer_text_new();
	g_object_set(cell, "editable", TRUE, NULL);
	CONNECT_SIGNAL(cell, "edited", on_artist_edited);
	TRACK_INSERT(_("Artist"), STR_TEXT, COL_TRACKARTIST);

	cell = gtk_cell_renderer_text_new();
	g_object_set(cell, "editable", TRUE, NULL);
	CONNECT_SIGNAL(cell, "edited", on_title_edited);
	TRACK_INSERT(_("Title"), STR_TEXT, COL_TRACKTITLE);

	cell = gtk_cell_renderer_text_new();
	g_object_set(cell, "editable", TRUE, NULL);
	CONNECT_SIGNAL(cell, "edited", on_genre_edited);
	TRACK_INSERT(_("Genre"), STR_TEXT, COL_GENRE);

	cell = gtk_cell_renderer_text_new();
	TRACK_INSERT(_("Time"), STR_TEXT, COL_TRACKTIME);

	// set up the columns for the album selecting dropdown box
	cell = gtk_cell_renderer_text_new();
	GtkCellLayout *disc_layout = GTK_CELL_LAYOUT(pick_disc);
	gtk_cell_layout_pack_start(disc_layout, cell, TRUE);
	gtk_cell_layout_set_attributes(disc_layout, cell, STR_TEXT, 0, NULL);
	cell = gtk_cell_renderer_text_new();
	gtk_cell_layout_pack_start(disc_layout, cell, TRUE);
	gtk_cell_layout_set_attributes(disc_layout, cell, STR_TEXT, 1, NULL);

	// Bottom HBOX
	GtkWidget *hbox5 = gtk_hbox_new(FALSE, 5);
	BOXPACK(vbox1, hbox5, FALSE, TRUE, 5);
	gtk_widget_show(hbox5);

	GtkWidget *statusLbl = gtk_label_new("Welcome to " PROGRAM_NAME);
	gtk_label_set_use_markup(GTK_LABEL(statusLbl), TRUE);
	gtk_misc_set_alignment(GTK_MISC(statusLbl), 0.02, 0.5);
	BOXPACK(hbox5, statusLbl, TRUE, TRUE, 0);
	gtk_widget_show(statusLbl);

	CONNECT_SIGNAL(main_win, "delete_event", on_window_close);
	CONNECT_SIGNAL(tracklist, "button-press-event", on_tracklist_clicked);
	CONNECT_SIGNAL(col, "clicked", on_ripcol_clicked);
	CONNECT_SIGNAL(lookup, "clicked", on_lookup_clicked);
	CONNECT_SIGNAL(seek_button, "clicked", on_seek_clicked);
	CONNECT_SIGNAL(pref, "clicked", on_preferences_clicked);
	CONNECT_SIGNAL(about, "clicked", on_about_clicked);
	CONNECT_SIGNAL(album_artist, "focus_out_event", on_album_artist_focus_out);
	CONNECT_SIGNAL(album_title, "focus_out_event", on_album_title_focus_out);
	CONNECT_SIGNAL(pick_disc, "changed", on_pick_disc_changed);
	CONNECT_SIGNAL(single_artist, "toggled", on_single_artist_toggled);
	CONNECT_SIGNAL(rip_button, "clicked", on_rip_button_clicked);
	CONNECT_SIGNAL(album_genre, "focus_out_event", on_album_genre_focus_out);
	CONNECT_SIGNAL(single_genre, "toggled", on_single_genre_toggled);
	CONNECT_SIGNAL(album_year, "focus_out_event", on_year_focus_out);

	/* KEYBOARD accelerators */
	GtkAccelGroup *group = gtk_accel_group_new();
	gtk_window_add_accel_group(GTK_WINDOW(main_win), group);

	guint key;
	GdkModifierType modifier;
	GClosure *closure = NULL;

	gtk_accelerator_parse("<Control>W", &key, &modifier);
	closure = g_cclosure_new(G_CALLBACK(on_window_close), NULL, NULL);
	gtk_accel_group_connect(group, key, modifier, GTK_ACCEL_VISIBLE, closure);

	gtk_accelerator_parse("<Control>Q", &key, &modifier);
	closure = g_cclosure_new(G_CALLBACK(on_window_close), NULL, NULL);
	gtk_accel_group_connect(group, key, modifier, GTK_ACCEL_VISIBLE, closure);

	gtk_accelerator_parse("F2", &key, &modifier);
	closure = g_cclosure_new(G_CALLBACK(on_press_f2), NULL, NULL);
	gtk_accel_group_connect(group, key, modifier, GTK_ACCEL_VISIBLE, closure);
	/* END KEYBOARD accelerators */

	/* Store pointers to all widgets, for use by lookup_widget(). */
	HOOKUP(main_win, about, WDG_ABOUT);
	HOOKUP(main_win, artist_label, WDG_LBL_ARTIST);
	HOOKUP(main_win, albumtitle_label, WDG_LBL_ALBUMTITLE);
	HOOKUP(main_win, genre_label, WDG_LBL_GENRE);
	HOOKUP(main_win, album_artist, WDG_ALBUM_ARTIST);
	HOOKUP(main_win, album_genre, WDG_ALBUM_GENRE);
	HOOKUP(main_win, album_title, WDG_ALBUM_TITLE);
	HOOKUP(main_win, album_year, WDG_ALBUM_YEAR);
	HOOKUP(main_win, disc, WDG_DISC);
	HOOKUP(main_win, lookup, WDG_CDDB);
	HOOKUP(main_win, seek_button, WDG_SEEK);
	HOOKUP(main_win, pick_disc, WDG_PICK_DISC);
	HOOKUP(main_win, pref, WDG_PREFS);
	HOOKUP(main_win, rip_button, WDG_RIP);
	HOOKUP(main_win, single_artist, WDG_SINGLE_ARTIST);
	HOOKUP(main_win, single_genre, WDG_SINGLE_GENRE);
	HOOKUP(main_win, statusLbl, WDG_STATUS);
	HOOKUP(main_win, tracklist, WDG_TRACKLIST);
	HOOKUP(main_win, scroll, WDG_SCROLL);
	HOOKUP(main_win, progress_total, WDG_PROGRESS_TOTAL);
	HOOKUP(main_win, progress_rip, WDG_PROGRESS_RIP);
	HOOKUP(main_win, progress_encode, WDG_PROGRESS_ENCODE);
	HOOKUP(main_win, win_ripping, WDG_RIPPING);
	HOOKUP_NOREF(main_win, main_win, WDG_MAIN);

	return main_win;
}

static GtkWidget *create_prefs(void)
{
	enum {GENERAL_TAB, FILENAMES_TAB,DRIVES_TAB, ENCODE_TAB, ADVANCED_TAB};

	GtkWidget *prefs = gtk_dialog_new();
	gtk_window_set_transient_for(GTK_WINDOW(prefs), GTK_WINDOW(win_main));
	gtk_window_set_title(GTK_WINDOW(prefs), _("Preferences"));
	gtk_window_set_modal(GTK_WINDOW(prefs), TRUE);
	gtk_window_set_type_hint(GTK_WINDOW(prefs), GDK_WINDOW_TYPE_HINT_DIALOG);

	GtkWidget *vbox = GTK_DIALOG(prefs)->vbox;
	gtk_widget_show(vbox);

	GtkWidget *notebook1 = gtk_notebook_new();
	GtkNotebook *tabs = GTK_NOTEBOOK(notebook1);
	gtk_widget_show(notebook1);
	BOXPACK(vbox, notebook1, TRUE, TRUE, 0);

	GtkWidget *frame = NULL;
	GtkWidget *alignment = NULL;
	GtkWidget *wbox = NULL;

	/* GENERAL tab */
	vbox = gtk_vbox_new(FALSE, 5);
	gtk_container_set_border_width(GTK_CONTAINER(vbox), 5);
	gtk_widget_show(vbox);
	gtk_container_add(GTK_CONTAINER(notebook1), vbox);

	GtkWidget *label = gtk_label_new(_("Destination folder"));
	gtk_misc_set_alignment(GTK_MISC(label), 0, 0);
	gtk_widget_show(label);
	BOXPACK(vbox, label, FALSE, FALSE, 0);
	gtk_label_set_use_markup(GTK_LABEL(label), TRUE);

	GtkWidget *hbox = gtk_hbox_new(FALSE, 3);
	gtk_widget_show(hbox);
	BOXPACK(vbox, hbox, FALSE, FALSE, 0);
	GtkWidget *music_folder = gtk_entry_new();
	gtk_widget_show(music_folder);
	BOXPACK(hbox, music_folder, TRUE, TRUE, 0);

	GtkWidget *folder_btn =  gtk_button_new_with_label(" ... ");
	gtk_widget_show(folder_btn);
	GtkTooltips *tip = gtk_tooltips_new();
	gtk_tooltips_set_tip(tip, folder_btn, _("Choose another folder"), NULL);

	CONNECT_SIGNAL(folder_btn, "clicked", on_folder_clicked);
	BOXPACK(hbox, folder_btn, FALSE, FALSE, 0);

	hbox = gtk_hbox_new(FALSE, 4);
	gtk_widget_show(hbox);
	BOXPACK(vbox, hbox, FALSE, FALSE, 2);
	GtkWidget *space = gtk_label_new(_("Free space:"));
	gtk_widget_show(space);
	BOXPACK(hbox, space, FALSE, FALSE, 4);
	GtkWidget *fslabel = gtk_label_new("(Not Available)");
	gtk_widget_show(fslabel);
	BOXPACK(hbox, fslabel, FALSE, FALSE, 4);
	HOOKUP(prefs, fslabel, WDG_LBL_FREESPACE);

	GtkWidget *make_m3u = gtk_check_button_new_with_mnemonic(
													_("Create M3U playlist"));
	gtk_widget_show(make_m3u);
	BOXPACK(vbox, make_m3u, FALSE, FALSE, 6);

	GtkWidget *always_overwrite = gtk_check_button_new_with_mnemonic(
										_("Always overwrite output files"));
	gtk_widget_show(always_overwrite);
	BOXPACK(vbox, always_overwrite, FALSE, FALSE, 2);

	GtkWidget *use_notify = gtk_check_button_new_with_mnemonic(
					_("Display desktop notifications"));
	tip = gtk_tooltips_new();
	gtk_tooltips_set_tip(tip, use_notify, _("This feature requires 'libnotify'."), NULL);
	gtk_widget_show(use_notify);
	BOXPACK(vbox, use_notify, FALSE, FALSE, 2);

	GtkWidget *mb_lookup = gtk_check_button_new_with_mnemonic(
				_("Enable IronSeek engine (very experimental!)"));
	tip = gtk_tooltips_new();
	gtk_tooltips_set_tip(tip, mb_lookup, _("Look up into MusicBrainz for metadata and covers."), NULL);
	gtk_widget_show(mb_lookup);
	BOXPACK(vbox, mb_lookup, FALSE, FALSE, 2);

	label = gtk_label_new(_("General"));
	gtk_widget_show(label);
	gtk_notebook_set_tab_label(tabs, gtk_notebook_get_nth_page(tabs, GENERAL_TAB), label);
	/* END GENERAL tab */


	/* FILENAMES tab */
	vbox = gtk_vbox_new(FALSE, 5);
	gtk_container_set_border_width(GTK_CONTAINER(vbox), 5);
	gtk_widget_show(vbox);
	gtk_container_add(GTK_CONTAINER(notebook1), vbox);

	frame = gtk_frame_new(NULL);
	gtk_widget_show(frame);
	BOXPACK(vbox, frame, FALSE, FALSE, 0);

	vbox = gtk_vbox_new(FALSE, 0);
	gtk_container_set_border_width(GTK_CONTAINER(vbox), 5);
	gtk_widget_show(vbox);
	gtk_container_add(GTK_CONTAINER(frame), vbox);

	label = gtk_label_new(_("%A - Artist\n" "%L - Album\n"
							"%N - Track number (2-digit)\n"
							"%Y - Year (4-digit or \"0\")\n"
							"%T - Song title"));
	gtk_widget_show(label);
	BOXPACK(vbox, label, FALSE, FALSE, 0);
	gtk_misc_set_alignment(GTK_MISC(label), 0, 0);

	label = gtk_label_new(_("%G - Genre"));
	gtk_widget_show(label);
	BOXPACK(vbox, label, FALSE, FALSE, 0);
	gtk_misc_set_alignment(GTK_MISC(label), 0, 0);

	// problem is that the same albumdir is used (threads.c) for all formats
	//~ label = gtk_label_new (_("%F - Format (e.g. FLAC)"));
	//~ gtk_widget_show (label);
	//~ gtk_box_pack_start (GTK_BOX (vbox), label, FALSE, FALSE, 0);
	//~ gtk_misc_set_alignment (GTK_MISC (label), 0, 0);

	GtkWidget *table1 = gtk_table_new(3, 2, FALSE);
	GtkTable *t = GTK_TABLE(table1);
	gtk_widget_show(table1);
	BOXPACK(vbox, table1, TRUE, TRUE, 0);

	label = gtk_label_new(_("Album directory: "));
	gtk_widget_show(label);
	gtk_table_attach(t, label, 0, 1, 0, 1, GTK_FILL, 0, 0, 0);
	gtk_misc_set_alignment(GTK_MISC(label), 0, 0);

	label = gtk_label_new(_("Playlist file: "));
	gtk_widget_show(label);
	gtk_table_attach(t, label, 0, 1, 1, 2, GTK_FILL, 0, 0, 0);
	gtk_misc_set_alignment(GTK_MISC(label), 0, 0);

	label = gtk_label_new(_("Music file: "));
	gtk_widget_show(label);
	gtk_table_attach(t, label, 0, 1, 2, 3, GTK_FILL, 0, 0, 0);
	gtk_misc_set_alignment(GTK_MISC(label), 0, 0);

	GtkWidget *fmt_albumdir = gtk_entry_new();
	gtk_widget_show(fmt_albumdir);
	gtk_table_attach(t, fmt_albumdir, 1, 2, 0, 1, GTK_EXPAND_FILL, 0, 0, 0);

	tip = gtk_tooltips_new();
	gtk_tooltips_set_tip(tip, fmt_albumdir,
		_("This is relative to the destination folder (from the General tab).\n"
		"Can be blank.\n" "Default: %A - %L\n" "Other example: %A/%L"), NULL);

	GtkWidget *fmt_playlist = gtk_entry_new();
	gtk_widget_show(fmt_playlist);
	gtk_table_attach(t, fmt_playlist, 1, 2, 1, 2, GTK_EXPAND_FILL, 0, 0, 0);
	tip = gtk_tooltips_new();
	gtk_tooltips_set_tip(tip, fmt_playlist,
							_("This will be stored in the album directory.\n"
							"Can be blank.\n" "Default: %A - %L"), NULL);

	GtkWidget *fmt_music = gtk_entry_new();
	gtk_widget_show(fmt_music);
	gtk_table_attach(t, fmt_music, 1, 2, 2, 3, GTK_EXPAND_FILL, 0, 0, 0);
	tip = gtk_tooltips_new();
	gtk_tooltips_set_tip(tip, fmt_music,
							_("This will be stored in the album directory.\n"
							"Cannot be blank.\n" "Default: %A - %T\n"
							"Other example: %N - %T"), NULL);

	label = gtk_label_new(_("\nTip: use lowercase letters for simplified names."));
	gtk_widget_show(label);
	BOXPACK(vbox, label, FALSE, FALSE, 0);
	gtk_misc_set_alignment(GTK_MISC(label), 0, 0);

	label = gtk_label_new(_("Filename formats"));
	gtk_widget_show(label);
	gtk_frame_set_label_widget(GTK_FRAME(frame), label);
	gtk_label_set_use_markup(GTK_LABEL(label), TRUE);

	label = gtk_label_new(_("Filenames"));
	gtk_widget_show(label);
	gtk_notebook_set_tab_label(tabs, gtk_notebook_get_nth_page(tabs, FILENAMES_TAB), label);
	/* END FILENAMES tab */

	/* DRIVES tab */
	vbox = gtk_vbox_new(FALSE, 5);
	gtk_container_set_border_width(GTK_CONTAINER(vbox), 5);
	gtk_widget_show(vbox);
	gtk_container_add(GTK_CONTAINER(notebook1), vbox);


	frame = gtk_frame_new(NULL);
	gtk_widget_show(frame);
	BOXPACK(vbox, frame, FALSE, FALSE, 0);
	alignment = gtk_alignment_new(0.5, 0.5, 1, 1);
	gtk_widget_show(alignment);
	gtk_container_add(GTK_CONTAINER(frame), alignment);
	gtk_alignment_set_padding(GTK_ALIGNMENT(alignment), 1, 1, 8, 8);
	wbox = gtk_vbox_new(FALSE, 0);
	gtk_widget_show(wbox);
	gtk_container_add(GTK_CONTAINER(alignment), wbox);

	/* CDROM drives */
	hbox = gtk_hbox_new(FALSE, 0);
	gtk_widget_show(hbox);
	BOXPACK(wbox, hbox, FALSE, FALSE, 0);
	label = gtk_label_new(_("CD-ROM drives: "));
	gtk_widget_show(label);
	BOXPACK(hbox, label, FALSE, FALSE, 0);

	GtkWidget *cdrom_drives = gtk_combo_box_new();
	gtk_widget_show(cdrom_drives);
	BOXPACK(hbox, cdrom_drives, TRUE, TRUE, 0);
	GtkCellRenderer *p_cell = gtk_cell_renderer_text_new();
	GtkCellLayout *layout = GTK_CELL_LAYOUT(cdrom_drives);
	gtk_cell_layout_pack_start(layout, p_cell, FALSE);
	gtk_cell_layout_set_attributes(layout, p_cell, "text", 1, NULL);
	// PATH TO DEVICE
	hbox = gtk_hbox_new(FALSE, 0);
	gtk_widget_show(hbox);
	BOXPACK(wbox, hbox, FALSE, FALSE, 0);
	label = gtk_label_new(_("Path to device: "));
	gtk_widget_show(label);
	BOXPACK(hbox, label, FALSE, FALSE, 0);

	GtkWidget *cdrom = gtk_entry_new();
	gtk_widget_show(cdrom);
	gtk_widget_set_sensitive(cdrom, FALSE);
	BOXPACK(hbox, cdrom, TRUE, TRUE, 0);
	tip = gtk_tooltips_new();
	gtk_tooltips_set_tip(tip, cdrom, _("Default: /dev/cdrom\n"
			"Other example: /dev/hdc\n" "Other example: /dev/sr0"), NULL);
	CONNECT_SIGNAL(cdrom, "focus_out_event", on_cdrom_focus_out);

	CONNECT_SIGNAL(cdrom_drives, "changed", on_s_drive_changed);

	GtkWidget *eject_on_done = gtk_check_button_new_with_mnemonic(
											_("Eject disc when finished"));
	gtk_widget_show(eject_on_done);
	BOXPACK(wbox, eject_on_done, FALSE, FALSE, 4);

	label = gtk_label_new(_("Device"));
	gtk_widget_show(label);
	gtk_frame_set_label_widget(GTK_FRAME(frame), label);
	gtk_label_set_use_markup(GTK_LABEL(label), TRUE);
	/* END of CDROM drives */

	frame = gtk_frame_new(NULL);
	gtk_widget_show(frame);
	BOXPACK(vbox, frame, FALSE, FALSE, 0);
	alignment = gtk_alignment_new(0.5, 0.5, 1, 1);
	gtk_widget_show(alignment);
	gtk_container_add(GTK_CONTAINER(frame), alignment);
	gtk_alignment_set_padding(GTK_ALIGNMENT(alignment), 1, 1, 8, 8);
	wbox = gtk_vbox_new(FALSE, 0);
	gtk_widget_show(wbox);
	gtk_container_add(GTK_CONTAINER(alignment), wbox);

	GtkWidget *rip_fast = gtk_check_button_new_with_mnemonic(
										_("Cdparanoia fast mode"));
	gtk_widget_show(rip_fast);
	BOXPACK(wbox, rip_fast, FALSE, FALSE, 4);

	label = gtk_label_new(_("Cdparanoia options"));
	gtk_widget_show(label);
	gtk_frame_set_label_widget(GTK_FRAME(frame), label);
	gtk_label_set_use_markup(GTK_LABEL(label), TRUE);

	label = gtk_label_new(_("Audio CD"));
	gtk_widget_show(label);
	gtk_notebook_set_tab_label(tabs, gtk_notebook_get_nth_page(tabs, DRIVES_TAB), label);
	/* END DRIVES tab */

	/* ENCODE tab */
	vbox = gtk_vbox_new(FALSE, 0);
	gtk_container_set_border_width(GTK_CONTAINER(vbox), 5);
	gtk_widget_show(vbox);
	gtk_container_add(GTK_CONTAINER(notebook1), vbox);

	/* WAV */
	frame = gtk_frame_new(NULL);
	gtk_frame_set_label(GTK_FRAME(frame), _("Lossless formats"));
	gtk_widget_show(frame);
	BOXPACK(vbox, frame, FALSE, FALSE, 0);
	alignment = gtk_alignment_new(0.5, 0.5, 1, 1);
	gtk_widget_show(alignment);
	gtk_container_add(GTK_CONTAINER(frame), alignment);
	gtk_alignment_set_padding(GTK_ALIGNMENT(alignment), 1, 1, 8, 8);
	wbox = gtk_vbox_new(FALSE, 0);
	gtk_widget_show(wbox);
	gtk_container_add(GTK_CONTAINER(alignment), wbox);


	GtkWidget *rip_wav = gtk_check_button_new_with_label(
										_("WAVE (uncompressed)"));
	gtk_widget_show(rip_wav);
	BOXPACK(wbox, rip_wav, FALSE, FALSE, 0);
	tip = gtk_tooltips_new();
	gtk_tooltips_set_tip(tip, rip_wav,
						_("Original sound quality, big size."), NULL);
	/* END WAV */

	/* FLAC */
	GtkWidget *rip_flac = gtk_check_button_new_with_label(
									_("FLAC (compressed)"));
	gtk_widget_show(rip_flac);
	BOXPACK(wbox, rip_flac, FALSE, FALSE, 0);
	tip = gtk_tooltips_new();
	gtk_tooltips_set_tip(tip, rip_flac,
						_("Original sound quality, smaller size."), NULL);
	CONNECT_SIGNAL(rip_flac, "toggled", on_rip_flac_toggled);
	/* END FLAC */

	/* MP3 */
	frame = gtk_frame_new(NULL);
	gtk_widget_show(frame);
	BOXPACK(vbox, frame, FALSE, FALSE, 0);

	alignment = gtk_alignment_new(0.5, 0.5, 1, 1);
	gtk_widget_show(alignment);
	gtk_container_add(GTK_CONTAINER(frame), alignment);
	gtk_alignment_set_padding(GTK_ALIGNMENT(alignment), 1, 1, 8, 8);

	wbox = gtk_vbox_new(FALSE, 6);
	gtk_widget_show(wbox);
	gtk_container_add(GTK_CONTAINER(alignment), wbox);

	GtkWidget *mp3_vbr = gtk_check_button_new_with_mnemonic(
											_("Variable bit rate (VBR)"));
	gtk_widget_show(mp3_vbr);
	BOXPACK(wbox, mp3_vbr, FALSE, FALSE, 0);
	CONNECT_SIGNAL(mp3_vbr, "toggled", on_vbr_toggled);

	tip = gtk_tooltips_new();
	gtk_tooltips_set_tip(tip, mp3_vbr,
							_("Better quality for the same size."), NULL);

	GtkWidget *hboxcombo = gtk_hbox_new(FALSE, 0);
	gtk_widget_show(hboxcombo);
	BOXPACK(wbox, hboxcombo, FALSE, FALSE, 1);

	GtkWidget *quality_label = gtk_label_new(_("Quality : "));
	gtk_widget_show(quality_label);
	BOXPACK(hboxcombo, quality_label, FALSE, FALSE, 0);
	HOOKUP(prefs, quality_label, WDG_LBL_QUALITY_MP3);

	GtkWidget *mp3_quality = gtk_combo_box_new_text();
	gtk_widget_show(mp3_quality);
	tip = gtk_tooltips_new();
	gtk_tooltips_set_tip(tip, mp3_quality,
						_("Choosing 'High quality' is recommended."), NULL);

	GtkComboBox *cmb_mp3 = GTK_COMBO_BOX(mp3_quality);
	gtk_combo_box_append_text(cmb_mp3, "Low quality");
	gtk_combo_box_append_text(cmb_mp3, "Good quality");
	gtk_combo_box_append_text(cmb_mp3, "High quality (recommended)");
	gtk_combo_box_append_text(cmb_mp3, "Maximum quality");
	gtk_combo_box_set_active(cmb_mp3, 2);
	BOXPACK(hboxcombo, mp3_quality, FALSE, FALSE, 0);
	CONNECT_SIGNAL(mp3_quality, "changed", on_mp3_quality_changed);

	hbox = gtk_hbox_new(FALSE, 0);
	gtk_widget_show(hbox);
	BOXPACK(wbox, hbox, FALSE, FALSE, 0);

	GtkWidget *bitrate_label = gtk_label_new(_("Bitrate : "));
	gtk_widget_show(bitrate_label);
	BOXPACK(hbox, bitrate_label, FALSE, FALSE, 0);
	HOOKUP(prefs, bitrate_label, WDG_LBL_BITRATE);

	int rate = mp3_quality_to_bitrate(g_prefs->mp3_quality,g_prefs->mp3_vbr);
	gchar *kbps = g_strdup_printf(_("%dKbps"), rate);
	label = gtk_label_new(kbps);
	gtk_widget_show(label);
	g_free(kbps);
	BOXPACK(hbox, label, FALSE, FALSE, 0);
	HOOKUP(prefs, label, WDG_BITRATE);

	GtkWidget *rip_mp3 = gtk_check_button_new_with_mnemonic(
												_("MP3 (lossy compression)"));
	gtk_widget_show(rip_mp3);
	gtk_frame_set_label_widget(GTK_FRAME(frame), rip_mp3);
	CONNECT_SIGNAL(rip_mp3, "toggled", on_rip_mp3_toggled);
	/* END MP3 */

	// OGG
	frame = gtk_frame_new(NULL);
	gtk_widget_show(frame);
	BOXPACK(vbox, frame, FALSE, FALSE, 0);

	alignment = gtk_alignment_new(0.5, 0.5, 1, 1);
	gtk_widget_show(alignment);
	gtk_container_add(GTK_CONTAINER(frame), alignment);
	gtk_alignment_set_padding(GTK_ALIGNMENT(alignment), 1, 1, 8, 8);

	wbox = gtk_vbox_new(FALSE, 6);
	gtk_widget_show(wbox);
	gtk_container_add(GTK_CONTAINER(alignment), wbox);

	hboxcombo = gtk_hbox_new(FALSE, 0);
	gtk_widget_show(hboxcombo);
	BOXPACK(wbox, hboxcombo, FALSE, FALSE, 1);

	quality_label = gtk_label_new(_("Quality : "));
	gtk_widget_show(quality_label);
	BOXPACK(hboxcombo, quality_label, FALSE, FALSE, 0);
	HOOKUP(prefs, quality_label, WDG_LBL_QUALITY_OGG);

	GtkWidget *ogg_quality = gtk_hscale_new (GTK_ADJUSTMENT (gtk_adjustment_new (6, 0, 11, 1, 1, 1)));
	gtk_widget_show (ogg_quality);
	tip = gtk_tooltips_new();
	gtk_tooltips_set_tip(tip, ogg_quality,
			_("Higher quality means bigger file. Default is 7 (recommended)."), NULL);

	BOXPACK(hboxcombo, ogg_quality, TRUE, TRUE, 0);
	gtk_scale_set_value_pos (GTK_SCALE (ogg_quality), GTK_POS_RIGHT);
	gtk_scale_set_digits (GTK_SCALE (ogg_quality), 0);

	GtkWidget *rip_ogg = gtk_check_button_new_with_mnemonic (_("OGG Vorbis (lossy compression)"));
	gtk_widget_show (rip_ogg);
	gtk_frame_set_label_widget (GTK_FRAME (frame), rip_ogg);
	CONNECT_SIGNAL(rip_ogg, "toggled", on_rip_ogg_toggled);
	// END OGG

	label = gtk_label_new(_("Encode"));
	gtk_widget_show(label);
	gtk_notebook_set_tab_label(tabs, gtk_notebook_get_nth_page(tabs, ENCODE_TAB), label);
	/* END ENCODE tab */


	/* ADVANCED tab */
	vbox = gtk_vbox_new(FALSE, 5);
	gtk_container_set_border_width(GTK_CONTAINER(vbox), 5);
	gtk_widget_show(vbox);
	gtk_container_add(GTK_CONTAINER(notebook1), vbox);

	frame = gtk_frame_new(NULL);
	gtk_frame_set_label(GTK_FRAME(frame), "CDDB");
	gtk_widget_show(frame);
	BOXPACK(vbox, frame, FALSE, FALSE, 0);

	GtkWidget *frameVbox = gtk_vbox_new(FALSE, 0);
	gtk_widget_show(frameVbox);
	gtk_container_add(GTK_CONTAINER(frame), frameVbox);

	GtkWidget *do_cddb_updates = gtk_check_button_new_with_mnemonic(
						_("Get disc info from the internet automatically"));
	gtk_widget_show(do_cddb_updates);
	BOXPACK(frameVbox, do_cddb_updates, FALSE, FALSE, 0);

	hbox = gtk_hbox_new(FALSE, 0);
	gtk_widget_show(hbox);
	BOXPACK(frameVbox, hbox, FALSE, FALSE, 1);

	label = gtk_label_new(_("Server: "));
	gtk_widget_show(label);
	BOXPACK(hbox, label, FALSE, FALSE, 5);

	GtkWidget *cddbServerName = gtk_entry_new();
	gtk_widget_show(cddbServerName);
	BOXPACK(hbox, cddbServerName, TRUE, TRUE, 5);

	tip = gtk_tooltips_new();
	gtk_tooltips_set_tip(tip, cddbServerName,
								_("The CDDB server to get disc info from"
								   " (default is freedb.freedb.org)"), NULL);
	hbox = gtk_hbox_new(FALSE, 0);
	gtk_widget_show(hbox);
	BOXPACK(frameVbox, hbox, FALSE, FALSE, 1);

	label = gtk_label_new(_("Port: "));
	gtk_widget_show(label);
	BOXPACK(hbox, label, FALSE, FALSE, 5);

	GtkWidget *cddbPortNum = gtk_entry_new();
	gtk_widget_show(cddbPortNum);
	BOXPACK(hbox, cddbPortNum, TRUE, TRUE, 5);

	tip = gtk_tooltips_new();
	gtk_tooltips_set_tip(tip, cddbPortNum,
						_("The CDDB server port (default is 8880)"), NULL);

	GtkWidget *cddb_nocache = gtk_check_button_new_with_label(
											_("Disable CDDB local cache"));
	gtk_widget_show(cddb_nocache);
	BOXPACK(frameVbox, cddb_nocache, FALSE, FALSE, 0);

	frame = gtk_frame_new(NULL);
	gtk_widget_show(frame);
	BOXPACK(vbox, frame, FALSE, FALSE, 0);

	GtkWidget *useProxy = gtk_check_button_new_with_mnemonic(
							_("Use an HTTP proxy to connect to the internet"));
	gtk_widget_show(useProxy);
	gtk_frame_set_label_widget(GTK_FRAME(frame), useProxy);

	frameVbox = gtk_vbox_new(FALSE, 0);
	gtk_widget_show(frameVbox);
	gtk_container_add(GTK_CONTAINER(frame), frameVbox);

	hbox = gtk_hbox_new(FALSE, 0);
	gtk_widget_show(hbox);
	BOXPACK(frameVbox, hbox, FALSE, FALSE, 1);

	label = gtk_label_new(_("Server: "));
	gtk_widget_show(label);
	BOXPACK(hbox, label, FALSE, FALSE, 5);

	GtkWidget *serverName = gtk_entry_new();
	gtk_widget_show(serverName);
	BOXPACK(hbox, serverName, TRUE, TRUE, 5);

	hbox = gtk_hbox_new(FALSE, 0);
	gtk_widget_show(hbox);
	BOXPACK(frameVbox, hbox, FALSE, FALSE, 1);

	label = gtk_label_new(_("Port: "));
	gtk_widget_show(label);
	BOXPACK(hbox, label, FALSE, FALSE, 5);

	GtkWidget *portNum = gtk_entry_new();
	gtk_widget_show(portNum);
	BOXPACK(hbox, portNum, TRUE, TRUE, 5);

	gchar *lbl = g_strdup_printf("Log to %s", g_prefs->log_file);
	GtkWidget *log_file = gtk_check_button_new_with_label(lbl);
	gtk_widget_show(log_file);
	BOXPACK(vbox, log_file, FALSE, FALSE, 0);
	g_free(lbl);

	label = gtk_label_new(_("Advanced"));
	gtk_widget_show(label);
	gtk_notebook_set_tab_label(tabs, gtk_notebook_get_nth_page(tabs, ADVANCED_TAB), label);
	/* END ADVANCED tab */

	GtkWidget *action_area = GTK_DIALOG(prefs)->action_area;
	gtk_widget_show(action_area);
	gtk_button_box_set_layout(GTK_BUTTON_BOX(action_area), GTK_BUTTONBOX_END);

	GtkWidget *cancel = gtk_button_new_from_stock(GTK_STOCK_CANCEL);
	gtk_widget_show(cancel);
	gtk_dialog_add_action_widget(GTK_DIALOG(prefs), cancel, GTK_RESPONSE_CANCEL);
	GTK_WIDGET_SET_FLAGS(cancel, GTK_CAN_DEFAULT);

	GtkWidget *ok = gtk_button_new_from_stock(GTK_STOCK_OK);
	gtk_widget_show(ok);
	gtk_dialog_add_action_widget(GTK_DIALOG(prefs), ok, GTK_RESPONSE_OK);
	GTK_WIDGET_SET_FLAGS(ok, GTK_CAN_DEFAULT);

	CONNECT_SIGNAL(prefs, "response", on_prefs_response);
	CONNECT_SIGNAL(prefs, "realize", on_prefs_show);

	/* Store pointers to all widgets, for use by lookup_widget(). */
	HOOKUP_NOREF(prefs, prefs, "prefs");
	HOOKUP(prefs, cddbServerName, "cddb_server_name");
	HOOKUP(prefs, cddbPortNum, "cddb_port");
	HOOKUP(prefs, useProxy, "use_proxy");
	HOOKUP(prefs, serverName, "server_name");
	HOOKUP(prefs, portNum, "port_number");
	HOOKUP(prefs, log_file, "do_log");
	HOOKUP(prefs, cddb_nocache, "cddb_nocache");
	HOOKUP(prefs, music_folder, WDG_MUSIC_DIR);
	HOOKUP(prefs, make_m3u, "make_playlist");
	HOOKUP(prefs, cdrom, "cdrom");
	HOOKUP(prefs, cdrom_drives, WDG_CDROM_DRIVES);
	HOOKUP(prefs, eject_on_done, "eject_on_done");
	HOOKUP(prefs, always_overwrite, "always_overwrite");
	HOOKUP(prefs, use_notify, "use_notify");
	HOOKUP(prefs, mb_lookup, "mb_lookup");
	HOOKUP(prefs, rip_fast, "rip_fast");
	HOOKUP(prefs, fmt_music, WDG_FMT_MUSIC);
	HOOKUP(prefs, fmt_albumdir, WDG_FMT_ALBUMDIR);
	HOOKUP(prefs, fmt_playlist, WDG_FMT_PLAYLIST);
	HOOKUP(prefs, rip_wav, "rip_wav");
	HOOKUP(prefs, rip_flac, "rip_flac");
	HOOKUP(prefs, mp3_vbr, WDG_MP3VBR);
	HOOKUP(prefs, mp3_quality, WDG_MP3_QUALITY);
	HOOKUP(prefs, ogg_quality, WDG_OGG_QUALITY);
	HOOKUP(prefs, rip_mp3, "rip_mp3");
	HOOKUP(prefs, rip_ogg, "rip_ogg");
	HOOKUP(prefs, do_cddb_updates, "do_cddb_updates");
	return prefs;
}

static GtkWidget *ripping_bar(GtkWidget *table, char *name, int y) {
	GtkTable *t = GTK_TABLE(table);
	GtkWidget *label = gtk_label_new(_(name));
	gtk_widget_show(label);
	gtk_table_attach(t, label, 0, 1, y, y+1, GTK_FILL, 0, 5, 10);
	gtk_misc_set_alignment(GTK_MISC(label), 0, 0.5);
	GtkWidget *progress = gtk_progress_bar_new();
	gtk_widget_show(progress);
	gtk_table_attach(t, progress, 1, 2, y, y+1, GTK_EXPAND_FILL, 0, 5, 10);
	return progress;
}

static void disable_all_main_widgets(void)
{
	gtk_widget_set_sensitive(LKP_MAIN(WDG_TRACKLIST), FALSE);
	disable_widget(WDG_DISC);
	disable_widget(WDG_LBL_GENRE);
	disable_widget(WDG_LBL_ALBUMTITLE);
	disable_widget(WDG_LBL_ARTIST);
	disable_widget(WDG_CDDB);
	disable_widget(WDG_SEEK);
	disable_widget(WDG_PREFS);
	disable_widget(WDG_RIP);
	disable_widget(WDG_SINGLE_ARTIST);
	disable_widget(WDG_SINGLE_GENRE);
	disable_widget(WDG_ALBUM_ARTIST);
	disable_widget(WDG_ALBUM_GENRE);
	disable_widget(WDG_ALBUM_TITLE);
	disable_widget(WDG_ALBUM_YEAR);
	disable_widget(WDG_PICK_DISC);
}

static void disable_lookup_widgets(void)
{
	gtk_widget_set_sensitive(LKP_MAIN(WDG_TRACKLIST), FALSE);
	disable_widget(WDG_DISC);
	disable_widget(WDG_LBL_GENRE);
	disable_widget(WDG_LBL_ALBUMTITLE);
	disable_widget(WDG_LBL_ARTIST);
	disable_widget(WDG_CDDB);
	disable_widget(WDG_SEEK);
	disable_widget(WDG_RIP);
	disable_widget(WDG_SINGLE_ARTIST);
	disable_widget(WDG_SINGLE_GENRE);
	disable_widget(WDG_ALBUM_ARTIST);
	disable_widget(WDG_ALBUM_GENRE);
	disable_widget(WDG_ALBUM_TITLE);
	disable_widget(WDG_ALBUM_YEAR);
	disable_widget(WDG_PICK_DISC);
}

static void enable_all_main_widgets(void)
{
	gtk_widget_set_sensitive(LKP_MAIN(WDG_TRACKLIST), TRUE);
	enable_widget(WDG_DISC);
	enable_widget(WDG_LBL_GENRE);
	enable_widget(WDG_LBL_ALBUMTITLE);
	enable_widget(WDG_LBL_ARTIST);
	enable_widget(WDG_CDDB);
	if(g_prefs->mb_lookup) enable_widget(WDG_SEEK);
	enable_widget(WDG_PREFS);
	enable_widget(WDG_RIP);
	enable_widget(WDG_SINGLE_ARTIST);
	enable_widget(WDG_SINGLE_GENRE);
	enable_widget(WDG_ALBUM_ARTIST);
	enable_widget(WDG_ALBUM_GENRE);
	enable_widget(WDG_ALBUM_TITLE);
	enable_widget(WDG_ALBUM_YEAR);
	enable_widget(WDG_PICK_DISC);
}

static void disable_mp3_widgets(void)
{
	gtk_widget_set_sensitive(LKP_PREF(WDG_MP3VBR), FALSE);
	gtk_widget_set_sensitive(LKP_PREF(WDG_MP3_QUALITY), FALSE);
	gtk_widget_set_sensitive(LKP_PREF(WDG_BITRATE), FALSE);
	gtk_widget_set_sensitive(LKP_PREF(WDG_LBL_BITRATE), FALSE);
	gtk_widget_set_sensitive(LKP_PREF(WDG_LBL_QUALITY_MP3), FALSE);
}
static void disable_ogg_widgets(void)
{
	gtk_widget_set_sensitive(LKP_PREF(WDG_OGG_QUALITY), FALSE);
	gtk_widget_set_sensitive(LKP_PREF(WDG_LBL_QUALITY_OGG), FALSE);
}
static void enable_mp3_widgets(void)
{
	gtk_widget_set_sensitive(LKP_PREF(WDG_MP3VBR), TRUE);
	gtk_widget_set_sensitive(LKP_PREF(WDG_MP3_QUALITY), TRUE);
	gtk_widget_set_sensitive(LKP_PREF(WDG_BITRATE), TRUE);
	gtk_widget_set_sensitive(LKP_PREF(WDG_LBL_BITRATE), TRUE);
	gtk_widget_set_sensitive(LKP_PREF(WDG_LBL_QUALITY_MP3), TRUE);
}
static void enable_ogg_widgets(void)
{
	gtk_widget_set_sensitive(LKP_PREF(WDG_OGG_QUALITY), TRUE);
	gtk_widget_set_sensitive(LKP_PREF(WDG_LBL_QUALITY_OGG), TRUE);
}

static void show_completed_dialog()
{
#define CREATOK " created successfully"
#define CREATKO "There was an error creating "

	int numOk = g_data->numCdparanoiaOk + g_data->numLameOk
					+ g_data->numOggOk + g_data->numFlacOk;

	int numFailed = g_data->numCdparanoiaFailed + g_data->numLameFailed
					+ g_data->numOggFailed + g_data->numFlacFailed;

    if(numFailed > 0) {
		if(g_prefs->use_notify) {
			gchar *m = g_strdup_printf(ngettext(CREATKO "%d file", CREATKO "%d files", numFailed), numFailed);
			notify(m);
			g_free(m);
		}
		DIALOG_WARNING_CLOSE(
				ngettext(CREATKO "%d file", CREATKO "%d files", numFailed),
				numFailed);
	} else {
		if(g_prefs->use_notify) {
			gchar *m = g_strdup_printf(
						ngettext("%d file" CREATOK, "%d files" CREATOK, numOk),
						numOk);
			notify(m);
			g_free(m);
		}
		DIALOG_INFO_CLOSE(
				ngettext("%d file" CREATOK, "%d files" CREATOK, numOk),
				numOk);
	}
}

// free memory allocated for prefs struct
// also frees any strings pointed to in the struct
static void free_prefs(prefs * p)
{
	g_free(p);
}

// returns a new prefs struct with all members set to nice default values
// this struct must be freed with delete_prefs()
static prefs *get_default_prefs()
{
	prefs *p = g_malloc0(sizeof(prefs));
	p->do_cddb_updates = 1;
	p->do_log = 0;
	p->max_log_size = 5000; // kb
	szcopy(p->log_file, DEFAULT_LOG_FILE);
	const gchar *user = g_getenv("USER");
	if(user!=NULL && *user!='\0') {
		snprintf(p->log_file, SZENTRY, "/tmp/%s.%s.log", PACKAGE, user);
	}
	p->eject_on_done = 0;
	p->always_overwrite = 0;
	p->use_notify = 0;
	p->mb_lookup = 0;
	p->rip_fast = 0;
	p->main_window_height = 380;
	p->main_window_width = 520;
	p->make_playlist = 1;
	p->mp3_quality = 2; // 0:low, 1:good, 2:high, 3:max
	p->ogg_quality = 7; // from 0:low to 10:max
	p->mp3_vbr = 1;
	p->rip_mp3 = 1;
	p->rip_ogg = 0;
	p->rip_wav = 0;
	p->rip_flac = 0;
	p->use_proxy = 0;
	p->port_number = DEFAULT_PROXY_PORT;
	p->cddb_port= DEFAULT_CDDB_SERVER_PORT;
	p->cddb_nocache= 1;
	gchar *v = get_default_music_dir();
	szcopy(p->music_dir, v);
	g_free(v);
	szcopy(p->cdrom, "/dev/cdrom");
	szcopy(p->tmp_cdrom, p->cdrom);
	szcopy(p->drive, "DEFAULT");
	szcopy(p->format_music, "%N - %A - %T");
	szcopy(p->format_playlist, "%A - %L");
	szcopy(p->format_albumdir, "%A - %L - %Y");
	szcopy(p->server_name, "10.0.0.1");
	szcopy(p->cddb_server_name, DEFAULT_CDDB_SERVER);
	return p;
}

static void set_pref_toggle(char *widget_name, int on_off)
{
	gtk_toggle_button_set_active(
					GTK_TOGGLE_BUTTON(LKP_PREF(widget_name)), on_off);
}

static GList* MakeDriveCombo()
{
	GList *l = g_data->drive_list;
	GList *r = NULL;

	if (l != NULL && g_list_length(l) > 0) {
		for (GList *c = g_list_first(l); c != NULL; c = g_list_next(c)) {
			s_drive *d = clone_drive((s_drive *) c->data);
			r = g_list_append(r, d);
		}
	}
	r = g_list_append(r, create_drive("AUTO", "Scan all CD-ROM drives"));
	r = g_list_append(r, create_drive("DEFAULT", "Default '/dev/cdrom' device"));
	r = g_list_append(r, create_drive("MANUAL", "User-defined device path"));
	return r;
}

// sets up all of the widgets in the preferences dialog to
// match the given prefs struct
static void set_widgets_from_prefs(prefs *p)
{
	set_pref_text("cddb_server_name", p->cddb_server_name);
	set_pref_text(WDG_FMT_ALBUMDIR, p->format_albumdir);
	set_pref_text(WDG_FMT_MUSIC, p->format_music);
	set_pref_text(WDG_FMT_PLAYLIST, p->format_playlist);
	set_pref_text("server_name", p->server_name);

	prefs_get_music_dir(p);
	set_pref_text(WDG_MUSIC_DIR, p->music_dir);

	gtk_combo_box_set_active(COMBO_MP3Q, p->mp3_quality);
    gtk_range_set_value(SCALE_OGGQ, p->ogg_quality);

	set_pref_toggle("do_cddb_updates", p->do_cddb_updates);
	set_pref_toggle("do_log", p->do_log);
	set_pref_toggle("eject_on_done", p->eject_on_done);
	set_pref_toggle("always_overwrite", p->always_overwrite);
	set_pref_toggle("use_notify", p->use_notify);
	set_pref_toggle("mb_lookup", p->mb_lookup);
	set_pref_toggle("rip_fast", p->rip_fast);
	set_pref_toggle("make_playlist", p->make_playlist);
	set_pref_toggle(WDG_MP3VBR, p->mp3_vbr);
	set_pref_toggle("rip_mp3", p->rip_mp3);
	set_pref_toggle("rip_ogg", p->rip_ogg);
	set_pref_toggle("rip_wav", p->rip_wav);
	set_pref_toggle("rip_flac", p->rip_flac);
	set_pref_toggle("use_proxy", p->use_proxy);
	set_pref_toggle("cddb_nocache", p->cddb_nocache);

	gchar *num = g_strdup_printf("%d", p->port_number);
	set_pref_text("port_number", num);
	g_free(num);
	num = g_strdup_printf("%d", p->cddb_port);
	set_pref_text("cddb_port", num);
	g_free(num);

	set_pref_text("cdrom", p->cdrom);

	GList *cmb = MakeDriveCombo();
	GtkListStore *store = gtk_list_store_new(2, G_TYPE_STRING, G_TYPE_STRING);
	gtk_combo_box_set_model(COMBO_DRIVE, GTK_TREE_MODEL(store));
	GtkTreeIter iter;
	int active = 0;
	int i = 0;
	if (cmb != NULL) {
		for(GList *curr = g_list_first(cmb);
					curr != NULL; curr = g_list_next(curr)) {
			s_drive *drive = (s_drive *) curr->data;
			TRACEINFO("Model: [%s] Device [%s]", drive->model, drive->device);
			gtk_list_store_append(store, &iter);
			gtk_list_store_set(store, &iter, 0, drive->device,
											 1, drive->model, -1);
			if(g_prefs->drive
				&& (strcmp(drive->model,g_prefs->drive)==0
					|| strcmp(drive->device,g_prefs->drive)==0)
				) {
				// printf("DRIVE ACTIVE %d\n",i);
				active = i;
			}
			i++;
		}
		gtk_combo_box_set_active(COMBO_DRIVE, active);
	}
	g_list_free_full(cmb, free_drive);
	// disable mp3 widgets if needed
	if(!(p->rip_mp3)) disable_mp3_widgets();
	if(!(p->rip_ogg)) disable_ogg_widgets();
}

static int get_pref_toggle(char *widget_name)
{
	return gtk_toggle_button_get_active(
						GTK_TOGGLE_BUTTON(LKP_PREF(widget_name)));
}

static int get_main_toggle(char *widget_name)
{
	return gtk_toggle_button_get_active(
						GTK_TOGGLE_BUTTON(LKP_MAIN(widget_name)));
}

// populates a prefs struct from the current state of the widgets
static void get_prefs_from_widgets(prefs *p)
{
	GtkTreeModel *m = gtk_combo_box_get_model(COMBO_DRIVE);
	GtkTreeIter iter;
	gchar *device = NULL;
	gchar *model = NULL;
	GtkComboBox *combo = COMBO_DRIVE;
	if(gtk_combo_box_get_active_iter(combo, &iter)) {
		gtk_tree_model_get(m, &iter, 0, &device, 1, &model, -1);
		gint i = gtk_combo_box_get_active(COMBO_DRIVE);
		gint l = g_list_length(g_data->drive_list);
		if(i < l) {
			strcpy(p->drive, model);
			// printf("INSIDE MODELS => '%s'\n", p->drive);
		} else {
			strcpy(p->drive, device);
			// printf("OUTSIDE MODELS => '%s'\n", p->drive);
		}
		g_free(device);
		g_free(model);
	}
	strcpy(p->cdrom,p->tmp_cdrom);
	szcopy(p->music_dir, GET_PREF_TEXT(WDG_MUSIC_DIR));
	szcopy(p->format_music, GET_PREF_TEXT(WDG_FMT_MUSIC));
	szcopy(p->format_playlist, GET_PREF_TEXT(WDG_FMT_PLAYLIST));
	szcopy(p->format_albumdir, GET_PREF_TEXT(WDG_FMT_ALBUMDIR));
	szcopy(p->cddb_server_name, GET_PREF_TEXT("cddb_server_name"));
	szcopy(p->server_name, GET_PREF_TEXT("server_name"));
	p->mp3_quality = gtk_combo_box_get_active(COMBO_MP3Q);
	p->ogg_quality = gtk_range_get_value(SCALE_OGGQ);
	p->make_playlist = get_pref_toggle("make_playlist");
	p->rip_wav = get_pref_toggle("rip_wav");
	p->rip_flac = get_pref_toggle("rip_flac");
	p->rip_ogg = get_pref_toggle("rip_ogg");
	p->rip_mp3 = get_pref_toggle("rip_mp3");
	p->mp3_vbr = get_pref_toggle(WDG_MP3VBR);
	p->eject_on_done = get_pref_toggle("eject_on_done");
	p->always_overwrite = get_pref_toggle("always_overwrite");
	p->use_notify = get_pref_toggle("use_notify");
	p->mb_lookup = get_pref_toggle("mb_lookup");
	p->rip_fast = get_pref_toggle("rip_fast");
	p->do_cddb_updates = get_pref_toggle("do_cddb_updates");
	p->use_proxy = get_pref_toggle("use_proxy");
	p->cddb_nocache = get_pref_toggle("cddb_nocache");
	p->do_log = get_pref_toggle("do_log");
	p->port_number = atoi(GET_PREF_TEXT("port_number"));
	p->cddb_port= atoi(GET_PREF_TEXT("cddb_port"));
}

static gchar* get_config_path(const gchar *file_suffix)
{
	gchar *filename = NULL;
	gchar *path = NULL;
	const gchar *home = g_getenv("HOME");
	const gchar *xch = g_getenv("XDG_CONFIG_HOME");
	if(xch == NULL || *xch == '\0') {
		path = g_strdup_printf("%s/.config/%s", home, PACKAGE);
	} else {
		path = g_strdup_printf("%s/%s", xch, PACKAGE);
	}
	if(!mkdir_p(path)) {
		goto cleanup;
	}
	if(file_suffix == NULL) {
		filename = g_strdup_printf("%s/%s.conf", path, PACKAGE);
	} else {
		filename = g_strdup_printf("%s/%s.%s", path, PACKAGE, file_suffix);
	}
cleanup:
	g_free(path);
	return filename;
}

// store the given prefs struct to the config file
static void save_prefs(prefs *p)
{
	gchar *file = get_config_path(NULL);
	if(!file) {
		TRACERROR("could not save configuration: %s", strerror(errno));
		return;
	}
	FILE *fd = fopen(file, "w");
	if(fd == NULL) {
		TRACEWARN("could not save file: %s", strerror(errno));
		g_free(file);
		return;
	}
	fprintf(fd, "DEVICE=%s\n", p->cdrom);
	fprintf(fd, "DRIVE=%s\n", p->drive);
	fprintf(fd, "MUSIC_DIR=%s\n", p->music_dir);
	fprintf(fd, "MAKE_PLAYLIST=%d\n", p->make_playlist);
	fprintf(fd, "FORMAT_MUSIC=%s\n", p->format_music);
	fprintf(fd, "FORMAT_PLAYLIST=%s\n", p->format_playlist);
	fprintf(fd, "FORMAT_ALBUMDIR=%s\n", p->format_albumdir);
	fprintf(fd, "RIP_WAV=%d\n", p->rip_wav);
	fprintf(fd, "RIP_FLAC=%d\n", p->rip_flac);
	fprintf(fd, "RIP_OGG=%d\n", p->rip_ogg);
	fprintf(fd, "RIP_MP3=%d\n", p->rip_mp3);
	fprintf(fd, "MP3_VBR=%d\n", p->mp3_vbr);
	fprintf(fd, "MP3_QUALITY=%d\n", p->mp3_quality);
	fprintf(fd, "OGG_QUALITY=%d\n", p->ogg_quality);
	fprintf(fd, "WINDOW_WIDTH=%d\n", p->main_window_width);
	fprintf(fd, "WINDOW_HEIGHT=%d\n", p->main_window_height);
	fprintf(fd, "EJECT=%d\n", p->eject_on_done);
	fprintf(fd, "OVERWRITE=%d\n", p->always_overwrite);
	fprintf(fd, "NOTIFY=%d\n", p->use_notify);
	fprintf(fd, "SEEK_MUSICBRAINZ=%d\n", p->mb_lookup);
	fprintf(fd, "CDPARANOIA_ZMODE=%d\n", p->rip_fast);
	fprintf(fd, "CDDB_UPDATE=%d\n", p->do_cddb_updates);
	fprintf(fd, "USE_PROXY=%d\n", p->use_proxy);
	fprintf(fd, "SERVER_NAME=%s\n", p->server_name);
	fprintf(fd, "PORT=%d\n", p->port_number);
	fprintf(fd, "DO_LOG=%d\n", p->do_log);
	fprintf(fd, "LOG_FILE=%s\n", p->log_file);
	fprintf(fd, "MAX_LOG_SIZE=%d\n", p->max_log_size);
	fprintf(fd, "CDDB_SERVER_NAME=%s\n", p->cddb_server_name);
	fprintf(fd, "CDDB_PORT=%d\n", p->cddb_port);
	fprintf(fd, "CDDB_NOCACHE=%d\n", p->cddb_nocache);
	fclose(fd);
	g_free(file);
	TRACEINFO("Configuration saved.");
}

static void set_device_from_drive(char *drive)
{
	if(strcmp(drive,"MANUAL")==0) {
		strcpy(g_data->device, g_prefs->cdrom);
	} else if(strcmp(drive,"DEFAULT")==0) {
		strcpy(g_data->device, "/dev/cdrom");
	} else if(strcmp(drive,"AUTO")==0) {
	} else {
	}
}

// load the prefereces from the config file into the given prefs struct
static void load_prefs()
{
	g_prefs = get_default_prefs();
	prefs *p = g_prefs;
	gchar *file = get_config_path(NULL);
	if(!file) {
		TRACEWARN("could not get configuration file path: %s", strerror(errno));
		return;
	}
	char *buf = NULL;
	char *s = NULL; // start position into buffer
	if(!is_file(file)) {
		g_free(file);
		return;
	}
	if(!load_file(file, &buf)) {
		g_free(file);
		return;
	}
	s = buf; // Start of HEADER section

	if(get_field_as_szentry(s, file, "DEVICE", p->cdrom))
		szcopy(p->tmp_cdrom, p->cdrom);

	if(get_field_as_szentry(s, file, "DRIVE", p->drive))
		set_device_from_drive(p->drive);

	get_field_as_szentry(s, file, "MUSIC_DIR", p->music_dir);
	get_field_as_long(s,file,"MAKE_PLAYLIST", &(p->make_playlist));
	get_field_as_long(s,file,"RIP_WAV", &(p->rip_wav));
	get_field_as_long(s,file,"RIP_FLAC", &(p->rip_flac));
	get_field_as_long(s,file,"RIP_OGG", &(p->rip_ogg));
	get_field_as_long(s,file,"RIP_MP3", &(p->rip_mp3));
	get_field_as_long(s,file,"MP3_VBR", &(p->mp3_vbr));

	int i = 0;
	if(get_field_as_long(s,file,"MP3_QUALITY", &i)) {;
		if(i > -1 && i < 4) p->mp3_quality = i;
	}
	i=0;
	if(get_field_as_long(s,file,"OGG_QUALITY", &i)) {;
		if(i > -1 && i < 11) p->ogg_quality = i;
	}
	i=0;
	get_field_as_long(s,file,"WINDOW_WIDTH", &i);
	if (i >= 200) p->main_window_width = i;
	i=0;
	get_field_as_long(s,file,"WINDOW_HEIGHT", &i);
	if (i >= 150) p->main_window_height = i;

	get_field_as_long(s,file,"DO_LOG", &(p->do_log));
	get_field_as_long(s,file,"MAX_LOG_SIZE", &(p->max_log_size));
	get_field_as_szentry(s, file, "LOG_FILE", p->log_file);
	get_field_as_long(s,file,"EJECT", &(p->eject_on_done));
	get_field_as_long(s,file,"OVERWRITE", &(p->always_overwrite));
	get_field_as_long(s,file,"NOTIFY", &(p->use_notify));
	get_field_as_long(s,file,"SEEK_MUSICBRAINZ", &(p->mb_lookup));
	get_field_as_long(s,file,"CDPARANOIA_ZMODE", &(p->rip_fast));
	get_field_as_long(s,file,"CDDB_UPDATE", &(p->do_cddb_updates));
	get_field_as_long(s,file,"USE_PROXY",&(p->use_proxy));
	get_field_as_long(s,file,"CDDB_NOCACHE",&(p->cddb_nocache));
	get_field_as_long(s,file,"PORT",&(p->port_number));
	if (p->port_number == 0 || !is_valid_port_number(p->port_number)) {
		printf("bad port number read from config file, using %d instead\n",
														DEFAULT_PROXY_PORT);
		p->port_number = DEFAULT_PROXY_PORT;
	}
	get_field_as_long(s,file,"CDDB_PORT",&(p->cddb_port));
	if (p->cddb_port== 0 || !is_valid_port_number(p->cddb_port)) {
		printf("bad port number read from config file, using %d instead\n",
													DEFAULT_CDDB_SERVER_PORT);
		p->cddb_port= DEFAULT_CDDB_SERVER_PORT;
	}
	get_field_as_szentry(s, file, "SERVER_NAME", p->server_name);
	get_field_as_szentry(s, file, "CDDB_SERVER_NAME", p->cddb_server_name);
	get_field_as_szentry(s, file, "FORMAT_MUSIC", p->format_music);
	get_field_as_szentry(s, file, "FORMAT_PLAYLIST", p->format_playlist);
	get_field_as_szentry(s, file, "FORMAT_ALBUMDIR", p->format_albumdir);
	if(buf) {
		g_free(buf);
	}
	g_free(file);
}

// use this method when reading the "music_dir" field of a prefs struct
// it will make sure it always points to a nice directory
static char *prefs_get_music_dir(prefs *p)
{
	if(is_directory(p->music_dir)) {
		return p->music_dir;
	}
	gchar *newdir = get_default_music_dir();
	if(strcmp(newdir,p->music_dir)) {
		DIALOG_ERROR_OK("The music directory '%s' does not exist (or the path does not lead to a directory).\n\n"
				"The music directory will be reset to '%s'.", p->music_dir, newdir);
		szcopy(p->music_dir, newdir);
	}
	mkdir_p(newdir);
	g_free(newdir);
	save_prefs(p);
	return p->music_dir;
}

static int is_valid_port_number(int number)
{
	if (number >= 0 && number <= 65535)
		return 1;
	else
		return 0;
}

static bool prefs_are_valid(void)
{
	bool somethingWrong = false;

	// playlistfile
	if (string_has_slashes(GET_PREF_TEXT(WDG_FMT_PLAYLIST))) {
		DIALOG_ERROR_OK(_("Invalid characters in the '%s' field"),
						_("Playlist file: "));
		somethingWrong = true;
	}
	// musicfile
	if (string_has_slashes(GET_PREF_TEXT(WDG_FMT_MUSIC))) {
		DIALOG_ERROR_OK(_("Invalid characters in the '%s' field"),
						_("Music file: "));
		somethingWrong = true;
	}
	if (strlen(GET_PREF_TEXT(WDG_FMT_MUSIC)) == 0) {
		DIALOG_ERROR_OK(_("'%s' cannot be empty"), _("Music file: "));
		somethingWrong = true;
	}
	// proxy port
	int port_number;
	int rc = sscanf(GET_PREF_TEXT("port_number"), "%d", &port_number);
	if (rc != 1 || !is_valid_port_number(port_number)) {
		DIALOG_ERROR_OK(_("Invalid proxy port number"));
		somethingWrong = true;
	}
	// cddb server port
	int cddb_port;
	rc = sscanf(GET_PREF_TEXT("cddb_port"), "%d", &cddb_port);
	if (rc != 1 || !is_valid_port_number(cddb_port)) {
		DIALOG_ERROR_OK(_("Invalid cddb server port number"));
		somethingWrong = true;
	}
	if (somethingWrong)
		return false;
	else
		return true;
}

static bool string_has_slashes(const char *string)
{
	int count;
	for (count = strlen(string) - 1; count >= 0; count--) {
		if (string[count] == '/')
			return true;
	}
	return false;
}

static bool confirmOverwrite(const char *pathAndName)
{
	if (g_prefs->always_overwrite) return true;
	if (g_data->overwriteAll) return true;
	if (g_data->overwriteNone) return false;

	GtkWidget *dialog = gtk_dialog_new_with_buttons(_("Overwrite?"),
					     GTK_WINDOW(win_main),
					     GTK_DIALOG_DESTROY_WITH_PARENT,
					     GTK_STOCK_YES, GTK_RESPONSE_ACCEPT,
						 GTK_STOCK_NO, GTK_RESPONSE_REJECT, NULL);
	gtk_dialog_set_default_response(GTK_DIALOG(dialog), GTK_RESPONSE_REJECT);

	char *lastSlash = strrchr(pathAndName, '/');
	lastSlash++;

	gchar *m = g_strdup_printf(_("The file '%s' already exists."
								" Do you want to overwrite it?\n"), lastSlash);
	GtkWidget *label = gtk_label_new(m);
	g_free(m);
	gtk_widget_show(label);
	BOXPACK(GTK_DIALOG(dialog)->vbox, label, TRUE, TRUE, 0);

	GtkWidget *checkbox = gtk_check_button_new_with_mnemonic(
			_("Remember the answer for _all the files made from this CD"));
	gtk_widget_show(checkbox);
	BOXPACK(GTK_DIALOG(dialog)->vbox, checkbox, TRUE, TRUE, 0);

	int rc = gtk_dialog_run(GTK_DIALOG(dialog));
	if (gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(checkbox))) {
		if (rc == GTK_RESPONSE_ACCEPT)
			g_data->overwriteAll = true;
		else
			g_data->overwriteNone = true;
	}
	gtk_widget_destroy(dialog);

	if (rc == GTK_RESPONSE_ACCEPT)
		return true;
	else
		return false;
}

/*   RIPPING
 */
// aborts ripping- stops all the threads and return to normal execution
static void abort_threads()
{
    g_data->aborted = true;
    if (g_data->cdparanoia_pid != 0) {
        kill(g_data->cdparanoia_pid, SIGKILL);
	}
    if (g_data->lame_pid != 0) {
        kill(g_data->lame_pid, SIGKILL);
	}
    if (g_data->oggenc_pid != 0) {
        kill(g_data->oggenc_pid, SIGKILL);
	}
    if (g_data->flac_pid != 0) {
        kill(g_data->flac_pid, SIGKILL);
	}
    // wait until all the worker threads are done
    while (g_data->cdparanoia_pid != 0
			|| g_data->lame_pid != 0
			|| g_data->oggenc_pid != 0
			|| g_data->flac_pid != 0) {
        Sleep(200);
		TRACEINFO("cdparanoia=%d lame=%d ogg=%d flac=%d", g_data->cdparanoia_pid,
						g_data->lame_pid, g_data->oggenc_pid, g_data->flac_pid);
    }
    g_cond_signal(g_data->available);
    g_thread_join(g_data->ripper);
    g_thread_join(g_data->encoder);
    g_thread_join(g_data->tracker);
    // gdk_flush();
    g_data->working = false;
    show_completed_dialog();
    gtk_window_set_title(GTK_WINDOW(win_main), PROGRAM_NAME);
    gtk_widget_hide(LKP_MAIN(WDG_RIPPING));
	gtk_widget_show(LKP_MAIN(WDG_SCROLL));
	enable_all_main_widgets();
	set_status("Job cancelled.");
	GTK_REFRESH;
}

static void on_cancel_clicked(GtkButton *button, gpointer user_data)
{
	TRACEINFO("on_cancel_clicked !!");
	abort_threads();
}

void make_playlist(const char *filename, FILE ** file)
{
	bool makePlaylist = true;
	struct stat statStruct;

	int rc = stat(filename, &statStruct);
	if (rc == 0 && !confirmOverwrite(filename)) {
		makePlaylist = false;
	}
	*file = NULL;
	if (makePlaylist) {
		*file = fopen(filename, "w");
		if (*file == NULL) {
			DIALOG_ERROR_OK("Unable to create WAV playlist \"%s\": %s",
								filename, strerror(errno));
		} else {
			fprintf(*file, "#EXTM3U\n");
		}
	}
}

// uses cdparanoia to rip a WAV track from a cdrom
// cdrom    - the path to the cdrom device
// tracknum - the track to rip
// filename - the name of the output WAV file
static void cdparanoia(char *cdrom, int tracknum, char *filename)
{
	gchar *trk = g_strdup_printf("%d", tracknum);
	int pos = 0;
	const char *args[32];
	args[pos++] = CDPARANOIA_PRG;
	if(g_prefs->rip_fast) args[pos++] = "-Z";
	args[pos++] = "-e";
	args[pos++] = "-d";
	args[pos++] = cdrom;
	args[pos++] = trk;
	args[pos++] = filename;
	args[pos++] = NULL;

	int fd = exec_with_output(args, STDERR_FILENO, &g_data->cdparanoia_pid);
	fd_set readfds;
	FD_ZERO(&readfds);
	FD_SET(fd, &readfds);
	TRACEINFO("Start of ripping track %d, fd=%d", tracknum, fd);

	// to convert the progress number stat cdparanoia spits out
	// into sector numbers divide by 1176
	// note: only use the "[wrote]" numbers
	char buf[256];
	int size = 0;
	int start = 0;
	int end = 0;
	do {
		int pos = -1;
		do {
			/* Setup a timeout for read */
			struct timeval timeout;
			timeout.tv_sec = 16;
			timeout.tv_usec = 0;
			if(select(32, &readfds, NULL, NULL, &timeout) != 1) {
				TRACEWARN("cdparanoia is FROZEN !");
				goto cleanup;
			}
			pos++;
			size = read(fd, &buf[pos], 1);
			/* signal interrupted read(), try again */
			if (size == -1) {
				if(errno == EINTR) {
					TRACEWARN("signal interrupted read(),"
								" try again at position %d", pos);
					pos--;
					size = 1;
				} else {
					perror("Read error from cdparanoia");
					TRACEWARN("read error");
				}
			}
		} while ((buf[pos] != '\n') && (size > 0) && (pos < 256));
		buf[pos] = '\0';

		if ((buf[0] == 'R') && (buf[1] == 'i')) {
			sscanf(buf, "Ripping from sector %d", &start);
		} else if (buf[0] == '\t') {
			sscanf(buf, "\t to sector %d", &end);
		} else if (buf[0] == '#') {
			int code = 0;
			char type[200];
			int sector = 0;
			sscanf(buf, "##: %d %s @ %d", &code, type, &sector);
			sector /= 1176;
			if (strncmp("[wrote]", type, 7) == 0) {
				g_data->rip_percent = (double)(sector - start) / (end - start);
			}
		} else {
			// printf("Unknown data : %s\n", buf);
		}
	} while (size > 0);

cleanup:
	TRACEINFO("End of ripping track %d / %f %%", tracknum, g_data->rip_percent);
	close(fd);
	sigchld();
	//  don't go on until the signal for the previous call is handled
	if (g_data->cdparanoia_pid != 0) {
		kill(g_data->cdparanoia_pid, SIGKILL);
	}
	g_free(trk);
}

static void rip_track(int tracknum, const char *trackartist,
										const char *tracktitle)
{
	const char *albumartist = GET_MAIN_TEXT(WDG_ALBUM_ARTIST);
	const char *albumtitle = GET_MAIN_TEXT(WDG_ALBUM_TITLE);
	const char *albumyear = GET_MAIN_TEXT(WDG_ALBUM_YEAR);
	const char *albumgenre = GET_MAIN_TEXT(WDG_ALBUM_GENRE);

	char *albumdir = parse_format(g_prefs->format_albumdir, 0, albumyear,
									albumartist, albumtitle, albumgenre, NULL);

	char *musicfilename = parse_format(g_prefs->format_music, tracknum,
				albumyear, trackartist, albumtitle, albumgenre, tracktitle);

	char *wavfilename = make_filename(prefs_get_music_dir(g_prefs), albumdir,
														musicfilename, "wav");
	if (g_data->aborted) g_thread_exit(NULL);
	struct stat statStruct;
	bool doRip = true;;
	int rc = stat(wavfilename, &statStruct);
	if (rc == 0) {
		gdk_threads_enter();
		if (!confirmOverwrite(wavfilename)) doRip = false;
		gdk_threads_leave();
	}
	if (doRip) {
		cdparanoia(g_data->device, tracknum, wavfilename);
	}
	g_free(albumdir);
	g_free(musicfilename);
	g_free(wavfilename);
	g_data->rip_percent = 0.0;
	g_data->rip_tracks_completed++;
	TRACEINFO("NOW rip_tracks_completed = %d", g_data->rip_tracks_completed);
}

// the thread that handles ripping tracks to WAV files
static gpointer rip_thread(gpointer data)
{
	GtkListStore *store = get_tracklist_store();
	gboolean single_artist = get_main_toggle(WDG_SINGLE_ARTIST);
	const char *albumartist = GET_MAIN_TEXT(WDG_ALBUM_ARTIST);

	GtkTreeIter iter;
	GtkTreeModel *tree_store = GTK_TREE_MODEL(store);
	gboolean rowsleft = gtk_tree_model_get_iter_first(tree_store, &iter);

	while (rowsleft) {
		int riptrack;
		int tracknum;
		char *trackartist;
		char *trackyear;
		char *tracktitle;
		gtk_tree_model_get(tree_store, &iter,
							   COL_RIPTRACK, &riptrack,
							   COL_TRACKNUM, &tracknum,
							   COL_TRACKARTIST, &trackartist,
							   COL_TRACKTITLE, &tracktitle,
							   COL_YEAR, &trackyear, -1);

		if (single_artist) {
			g_free(trackartist);
			trackartist = g_strdup(albumartist);
		}
		if (riptrack) {
			rip_track(tracknum, trackartist, tracktitle);
		}
		g_free(trackartist);
		g_free(trackyear);
		g_free(tracktitle);
		g_mutex_lock(g_data->barrier);
		g_data->counter++;
		g_mutex_unlock(g_data->barrier);
		g_cond_signal(g_data->available);
		if (g_data->aborted) g_thread_exit(NULL);
		rowsleft = gtk_tree_model_iter_next(tree_store, &iter);
	}
	// no more tracks to rip, safe to eject
	if (g_prefs->eject_on_done) {
		set_gui_action(ACTION_EJECTING, true);
		eject_disc(g_data->device);
	}
	return NULL;
}

static void set_status(char *text) {
	GtkWidget *s = LKP_MAIN(WDG_STATUS);
	gtk_label_set_text(GTK_LABEL(s), _(text));
	gtk_label_set_use_markup(GTK_LABEL(s), TRUE);
}

static void fmt_status(const char *fmt, ...) {
	gchar *str = NULL;
	va_list args;
	va_start(args,fmt);
	g_vasprintf(&str, fmt, args);
	va_end(args);
	set_status(str);
	g_free(str);
}

static void reset_counters()
{
	g_data->working = true;
	g_data->aborted = false;
	g_data->allDone = false;
	g_data->counter = 0;
	g_data->barrier = g_mutex_new();
	g_data->available = g_cond_new();
	g_data->mp3_percent = 0.0;
	g_data->ogg_percent = 0.0;
	g_data->flac_percent = 0.0;
	g_data->rip_tracks_completed = 0;
	g_data->encode_tracks_completed = 0;
	g_data->numCdparanoiaFailed = 0;
	g_data->numLameFailed = 0;
	g_data->numOggFailed = 0;
	g_data->numFlacFailed = 0;
	g_data->numCdparanoiaOk = 0;
	g_data->numLameOk = 0;
	g_data->numOggOk = 0;
	g_data->numFlacOk = 0;
	g_data->rip_percent = 0.0;
}

// spawn needed threads and begin ripping
static void dorip_mainthread()
{
	reset_counters();
	if (!g_prefs->rip_wav && !g_prefs->rip_mp3
			&& !g_prefs->rip_ogg && !g_prefs->rip_flac) {
		DIALOG_ERROR_OK(_("No ripping/encoding method selected."
						" Please enable one from the 'Preferences' menu."));
		return;
	}
	const char *albumartist = GET_MAIN_TEXT(WDG_ALBUM_ARTIST);
	const char *albumtitle = GET_MAIN_TEXT(WDG_ALBUM_TITLE);
	const char *albumyear = GET_MAIN_TEXT(WDG_ALBUM_YEAR);
	const char *albumgenre = GET_MAIN_TEXT(WDG_ALBUM_GENRE);
	g_data->overwriteAll = false;
	g_data->overwriteNone = false;
	GtkListStore *store = get_tracklist_store();
	GtkTreeModel *tree_store = GTK_TREE_MODEL(store);
	GtkTreeIter iter;
	gboolean rowsleft = gtk_tree_model_get_iter_first(tree_store, &iter);
	g_data->tracks_to_rip = 0;
	while (rowsleft) {
		int riptrack;
		gtk_tree_model_get(tree_store, &iter, COL_RIPTRACK, &riptrack, -1);
		if (riptrack) g_data->tracks_to_rip++;
		rowsleft = gtk_tree_model_iter_next(tree_store, &iter);
	}
	if (g_data->tracks_to_rip == 0) {
		DIALOG_ERROR_OK(_("No tracks were selected for ripping/encoding."
							" Please select at least one track."));
		return;
	}
	char *albumdir = parse_format(g_prefs->format_albumdir, 0,
						albumyear, albumartist, albumtitle, albumgenre, NULL);
	char *playlist = parse_format(g_prefs->format_playlist, 0,
						albumyear, albumartist, albumtitle, albumgenre, NULL);

	/* CREATE the album directory */
	char *dirpath = make_filename(prefs_get_music_dir(g_prefs), albumdir,
																NULL, NULL);
	TRACEINFO("Making album directory '%s'", dirpath);

	if (recursive_mkdir(dirpath) != 0 && errno != EEXIST) {
		DIALOG_ERROR_OK("Unable to create directory '%s': %s",
									dirpath, strerror(errno));
		g_free(dirpath);
		g_free(albumdir);
		g_free(playlist);
		return;
	}
	g_free(dirpath);
	/* END CREATE the album directory */

	if (g_prefs->make_playlist) {
		if (g_prefs->rip_wav) {
			char *filename = make_filename(prefs_get_music_dir(g_prefs),
											albumdir, playlist, "wav.m3u");
			make_playlist(filename, &(g_data->playlist_wav));
			g_free(filename);
		}
		if (g_prefs->rip_mp3) {
			char *filename = make_filename(prefs_get_music_dir(g_prefs),
											albumdir, playlist, "mp3.m3u");
			make_playlist(filename, &(g_data->playlist_mp3));
			g_free(filename);
		}
		if (g_prefs->rip_ogg) {
			char *filename = make_filename(prefs_get_music_dir(g_prefs),
											albumdir, playlist, "ogg.m3u");
			make_playlist(filename, &(g_data->playlist_ogg));
			g_free(filename);
		}
		if (g_prefs->rip_flac) {
			char *filename = make_filename(prefs_get_music_dir(g_prefs),
											albumdir, playlist, "flac.m3u");
			make_playlist(filename, &(g_data->playlist_flac));
			g_free(filename);
		}

	}
	g_free(albumdir);
	g_free(playlist);

	set_status("Working...");
	disable_all_main_widgets();
	gtk_widget_show(LKP_MAIN(WDG_RIPPING));
	gtk_widget_hide(LKP_MAIN(WDG_SCROLL));
	GTK_REFRESH;

	g_data->ripper = g_thread_create(rip_thread, NULL, TRUE, NULL);
	g_data->encoder = g_thread_create(encode_thread, NULL, TRUE, NULL);
	g_data->tracker = g_thread_create(track_thread, NULL, TRUE, NULL);
}

static void read_from_encoder(int encoder, int fd)
{
	int size = 0;
	do {
		char buf[256];
		char *p = buf;
		memset(buf,0,256);
		int pos = -1;
		do {
			pos++;
			size = read(fd, &buf[pos], 1);
			/* signal interrupted read(), try again */
			if (size == -1 && errno == EINTR) {
				pos--;
				size = 1;
			}
		} while ((buf[pos] != '\r') && (buf[pos] != '\n') && (buf[pos] != 0x08)
							&& (size > 0) && (pos < 255));
		buf[pos] = '\0';

		while(*p==0x08) p++;
		if(*p==0 && size>0) continue;
		// printf("LINE=%s\n", p);

		int sector;
		int end;
		gchar *s;
		switch(encoder) {
			case FLAC:
				{
					s = g_strrstr(p, "% complete,");
					if(!s) continue;
					*s=0;
					for(s=s-2; s>p; s--) {
					   if(*s==':') p=s+1;
					}
					if (sscanf(p, "%d%%", &sector) == 1) {
						g_data->flac_percent = (double)sector/100;
					}
				}
				break;

			case LAME:
				{
					s = g_strrstr(p, ")|");
					if(!s) continue;
					*s=0;
					for(s=s-1; s>p; s--) {
					   if(*s=='(') *s=0;
					}
					if (sscanf(p, "%d/%d", &sector, &end) == 2) {
						g_data->mp3_percent = (double)sector / end;
					}
				}
				break;

			case OGGENC:
				{
					if (sscanf(buf, "\t[\t%d.%d%%]", &sector, &end) == 2) {
						g_data->ogg_percent =(double) (sector + (end * 0.1)) / 100;
					} else if (sscanf(buf, "\t[\t%d,%d%%]", &sector, &end) == 2) {
						g_data->ogg_percent = (double) (sector + (end * 0.1)) / 100;
					}
				}
				break;
		}
	} while (size > 0);
}

static void lamehq(int tracknum, char *artist, char *album, char *title,
				char *genre, char *year, char *wavfilename, char *mp3filename)
{
	TRACEINFO("Genre: %s Artist: %s Title: %s", genre, artist, title);

	// nice lame -q 0 -v -V 0 $file #  && rm $file
	//  --cbr -b 224
	//  -v -V0 -b 190
	//
	int pos = 0;
	const char *args[32];
	args[pos++] = LAME_PRG;
	args[pos++] = "--nohist";
	args[pos++] = "-q";
	args[pos++] = "8";
	args[pos++] = "--id3v2-only";
	if(g_prefs->mp3_vbr) {
		args[pos++] = "-V";
		args[pos++] = "0";
	} else {
		args[pos++] = "--cbr";
	}
	args[pos++] = "-b";
	int bitrate = mp3_quality_to_bitrate(g_prefs->mp3_quality,g_prefs->mp3_vbr);
	char br_txt[8];
	snprintf(br_txt, 8, "%d", bitrate);
	args[pos++] = br_txt;

	char tr_txt[4];
	snprintf(tr_txt, 4, "%d", tracknum);
	if ((tracknum > 0) && (tracknum < 100)) {
		args[pos++] = "--tn";
		args[pos++] = tr_txt;
	}
	if ((artist != NULL) && (strlen(artist) > 0)) {
		args[pos++] = "--ta";
		args[pos++] = artist;
	}
	if ((album != NULL) && (strlen(album) > 0)) {
		args[pos++] = "--tl";
		args[pos++] = album;
	}
	if ((title != NULL) && (strlen(title) > 0)) {
		args[pos++] = "--tt";
		args[pos++] = title;
	}
	// lame refuses some genres that come from cddb.
	// Users can edit the genre field.
	if ((genre != NULL) && (strlen(genre))) {
		args[pos++] = "--tg";
		args[pos++] = genre;
	}
	if ((year != NULL) && (strlen(year) > 0)) {
		args[pos++] = "--ty";
		args[pos++] = year;
	}
	args[pos++] = wavfilename;
	args[pos++] = mp3filename;
	args[pos++] = NULL;

	int fd = exec_with_output(args, STDERR_FILENO, &g_data->lame_pid);
	read_from_encoder(LAME, fd);
	close(fd);
	Sleep(200);
	sigchld();
}

static void oggenc(int tracknum, char *artist, char *album, char *title,
				char *genre, char *year, char *wavfilename, char *oggfilename)
{
	const char *args[19];
	int pos = 0;
	args[pos++] = OGGENC_PRG;
	args[pos++] = "-q";
	char quality_level_text[3];
	snprintf(quality_level_text, 3, "%d", g_prefs->ogg_quality);
	args[pos++] = quality_level_text;
	char tracknum_text[4];
	snprintf(tracknum_text, 4, "%d", tracknum);
	if ((tracknum > 0) && (tracknum < 100)) {
		args[pos++] = "-N";
		args[pos++] = tracknum_text;
	}
	if ((artist != NULL) && (strlen(artist) > 0)) {
		args[pos++] = "-a";
		args[pos++] = artist;
	}
	if ((album != NULL) && (strlen(album) > 0)) {
		args[pos++] = "-l";
		args[pos++] = album;
	}
	if ((title != NULL) && (strlen(title) > 0)) {
		args[pos++] = "-t";
		args[pos++] = title;
	}
	if ((year != NULL) && (strlen(year) > 0)) {
		args[pos++] = "-d";
		args[pos++] = year;
	}
	if ((genre != NULL) && (strlen(genre) > 0)) {
		args[pos++] = "-G";
		args[pos++] = genre;
	}
	args[pos++] = wavfilename;
	args[pos++] = "-o";
	args[pos++] = oggfilename;
	args[pos++] = NULL;

	int fd = exec_with_output(args, STDERR_FILENO, &g_data->oggenc_pid);
	read_from_encoder(OGGENC, fd);
	close(fd);
	Sleep(200);
	sigchld();
}

static gchar* make_flac_arg(char *field, char *value)
{
	if(!field || !value) {
		fatalError("NULL parameter to make_flac_arg.");
		return NULL;
	}
	return g_strdup_printf("%s=%s", field, value);
}

static void push_flac_tag(const char *args[], int *pos, char *tag)
{
	if(!args || !pos || !tag) return;

	int i = *pos;
	if(strlen(tag) > 0) {
		args[i++] = "-T";
		args[i++] = tag;
		*pos = i;
	}
}

// uses the FLAC reference encoder to encode a WAV file into a FLAC and tag it
//
// tracknum - the track number
// artist - the artist's name
// album - the album the song came from
// title - the name of the song
// wavfilename - the path to the WAV file to encode
// flacfilename - the path to the output FLAC file
// compression_level - how hard to compress the file (0-8) see flac man page
static void flac(int tracknum, char *artist, char *album, char *title,
				char *genre, char *year, char *wavfilename,
				char *flacfilename, int compression_level)
{
	char compression_level_text[3];
	snprintf(compression_level_text, 3, "-%d", compression_level);
	char tracknum_text[19];
	snprintf(tracknum_text, 18, "TRACKNUMBER=%d", tracknum);

	gchar *artist_text = make_flac_arg("ARTIST", artist);
	gchar *album_text = make_flac_arg("ALBUM", album);
	gchar *title_text = make_flac_arg("TITLE", title);
	gchar *genre_text = make_flac_arg("GENRE", genre);
	gchar *year_text = make_flac_arg("DATE", year);

	const char *args[19];
	int pos = 0;
	args[pos++] = "flac";
	args[pos++] = "-f";
	args[pos++] = compression_level_text;
	if ((tracknum > 0) && (tracknum < 100)) {
		push_flac_tag(args,&pos,tracknum_text);
	}
	push_flac_tag(args,&pos,artist_text);
	push_flac_tag(args,&pos,album_text);
	push_flac_tag(args,&pos,title_text);
	push_flac_tag(args,&pos,genre_text);
	push_flac_tag(args,&pos,year_text);
	args[pos++] = wavfilename;
	args[pos++] = "-o";
	args[pos++] = flacfilename;
	args[pos++] = NULL;

	int fd = exec_with_output(args, STDERR_FILENO, &g_data->flac_pid);
	read_from_encoder(FLAC, fd);
	close(fd);
	g_data->flac_percent = 1.0;
	g_free(artist_text);
	g_free(album_text);
	g_free(title_text);
	g_free(genre_text);
	g_free(year_text);
	Sleep(200);
	sigchld();
}

static void set_main_completion(const gchar *widget_name)
{
	GtkWidget *w = LKP_MAIN(widget_name);
	add_completion(w);
	save_completion(w);
}

static void encode_track(char *genre, int tracknum, char *trackartist,
									char *tracktitle, char *tracktime)
{
	char *album_artist = (char *)GET_MAIN_TEXT(WDG_ALBUM_ARTIST);
	char *album_year   = (char *)GET_MAIN_TEXT(WDG_ALBUM_YEAR);
	char *album_title  = (char *)GET_MAIN_TEXT(WDG_ALBUM_TITLE);
	// char *album_genre  = GET_MAIN_TEXT(WDG_ALBUM_GENRE);

	char *albumdir = parse_format(g_prefs->format_albumdir, 0,
						album_year, album_artist, album_title, genre, NULL);

	char *musicfilename = parse_format(g_prefs->format_music, tracknum,
					album_year, trackartist, album_title, genre, tracktitle);

	char *wavfilename = make_filename(prefs_get_music_dir(g_prefs),
										albumdir, musicfilename, "wav");

	char *mp3filename = make_filename(prefs_get_music_dir(g_prefs),
										albumdir, musicfilename, "mp3");

	char *oggfilename = make_filename(prefs_get_music_dir(g_prefs),
										albumdir, musicfilename, "ogg");

	char *flacfilename = make_filename(prefs_get_music_dir(g_prefs),
										albumdir, musicfilename, "flac");
	g_free(musicfilename);
	g_free(albumdir);
	int min;
	int sec;
	sscanf(tracktime, "%d:%d", &min, &sec);

	if (g_prefs->rip_mp3) {
		TRACEINFO("MP3 encoding track %d to '%s'", tracknum, mp3filename);
		if (g_data->aborted) g_thread_exit(NULL);

		struct stat statStruct;
		int	rc = stat(mp3filename, &statStruct);
		bool doEncode = true;
		if (rc == 0) {
			gdk_threads_enter();
			if (!confirmOverwrite(mp3filename)) doEncode = false;
			gdk_threads_leave();
		}
		if (doEncode) {
			lamehq(tracknum, trackartist, album_title, tracktitle,
					genre, album_year, wavfilename, mp3filename);
		}
		if (g_data->aborted) g_thread_exit(NULL);

		if (g_data->playlist_mp3) {
			fprintf(g_data->playlist_mp3, "#EXTINF:%d,%s - %s\n",
					(min * 60) + sec, trackartist, tracktitle);
			fprintf(g_data->playlist_mp3, "%s\n", basename(mp3filename));
			fflush(g_data->playlist_mp3);
		}
	}
	g_free(mp3filename);

	if (g_prefs->rip_ogg) {
		TRACEINFO("Ogg encoding track %d to '%s'", tracknum, oggfilename);
		if (g_data->aborted) g_thread_exit(NULL);

		struct stat statStruct;
		int	rc = stat(oggfilename, &statStruct);
		bool doEncode = true;
		if (rc == 0) {
			gdk_threads_enter();
			if (!confirmOverwrite(oggfilename)) doEncode = false;
			gdk_threads_leave();
		}
		if (doEncode) {
			oggenc(tracknum, trackartist, album_title, tracktitle,
					genre, album_year, wavfilename, oggfilename);
		}
		if (g_data->aborted) g_thread_exit(NULL);

		if (g_data->playlist_ogg) {
			fprintf(g_data->playlist_ogg, "#EXTINF:%d,%s - %s\n",
					(min * 60) + sec, trackartist, tracktitle);
			fprintf(g_data->playlist_ogg, "%s\n", basename(oggfilename));
			fflush(g_data->playlist_ogg);
		}
	}
	g_free(oggfilename);


	if (g_prefs->rip_flac) {
		TRACEINFO("Flac Encoding track %d to '%s'", tracknum, flacfilename);
		if (g_data->aborted) g_thread_exit(NULL);

		struct stat statStruct;
		int	rc = stat(flacfilename, &statStruct);
		bool doEncode = true;
		if (rc == 0) {
			gdk_threads_enter();
			if (!confirmOverwrite(flacfilename)) doEncode = false;
			gdk_threads_leave();
		}
		if (doEncode) {
			flac(tracknum, trackartist, album_title, tracktitle,
					genre, album_year, wavfilename, flacfilename,
					8 /*compression */);
		}
		if (g_data->aborted) g_thread_exit(NULL);

		if (g_data->playlist_flac) {
			fprintf(g_data->playlist_flac, "#EXTINF:%d,%s - %s\n",
					(min * 60) + sec, trackartist, tracktitle);
			fprintf(g_data->playlist_flac, "%s\n", basename(flacfilename));
			fflush(g_data->playlist_flac);
		}
	}
	g_free(flacfilename);

	if (!g_prefs->rip_wav) {
		TRACEINFO("Removing track %d WAV file", tracknum);
		if (unlink(wavfilename) != 0) {
			TRACEWARN("Unable to delete WAV file \"%s\": %s",
					wavfilename, strerror(errno));
		}
	} else {
		if (g_data->playlist_wav) {
			fprintf(g_data->playlist_wav, "#EXTINF:%d,%s - %s\n",
					(min * 60) + sec, trackartist, tracktitle);
			fprintf(g_data->playlist_wav, "%s\n", basename(wavfilename));
			fflush(g_data->playlist_wav);
		}
	}
	g_free(wavfilename);
	g_data->mp3_percent = 0.0;
	g_data->ogg_percent = 0.0;
	g_data->flac_percent = 0.0;
	g_data->encode_tracks_completed++;
}

// the thread that handles encoding WAV files to all other formats
static gpointer encode_thread(gpointer data)
{
	set_gui_action(ACTION_COMPLETION, true);

	gboolean single_artist = get_main_toggle(WDG_SINGLE_ARTIST);
	gboolean single_genre = get_main_toggle(WDG_SINGLE_GENRE);
	GtkListStore *store = get_tracklist_store();
	GtkTreeIter iter;
	GtkTreeModel *tree_store = GTK_TREE_MODEL(store);
	gboolean row = gtk_tree_model_get_iter_first(tree_store, &iter);

	while (row) {
		g_mutex_lock(g_data->barrier);
		while ((g_data->counter < 1) && (!g_data->aborted)) {
			g_cond_wait(g_data->available, g_data->barrier);
		}
		g_data->counter--;
		g_mutex_unlock(g_data->barrier);
		if (g_data->aborted) g_thread_exit(NULL);

		int riptrack = 0;
		int tracknum = 0;
		char *genre = NULL;
		char *trackartist = NULL;
		char *tracktime = NULL;
		char *tracktitle = NULL;
		char *trackyear = NULL;
		gtk_tree_model_get(tree_store, &iter,
							   COL_RIPTRACK, &riptrack,
							   COL_TRACKNUM, &tracknum,
							   COL_TRACKARTIST, &trackartist,
							   COL_TRACKTITLE, &tracktitle,
							   COL_TRACKTIME, &tracktime,
							   COL_GENRE, &genre,
							   COL_YEAR, &trackyear, -1);
		if (single_artist) {
			g_free(trackartist);
			trackartist = g_strdup(GET_MAIN_TEXT(WDG_ALBUM_ARTIST));
		}
		if (single_genre) {
			g_free(genre);
			genre = g_strdup(GET_MAIN_TEXT(WDG_ALBUM_GENRE));
		}
		if (riptrack) {
			encode_track(genre, tracknum, trackartist, tracktitle, tracktime);
		}
		g_free(genre);
		g_free(trackartist);
		g_free(tracktime);
		g_free(tracktitle);
		g_free(trackyear);

		if (g_data->aborted) g_thread_exit(NULL);
		row = gtk_tree_model_iter_next(tree_store, &iter);
	}

	if (g_data->playlist_wav) fclose(g_data->playlist_wav);
	g_data->playlist_wav = NULL;
	if (g_data->playlist_mp3) fclose(g_data->playlist_mp3);
	g_data->playlist_mp3 = NULL;
	if (g_data->playlist_ogg) fclose(g_data->playlist_ogg);
	g_data->playlist_ogg = NULL;
	if (g_data->playlist_flac) fclose(g_data->playlist_flac);
	g_data->playlist_flac = NULL;

	g_mutex_free(g_data->barrier);
	g_data->barrier = NULL;
	g_cond_free(g_data->available);
	g_data->available = NULL;

	TRACEINFO("rip_percent= %f %%", g_data->rip_percent);
	/* wait until all the worker threads are done */
	while (g_data->cdparanoia_pid != 0 
			|| g_data->lame_pid != 0
			|| g_data->oggenc_pid != 0
			|| g_data->flac_pid != 0) {
		// printf("w2\n");
		Sleep(200);
	}
	TRACEINFO("Waking up to allDone");
	g_data->allDone = true;		// so the tracker thread will exit
	g_data->working = false;
	set_gui_action(ACTION_ENCODED, true);
	return NULL;
}

// calculates the progress of the other threads and updates the progress bars
static gpointer track_thread(gpointer data)
{
	g_data->parts = 1;
	if (g_prefs->rip_mp3) g_data->parts++;
	if (g_prefs->rip_ogg) g_data->parts++;
	if (g_prefs->rip_flac) g_data->parts++;

	set_gui_action(ACTION_INIT_PBAR, true);

	int torip;
	int completed;
	bool started = false;
	g_data->prip = 0;
	g_data->pencode = 0;
	g_data->ptotal = 0.0;

	while (!g_data->allDone) {
		if (g_data->aborted) {
			TRACEWARN("Aborted 1");
			g_thread_exit(NULL);
		}
		if(!started && g_data->rip_percent <= 0.0
			&& (g_data->parts == 1
				|| g_data->flac_percent <= 0.0
				|| g_data->ogg_percent <= 0.0
				|| g_data->mp3_percent <= 0.0)) {
			Sleep(200);
			continue;
		}
		started = true;
		torip = g_data->tracks_to_rip;
		completed = g_data->rip_tracks_completed;
		g_data->prip = (completed + g_data->rip_percent) / torip;
		snprintf(g_data->srip, 32, "%02.2f%% (%d/%d)", (g_data->prip * 100),
				(completed < torip) ? (completed + 1) : torip, torip);

		if(g_data->prip>1.0) {
			TRACEWARN("prip overflow=%.2f%% completed=%d rip=%.2f%% remain=%d",
						g_data->prip, completed, g_data->rip_percent, torip);
			g_data->prip=1.0;
		}
		if (g_data->parts > 1) {
			completed = g_data->encode_tracks_completed;
			g_data->pencode = ((double)completed / (double)torip)
					+ ((g_data->mp3_percent + g_data->ogg_percent + g_data->flac_percent)
							/ (g_data->parts - 1) / torip);

			snprintf(g_data->sencode, 13, "%d%% (%d/%d)",
						(int)(g_data->pencode * 100),
						(completed < torip) ? (completed + 1) : torip, torip);

			g_data->ptotal = g_data->prip / g_data->parts
								+ g_data->pencode * (g_data->parts - 1)
								/ g_data->parts;

			// TRACEINFO("prip=%.4f pencode=%.4f ptotal=%.4f", g_data->prip, g_data->pencode, g_data->ptotal);
		} else {
			g_data->ptotal = g_data->prip;
		}
		if (g_data->aborted) {
			TRACEWARN("Aborted 2");
			g_thread_exit(NULL);
		}
		set_gui_action(ACTION_UPDATE_PBAR, false);
		Sleep(200);
	}
	set_gui_action(ACTION_READY, true);
	TRACEINFO("The End.");
	return NULL;
}

// signal handler to find out when our child has exited
static void sigchld()
{
	if (g_prefs == NULL)
		return;

	int status = -1;
	pid_t pid = wait(&status);
	TRACEINFO("waited for %d, status=%d (know about wav %d, mp3 %d, ogg %d, flac %d)",
					pid, status, g_data->cdparanoia_pid,
					g_data->lame_pid, g_data->oggenc_pid, g_data->flac_pid);

	if (pid != g_data->cdparanoia_pid
			&& pid != g_data->lame_pid
			&& pid != g_data->oggenc_pid
			&& pid != g_data->flac_pid) {
		TRACERROR("unknown pid '%d', please report bug.", pid);
	}
	if (WIFEXITED(status)) {
		// TRACEINFO("exited, status=%d", WEXITSTATUS(status));
	} else if (WIFSIGNALED(status)) {
		TRACEINFO("killed by signal %d", WTERMSIG(status));
	} else if (WIFSTOPPED(status)) {
		TRACEINFO("stopped by signal %d", WSTOPSIG(status));
	} else if (WIFCONTINUED(status)) {
		TRACEINFO("continued");
	}
	if (status != 0) {
		if (pid == g_data->cdparanoia_pid) {
			g_data->cdparanoia_pid = 0;
			if (g_prefs->rip_wav)
				g_data->numCdparanoiaFailed++;
		} else if (pid == g_data->lame_pid) {
			g_data->lame_pid = 0;
			g_data->numLameFailed++;
		} else if (pid == g_data->oggenc_pid) {
			g_data->oggenc_pid = 0;
			g_data->numOggFailed++;
		} else if (pid == g_data->flac_pid) {
			g_data->flac_pid = 0;
			g_data->numFlacFailed++;
		}
	} else {
		if (pid == g_data->cdparanoia_pid) {
			g_data->cdparanoia_pid = 0;
			if (g_prefs->rip_wav)
				g_data->numCdparanoiaOk++;
		} else if (pid == g_data->lame_pid) {
			g_data->lame_pid = 0;
			g_data->numLameOk++;
		} else if (pid == g_data->oggenc_pid) {
			g_data->oggenc_pid = 0;
			g_data->numOggOk++;
		} else if (pid == g_data->flac_pid) {
			g_data->flac_pid = 0;
			g_data->numFlacOk++;
		}
	}
}

// fork() and exec() the file listed in "args"
//
// args - a valid array for execvp()
// toread - the file descriptor to pipe back to the parent
// p - a place to write the PID of the exec'ed process
// returns - a file descriptor that reads the program output on "toread"
static int exec_with_output(const char *args[], int toread, pid_t *p)
{
	int pipefd[2];

	if (pipe(pipefd) != 0)
		fatalError("exec_with_output(): failed to create a pipe");

	if ((*p = fork()) == 0) { // im the child, i execute the command
		close(pipefd[0]); // close the side of the pipe we don't need
		// setup output
		dup2(pipefd[1], toread);
		close(pipefd[1]);
		// call execvp
		execvp(args[0], (char **)args);
		// should never get here
		fatalError("exec_with_output(): execvp() failed");
	}
	// i'm the parent
	TRACEINFO("%d started: %s", *p, args[0]);
	// close the side of the pipe we don't need
	close(pipefd[1]);
	return pipefd[0];
}

static gboolean cb_gui_update(gpointer data)
{
	static int i = 0;
	i++;

	g_mutex_lock(g_data->monolock);
	if(g_data->action == NO_ACTION)
		goto unlock;

	//GtkWidget *s = LKP_MAIN(WDG_STATUS);
	//fmt_status("[%4d] %s", i, gtk_label_get_text(GTK_LABEL(s)));

	GtkProgressBar *pbar_total = GTK_PROGRESS_BAR(LKP_MAIN(WDG_PROGRESS_TOTAL));
	GtkProgressBar *pbar_rip = GTK_PROGRESS_BAR(LKP_MAIN(WDG_PROGRESS_RIP));
	GtkProgressBar *pbar_encode = GTK_PROGRESS_BAR(LKP_MAIN(WDG_PROGRESS_ENCODE));
	//TRACEINFO("cb_gui_update (%d/%s)", g_data->action, action2str(g_data->action));
	switch(g_data->action) {
		case ACTION_INIT_PBAR:
			gtk_progress_bar_set_fraction(pbar_total, 0.0);
			gtk_progress_bar_set_text(pbar_total, _("Waiting..."));
			gtk_progress_bar_set_fraction(pbar_rip, 0.0);
			gtk_progress_bar_set_text(pbar_rip, _("Waiting..."));
			if (g_data->parts > 1) {
				gtk_progress_bar_set_fraction(pbar_encode, 0.0);
				gtk_progress_bar_set_text(pbar_encode, _("Waiting..."));
			} else {
				gtk_progress_bar_set_fraction(pbar_encode, 1.0);
				gtk_progress_bar_set_text(pbar_encode, "100% (0/0)");
			}
			g_data->action = NO_ACTION;
			break;

		case ACTION_UPDATE_PBAR:
			gtk_progress_bar_set_fraction(pbar_rip, g_data->prip);
			gtk_progress_bar_set_text(pbar_rip, g_data->srip);
			if (g_data->parts > 1) {
				gtk_progress_bar_set_fraction(pbar_encode, g_data->pencode);
				gtk_progress_bar_set_text(pbar_encode, g_data->sencode);
			}
			gtk_progress_bar_set_fraction(pbar_total, g_data->ptotal);
			gchar *stotal = g_strdup_printf("%02.2f%%", g_data->ptotal * 100);
			gtk_progress_bar_set_text(pbar_total, stotal);
			gchar *windowTitle = g_strdup_printf("%s - %s", PROGRAM_NAME, stotal);
			gtk_window_set_title(GTK_WINDOW(win_main), windowTitle);
			g_free(windowTitle);
			g_free(stotal);
			g_data->action = NO_ACTION;
			break;

		case ACTION_READY:
			set_status("Ready.");
			gtk_window_set_title(GTK_WINDOW(win_main), PROGRAM_NAME);
			g_data->action = NO_ACTION;
			break;

		case ACTION_COMPLETION:
			set_main_completion(WDG_ALBUM_ARTIST);
			set_main_completion(WDG_ALBUM_TITLE);
			set_main_completion(WDG_ALBUM_GENRE);
			g_data->action = NO_ACTION;
			break;

		case ACTION_ENCODED:
			show_completed_dialog();
			gtk_widget_hide(LKP_MAIN(WDG_RIPPING));
			gtk_widget_show(LKP_MAIN(WDG_SCROLL));
			enable_all_main_widgets();
			g_data->action = NO_ACTION;
			break;

		case ACTION_EJECTING:
			fmt_status("Trying to eject disc in '%s'...", g_data->device);
			g_data->action = NO_ACTION;
			break;

		case ACTION_FREESPACE:
			{
				GtkLabel *lbl = GTK_LABEL(LKP_PREF(WDG_LBL_FREESPACE));
				gtk_label_set_markup(lbl, g_data->label_space);
			}
			g_data->action = NO_ACTION;
			break;
	}
	g_cond_signal(g_data->updated);
unlock:
	g_mutex_unlock(g_data->monolock);
	return TRUE;
}

#ifdef NDEBUG
  #define dprintf(s)  printf("%s:%d: %s: %s\n",__FILE__,__LINE__,__func__,s)
#else
  #define dprintf(s)
#endif

#define M_ArrayLen(a) (sizeof(a) / sizeof(a[0]))

typedef struct xmlnode *xmlnode_t;
bool        XmlParseFd(struct xmlnode **n,int fd);
const char *XmlParseStr(struct xmlnode **n,const char *s);
const char *XmlGetTag(struct xmlnode *n);
const char *XmlGetAttribute(struct xmlnode *n, const char *attr);
const char *XmlGetContent(struct xmlnode *n);
int         XmlTagStrcmp(struct xmlnode *n, const char *str);
struct xmlnode *XmlFindSubNode(struct xmlnode *n, const char *tag);
struct xmlnode *XmlFindSubNodeFree(struct xmlnode *n, const char *tag);
void XmlDestroy(struct xmlnode **n);

struct xmlnode {
    char *tag;
    char *content;
    char *attributes;
    void    *freeList[16];
    uint32_t freeListLen;
    bool        converted;
    int         fd;
    const char *array;
};

/** Skip space characters in the passed string.
 *
 * \returns Pointer to the first non-space character in \a s, which maybe a nul.
 */
static const char *skipSpace(const char *s)
{
	while (isspace(*s)) {
		s++;
	}
	return s;
}

static void convert(char *s)
{
	char *sIn = s;
	char *sOut = s;

	do {
		while (*sIn != '&' && *sIn != '\0') {
			*sOut = *sIn;
			sIn++;
			sOut++;
		}
		if (*sIn == '&') {
			static const struct {
				const char *token;
				const uint8_t len;
				const char *substitution;
			} replacements[] = {
				{
				"&quot;", 6, "\""}, {
				"&apos;", 6, "'"}, {
				"&amp;", 5, "&"}, {
				"&lt;", 4, "<"}, {
				"&gt;", 4, ">"}
			};

			uint8_t t;
			for (t = 0; t < M_ArrayLen(replacements); t++) {
				if (strncasecmp(sIn, replacements[t].token, replacements[t].len) == 0) {
					break;
				}
			}
			if (t < M_ArrayLen(replacements)) {
				sIn += replacements[t].len;
				strcpy(sOut, replacements[t].substitution);
				sOut += strlen(replacements[t].substitution);
			} else {
				*sOut = *sIn;
				sIn++;
				sOut++;
			}
		}
	}
	while (*sIn != '\0');

	*sOut = '\0';
}

/** Get another character from wherever we are reading.
 */
static int getnextchar(struct xmlnode *n, char *c)
{
	/* Reading from fd */
	if (n->fd != -1 && n->array == NULL)
		return read(n->fd, c, sizeof(char));

	/* Reading from array */
	if (n->fd == -1 && n->array != NULL) {
		*c = *(n->array);
		n->array++;
		return (*c == '\0') ? 0 : 1;
	}
	return 0;
}

/** Parse to find a tag enclosure. */
static bool Parse(struct xmlnode *n)
{
	char c;
	int r, l, m;
	int tagcount;

	/* Search for a < */
	do {
		r = getnextchar(n, &c);
	}
	while (r == 1 && c != '<');

	if (c != '<') {
		return false;
	}

	n->tag = g_malloc0(l = 64);
	m = 0;

	/* Read until a > */
	do {
		r = getnextchar(n, &n->tag[m++]);

		/* Skip space at the start of a tag eg. < tag> */
		if (m == 1 && isspace(n->tag[0]))
			m = 0;

		if (m == l)
			n->tag = g_realloc(n->tag, l += 256);

	}
	while (r == 1 && n->tag[m - 1] != '>');

	if (n->tag[m - 1] != '>') {
		dprintf("Failed to find closing '>'");
		g_free(n->tag);
		return false;
	}
	/* Null terminate the tag */
	n->tag[m - 1] = '\0';

	/* Check if it is a <?...> or <!...> tag */
	if (*n->tag == '?' || *n->tag == '!') {
		dprintf("Found '<? ...>' or '<! ...>' tag");
		dprintf(n->tag);
		n->content = NULL;
		return true;
	}
	/* Check if it is a < /> tag */
	if (m >= 2 && n->tag[m - 2] == '/') {
		n->tag[m - 2] = '\0';
		dprintf("Found '<.../>' tag");
		dprintf(n->tag);
		n->content = NULL;
		return true;
	}
	/* May need to truncate tag to remove attributes */
	r = 0;
	do {
		r++;
	}
	while (n->tag[r] != '\0' && !isspace(n->tag[r]));
	n->tag[r] = '\0';
	n->attributes = &n->tag[r + 1];

	dprintf("Found tag:");
	dprintf(n->tag);

	n->content = g_malloc0(l = 256);
	tagcount = 1;
	m = 0;

	/* Now read until the closing tag */
	do {
		r = getnextchar(n, &n->content[m++]);

		if (m > 1) {
			/* Is there a new tag opening? */
			if (n->content[m - 2] == '<') {
				/* Is the tag opening or closing */
				if (n->content[m - 1] == '/')
					tagcount--;
				else
					tagcount++;
			}
			/* Catch any <blah/> style tags */
			if (n->content[m - 2] == '/' && n->content[m - 1] == '>') {
				tagcount--;
			}
		}
		if (m == l)
			n->content = g_realloc(n->content, l += 256);

	}
	while (r == 1 && tagcount > 0);

	if (r != 1) {
		dprintf("Failed to find closing tag");
		dprintf(n->tag);
		g_free(n->tag);
		g_free(n->content);
		return false;
	}
	/* Null terminate the content */
	n->content[m - 2] = '\0';

	return true;
}

/** Return some attribute value for a node. */
const char *XmlGetAttribute(struct xmlnode *n, const char *attr)
{
	const char *s = n->attributes;
	const uint32_t attrLen = strlen(attr);

	while ((s = strstr(s, attr)) != NULL) {
		const char *preS = s - 1;
		const char *postS = s + attrLen;

		/* Check if the attribute was found in isolation
		 *  i.e. not part of another attribute name.
		 */
		if ((s == n->attributes || isspace(*preS)) && (*postS == '=' || isspace(*postS))) {
			postS = skipSpace(postS);

			if (*postS == '=') {
				postS++;
				postS = skipSpace(postS);
				if (*postS == '"') {
					char *c, *r = g_strdup(postS + 1);

					if ((c = strchr(r, '"')) != NULL)
						*c = '\0';

					n->freeList[n->freeListLen++] = r;
					return r;
				}
			}
		}
		/* Skip the match */
		s++;
	}
	return NULL;
}

/** Return the tag.
 */
const char *XmlGetTag(struct xmlnode *n)
{
	return n->tag;
}

/** Compare the tag name with some string.
 */
int XmlTagStrcmp(struct xmlnode *n, const char *str)
{
	return strcmp(n->tag, str);
}

/** Get the content.
 */
const char *XmlGetContent(struct xmlnode *n)
{
	if (!n->converted) {
		convert(n->content);
		n->converted = true;
	}
	return n->content;
}

/** Free a node and its storage.
 * \param[in,out] n  Pointer to node pointer to be freed.  If *n == NULL,
 *                    no action is taken.  *n is always set to NULL when
 *                    returning.
 */
void XmlDestroy(struct xmlnode **n)
{
	struct xmlnode *nn = *n;

	if (nn) {
		if (nn->tag) {
			g_free(nn->tag);
		}
		if (nn->content) {
			g_free(nn->content);
		}
		while (nn->freeListLen-- > 0) {
			g_free(nn->freeList[nn->freeListLen]);
		}
		g_free(nn);
		*n = NULL;
	}
}

/** Parse some XML from a file descriptor.
 * \param[in,out] n  Pointer to a node pointer. If *n != NULL, it will be freed.
 */
bool XmlParseFd(struct xmlnode **n, int fd)
{
	XmlDestroy(n);

	*n = g_malloc0(sizeof(struct xmlnode));
	(*n)->fd = fd;

	if (!Parse(*n)) {
		g_free(*n);
		*n = NULL;
		return false;
	}
	return true;
}

/** Parse from a string buffer.
 * Parse some XML in a string, returning the position in the string reached,
 * or NULL if the string was exhausted and no node was found.
 * \param[in,out] n  Pointer to a node pointer. If *n != NULL, it will be freed.
 */
const char *XmlParseStr(struct xmlnode **n, const char *s)
{
	XmlDestroy(n);

	if (s == NULL) {
		return NULL;
	}
	*n = g_malloc0(sizeof(struct xmlnode));
	(*n)->fd = -1;
	(*n)->array = s;
	if (!Parse(*n)) {
		g_free(*n);
		*n = NULL;
		return NULL;
	} else {
		return (*n)->array;
	}
}

/** Find a sub-node whose name matches the passed tag. */
struct xmlnode *XmlFindSubNode(struct xmlnode *n, const char *tag)
{
	if (n == NULL) {
		return NULL;
	}
	const char *s = XmlGetContent(n);
	struct xmlnode *r = NULL;
	do {
		XmlDestroy(&r);
		s = XmlParseStr(&r, s);
	}
	while (r && XmlTagStrcmp(r, tag) != 0);

	return r;
}

/** Same as XmlFindSubNode(), but frees the past node before returning.
 */
struct xmlnode *XmlFindSubNodeFree(struct xmlnode *n, const char *tag)
{
	struct xmlnode *r = XmlFindSubNode(n, tag);
	XmlDestroy(&n);
	return r;
}

struct cfetch {
    char    *data;
    size_t   size;
};

/** Callback for fetching URLs via CURL.  */
static size_t curlCallback(void *ptr, size_t size, size_t nmemb, void *param)
{
    struct cfetch *mbr = (struct cfetch *)param;
    size_t realsize = size * nmemb;
    mbr->data = g_realloc(mbr->data, mbr->size + realsize + 1);
    memcpy(&mbr->data[mbr->size], ptr, realsize);
    mbr->size += realsize;
    mbr->data[mbr->size] = '\0';
    return realsize;
}

#define FMAP(tdef,fname,fptr) \
	tdef fptr = (tdef)dlsym(h,fname);\
	if(h == NULL) {\
		printf("Library function not found: %s\n",fname);\
		goto cleanup;\
	}

static void* get_curl_lib()
{
	char *libs[] = { "libcurl.so.4", "libcurl.so", "libcurl-gnutls.so.4",
						"libcurl.so.3", NULL };
	static void *h = NULL;

	if(h) return h;
	char **p = libs;
	while(*p) {
		h = dlopen(*p, RTLD_LAZY);
		if(h) {
			// printf("Loaded %s\n", *p);
			return h;
		}
		p++;
	}
	printf("Failed to open Curl library.\n");
	return NULL;
}

/** Fetch some URL and return the contents in a memory buffer.
 * \param[in,out] size    Pointer to fill with the length of returned data.
 * \param[in]     urlFmt  The URL, or a printf-style format string for the URL.
 * \returns A pointer to the read data, or NULL if the fetch failed in any way.
*/
static void *CurlFetch(size_t *size, const char *urlFmt, ...)
{
	void *h = get_curl_lib();
	if(!h) return NULL;

	typedef void*		(*ce_init_t)(void);
	typedef int			(*ce_setopt_t)(void *, int, ...);
	typedef int			(*ce_perform_t)(void *);
	typedef const char* (*ce_strerror_t)(int);
	typedef void		(*ce_cleanup_t)(void *);

	FMAP(ce_init_t,"curl_easy_init",ce_init);
	FMAP(ce_setopt_t,"curl_easy_setopt",ce_setopt);
	FMAP(ce_perform_t,"curl_easy_perform",ce_perform);
	FMAP(ce_strerror_t,"curl_easy_strerror",ce_strerror);
	FMAP(ce_cleanup_t,"curl_easy_cleanup",ce_cleanup);

	void *ch = ce_init();
	if (ch == NULL) {
		fprintf(stderr,"Error: Failed to initialise libcurl\n");
		exit(EXIT_FAILURE);
	}
	/* Try to get the data */
	const int CO_URL = 10002;
	const int CO_WRITEFUNCTION = 20011;
	const int CO_WRITEDATA = 10001;
	const int CO_USERAGENT = 10018;
	const int CO_FAILONERROR = 45;
	//const int CO_VERBOSE = 41;
	struct cfetch cfdata;

	memset(&cfdata, 0, sizeof(cfdata));

	/* Formulate the URL */
	char buf[1024];
	va_list ap;
	va_start(ap, urlFmt);
	vsnprintf(buf, sizeof(buf), urlFmt, ap);
	va_end(ap);

	ce_setopt(ch, CO_URL, buf);
	ce_setopt(ch, CO_WRITEFUNCTION, curlCallback);
	ce_setopt(ch, CO_WRITEDATA, (void *) &cfdata);
	ce_setopt(ch, CO_USERAGENT, "irongrip/0.8");
	ce_setopt(ch, CO_FAILONERROR, 1);
	/*
	ce_setopt(ch, CO_VERBOSE, 1);
	// Set proxy information if necessary.
    (*curl_easy_setopt)(curl, CURLOPT_PROXY, proxy);
    (*curl_easy_setopt)(curl, CURLOPT_PROXYUSERPWD, proxy_user_pwd);
	*/

	void *res;
	int err_code = ce_perform(ch);
	if (err_code == 0) {
		res = cfdata.data;
		if (size != NULL) {
			*size = cfdata.size;
		}
	} else {
		printf("CURL err %d = %s\n", err_code, ce_strerror(err_code));
		res = NULL;
		if (size != NULL) {
			*size = 0;
		}
		g_free(cfdata.data);
	}
	ce_cleanup(ch);

cleanup:
	// dlclose(h); // Would blow up application
	return res;
}

/** Free memory associated with a mbartistcredit_t structure.  */
static void freeArtist(mbartistcredit_t * ad)
{
	g_free(ad->artistName);
	g_free(ad->artistNameSort);
	for (uint8_t t = 0; t < ad->artistIdCount; t++) {
		g_free(ad->artistId[t]);
	}
	g_free(ad->artistId);
}

static void freeMedium(mbmedium_t * md)
{
	g_free(md->title);
	for (uint16_t t = 0; t < md->trackCount; t++) {
		mbtrack_t *td = &md->track[t];
		g_free(td->trackName);
		freeArtist(&td->trackArtist);
	}
	g_free(md->track);
}

static void freeRelease(mbrelease_t * rel)
{
	g_free(rel->releaseGroupId);
	g_free(rel->releaseId);
	g_free(rel->asin);
	g_free(rel->albumTitle);
	g_free(rel->releaseType);
	freeArtist(&rel->albumArtist);
	freeMedium(&rel->medium);
}

static bool artistsAreIdentical(const mbartistcredit_t * ma, const mbartistcredit_t * mb)
{
	if (strcheck(ma->artistName, mb->artistName) != 0 ||
		strcheck(ma->artistNameSort, mb->artistNameSort) != 0) {
		return false;
	}
	return true;
}

static bool mediumsAreIdentical(const mbmedium_t * ma, const mbmedium_t * mb)
{
	if (ma->discNum != mb->discNum ||
		ma->trackCount != mb->trackCount || strcheck(ma->title, mb->title) != 0) {
		return false;
	}
	for (uint16_t t = 0; t < ma->trackCount; t++) {
		const mbtrack_t *mta = &ma->track[t], *mtb = &mb->track[t];
		if (strcheck(mta->trackName, mtb->trackName) != 0 ||
			!artistsAreIdentical(&mta->trackArtist, &mtb->trackArtist)) {
			return false;
		}
	}
	return true;
}

static bool releasesAreIdentical(const mbrelease_t * ra, const mbrelease_t * rb)
{
	if (ra->discTotal != rb->discTotal ||
		!artistsAreIdentical(&ra->albumArtist, &rb->albumArtist) ||
		strcheck(ra->releaseGroupId, rb->releaseGroupId) != 0 ||
		strcheck(ra->albumTitle, rb->albumTitle) != 0 ||
		strcheck(ra->releaseType, rb->releaseType) != 0 ||
		strcheck(ra->asin, rb->asin) != 0 || !mediumsAreIdentical(&ra->medium, &rb->medium)) {
		return false;
	}
	return true;
}

static uint16_t dedupeMediums(uint16_t mediumCount, mbmedium_t * mr)
{
	for (uint16_t t = 0; t < mediumCount; t++) {
		bool dupe = false;

		for (uint16_t u = t + 1; u < mediumCount && !dupe; u++) {
			if (mediumsAreIdentical(&mr[t], &mr[u])) {
				dupe = true;
			}
		}

		if (dupe) {
			mediumCount--;
			freeMedium(&mr[t]);
			mr[t] = mr[mediumCount];
			t--;
		}
	}

	return mediumCount;
}

static void dedupeReleases(mbresult_t * mr)
{
	for (uint8_t t = 0; t < mr->releaseCount; t++) {
		bool dupe = false;

		for (uint8_t u = t + 1; u < mr->releaseCount && !dupe; u++) {
			if (releasesAreIdentical(&mr->release[t], &mr->release[u])) {
				dupe = true;
			}
		}

		if (dupe) {
			mr->releaseCount--;
			freeRelease(&mr->release[t]);
			mr->release[t] = mr->release[mr->releaseCount];
			t--;
		}
	}
}

/** Process an %lt;artist-credit&gt; node. */
static void processArtistCredit(struct xmlnode *artistCreditNode, mbartistcredit_t * cd)
{

	const char *s = XmlGetContent(artistCreditNode);
	struct xmlnode *n = NULL;

	assert(strcmp(XmlGetTag(artistCreditNode), "artist-credit") == 0);

	/* Artist credit can have multiple entries... */
	while ((s = XmlParseStr(&n, s)) != NULL) {
		/* Process name-credits only at present */
		if (XmlTagStrcmp(n, "name-credit") == 0) {
			struct xmlnode *o, *artistNode = XmlFindSubNode(n, "artist");

			cd->artistIdCount++;
			cd->artistId = g_realloc(cd->artistId, sizeof(char *) * cd->artistIdCount);

			cd->artistId[cd->artistIdCount - 1] = g_strdup(XmlGetAttribute(artistNode, "id"));

			o = XmlFindSubNode(artistNode, "name");
			if (o) {
				const char *aa = XmlGetContent(o);

				if (cd->artistName == NULL) {
					cd->artistName = g_strdup(aa);
				} else {
					/* Concatenate multiple artists if needed */
					cd->artistName =
						g_realloc(cd->artistName, strlen(aa) + strlen(cd->artistName) + 3);

					strcat(cd->artistName, ", ");
					strcat(cd->artistName, aa);
				}
				XmlDestroy(&o);
			}
			o = XmlFindSubNode(artistNode, "sort-name");
			if (o) {
				cd->artistNameSort = g_strdup(XmlGetContent(o));
				XmlDestroy(&o);
			}
			XmlDestroy(&artistNode);
		}
	}
}

/** Process a &lt;track&gt; track node.  */
static void processTrackNode(struct xmlnode *trackNode, mbmedium_t * md)
{
	struct xmlnode *n = NULL;

	assert(strcmp(XmlGetTag(trackNode), "track") == 0);

	n = XmlFindSubNode(trackNode, "position");
	if (n) {
		uint16_t position = atoi(XmlGetContent(n));

		if (position >= 1 && position <= md->trackCount) {
			struct xmlnode *recording;
			mbtrack_t *td = &md->track[position - 1];

			recording = XmlFindSubNode(trackNode, "recording");
			if (recording) {
				struct xmlnode *m = NULL;
				const char *id;

				id = XmlGetAttribute(recording, "id");
				if (id) {
					td->trackId = g_strdup(id);
				}
				m = XmlFindSubNode(recording, "title");
				if (m) {
					td->trackName = g_strdup(XmlGetContent(m));
					XmlDestroy(&m);
				}
				m = XmlFindSubNode(recording, "artist-credit");
				if (m) {
					processArtistCredit(m, &td->trackArtist);
					XmlDestroy(&m);
				}
				XmlDestroy(&recording);
			}
		}
		XmlDestroy(&n);
	}
}

/** Process a &lt;medium&gt; node. */
static bool processMediumNode(struct xmlnode *mediumNode, const char *discId, mbmedium_t * md)
{
	bool mediumValid = false;
	struct xmlnode *n = NULL;

	assert(strcmp(XmlGetTag(mediumNode), "medium") == 0);

	memset(md, 0, sizeof(mbmedium_t));

	/* First find the disc-list to check if the discId is present */
	n = XmlFindSubNode(mediumNode, "disc-list");
	if (n) {
		struct xmlnode *disc = NULL;
		const char *s;

		s = XmlGetContent(n);
		while ((s = XmlParseStr(&disc, s)) != NULL) {
			if (XmlTagStrcmp(disc, "disc") == 0) {
				const char *id = XmlGetAttribute(disc, "id");

				if (id && strcmp(id, discId) == 0) {
					mediumValid = true;
				}
			}
		}
		XmlDestroy(&disc);
		XmlDestroy(&n);
	}

	/* Check if the medium should be processed */
	if (mediumValid) {
		memset(md, 0, sizeof(mbmedium_t));

		n = XmlFindSubNode(mediumNode, "position");
		if (n) {
			md->discNum = atoi(XmlGetContent(n));
			XmlDestroy(&n);
		}
		n = XmlFindSubNode(mediumNode, "title");
		if (n) {
			md->title = g_strdup(XmlGetContent(n));
			XmlDestroy(&n);
		}
		n = XmlFindSubNode(mediumNode, "track-list");
		if (n) {
			const char *count = XmlGetAttribute(n, "count");

			if (count) {
				struct xmlnode *track = NULL;
				const char *s;

				md->trackCount = atoi(count);
				md->track = g_malloc0(sizeof(mbtrack_t) * md->trackCount);

				s = XmlGetContent(n);
				while ((s = XmlParseStr(&track, s)) != NULL) {
					if (XmlTagStrcmp(track, "track") == 0) {
						processTrackNode(track, md);
					}
				}
				mediumValid = true;
				XmlDestroy(&track);
			}
			XmlDestroy(&n);
		}
	}
	return mediumValid;
}

static uint16_t processReleaseNode(struct xmlnode *releaseNode, const char *discId,
								   mbrelease_t * cd, mbmedium_t ** md)
{
	struct xmlnode *n;
	assert(strcmp(XmlGetTag(releaseNode), "release") == 0);

	memset(cd, 0, sizeof(mbrelease_t));

	cd->releaseId = g_strdup(XmlGetAttribute(releaseNode, "id"));

	n = XmlFindSubNode(releaseNode, "asin");
	if (n) {
		cd->asin = g_strdup(XmlGetContent(n));
		XmlDestroy(&n);
	}
	n = XmlFindSubNode(releaseNode, "title");
	if (n) {
		cd->albumTitle = g_strdup(XmlGetContent(n));
		XmlDestroy(&n);
	}
	n = XmlFindSubNode(releaseNode, "release-group");
	if (n) {
		cd->releaseType = g_strdup(XmlGetAttribute(n, "type"));
		cd->releaseGroupId = g_strdup(XmlGetAttribute(n, "id"));
		XmlDestroy(&n);
	}
	n = XmlFindSubNode(releaseNode, "artist-credit");
	if (n) {
		processArtistCredit(n, &cd->albumArtist);
		XmlDestroy(&n);
	}
	n = XmlFindSubNode(releaseNode, "medium-list");
	if (n) {
		const char *s, *count = XmlGetAttribute(n, "count");
		struct xmlnode *m = NULL;
		mbmedium_t *mediums = NULL;
		uint16_t mediumCount = 0;

		if (count) {
			cd->discTotal = atoi(count);
		}
		s = XmlGetContent(n);

		while ((s = XmlParseStr(&m, s)) != NULL) {
			if (XmlTagStrcmp(m, "medium") == 0) {
				mediums = g_realloc(mediums, sizeof(mbmedium_t) * (mediumCount + 1));
				if (processMediumNode(m, discId, &mediums[mediumCount])) {
					mediumCount++;
				}
			}
		}
		XmlDestroy(&n);
		XmlDestroy(&m);
		mediumCount = dedupeMediums(mediumCount, mediums);
		*md = mediums;
		return mediumCount;
	} else {
		*md = NULL;
		return 0;
	}
}

/** Process a release. */
static bool processRelease(const char *releaseId, const char *discId, mbresult_t * res)
{
	struct xmlnode *metaNode = NULL, *releaseNode = NULL;
	void *buf;
	const char *s;

	s = buf = CurlFetch(NULL,
		  "http://musicbrainz.org/ws/2/release/%s?inc=recordings+artists+release-groups+discids+artist-credits",
			  releaseId);
	if (buf == NULL) {
		return false;
	}
	/* Find the metadata node */
	do {
		s = XmlParseStr(&metaNode, s);
	}
	while (metaNode != NULL && XmlTagStrcmp(metaNode, "metadata") != 0);

	g_free(buf);

	if (metaNode == NULL) {
		return false;
	}
	releaseNode = XmlFindSubNodeFree(metaNode, "release");
	if (releaseNode) {
		mbmedium_t *medium = NULL;
		uint16_t mediumCount;
		mbrelease_t release;
		mediumCount = processReleaseNode(releaseNode, discId, &release, &medium);

		for (uint16_t m = 0; m < mediumCount; m++) {
			mbrelease_t *newRel;
			res->releaseCount++;
			res->release = g_realloc(res->release, sizeof(mbrelease_t) * res->releaseCount);
			newRel = &res->release[res->releaseCount - 1];
			memcpy(newRel, &release, sizeof(mbrelease_t));
			memcpy(&newRel->medium, &medium[m], sizeof(mbmedium_t));
		}
		XmlDestroy(&releaseNode);
		return true;
	}
	return false;
}

/** Lookup some CD.
 * \param[in] discId  The ID of the CD to lookup.
 * \param[in] res     Pointer to populate with the results.
 * \retval true   If the CD found and the results returned.
 * \retval false  If the CD is unknown to MusicBrainz.
 */
static bool musicbrainz_lookup(const char *discId, mbresult_t * res)
{
	memset(res, 0, sizeof(mbresult_t));
	void *buf;
	const char *s;
	s = buf = CurlFetch(NULL, "http://musicbrainz.org/ws/2/discid/%s", discId);
	if (!buf) {
		return false;
	}
	struct xmlnode *metaNode = NULL;
	/* Find the metadata node */
	do {
		s = XmlParseStr(&metaNode, s);
	}
	while (metaNode != NULL && XmlTagStrcmp(metaNode, "metadata") != 0);
	g_free(buf);
	if (metaNode == NULL) {
		return false;
	}
	struct xmlnode *releaseListNode = NULL;
	releaseListNode = XmlFindSubNodeFree(XmlFindSubNodeFree(metaNode, "disc"), "release-list");
	if (releaseListNode) {
		struct xmlnode *releaseNode = NULL;
		const char *s = XmlGetContent(releaseListNode);
		while ((s = XmlParseStr(&releaseNode, s)) != NULL) {
			const char *releaseId;
			if ((releaseId = XmlGetAttribute(releaseNode, "id")) != NULL) {
				processRelease(releaseId, discId, res);
			}
		}
		XmlDestroy(&releaseNode);
		dedupeReleases(res);
	}
	XmlDestroy(&releaseListNode);
	return true;
}

static void mb_free(mbresult_t * res)
{
	if(res==NULL) return;
	for (uint16_t r = 0; r < res->releaseCount; r++) {
		freeRelease(&res->release[r]);
	}
	g_free(res->release);
}

static void maystore(GList **d, char *a) {
	const char *PATTERNS[] =  { "ecx.images-amazon.com",
								"www.allmusic.com/album/",
								"www.discogs.com/master/",
								"www.discogs.com/release/",
								"www.youtube.com/watch?v=",
								NULL };
	for (GList *c = g_list_first(*d); c != NULL; c = g_list_next(c)) {
		char *s = (char *) c->data;
		if(strcmp(a,s)==0) return;
	}
	for(const char **y = PATTERNS;*y;y++) {
		if(!strstr(a, *y)) continue;
		*d = g_list_append(*d, g_strdup(a));
		return;
	}
}

#define HTML_HEADER \
"<!DOCTYPE html PUBLIC \"-//W3C//DTD XHTML 1.0 Transitional//EN\"\n" \
"\t\"http://www.w3.org/TR/xhtml1/DTD/xhtml1-transitional.dtd\">\n" \
"<html xmlns=\"http://www.w3.org/1999/xhtml\">\n" \
"<head>\n<title>Data from MusicBrainz</title>\n</head>\n<body>\n"

static void musicbrainz_scan(const mbresult_t * res, char *path)
{
	const char *FILTERS[] = { "http://", "coverartarchive.org/release/", NULL };
	const char *ANTIPATTERNS[] =  { "musicbrainz.org", "www.w3.org/", "purl.org/", "metabrainz.org/", NULL };

	gchar *dir = g_strdup_printf("%s/MusicBrainz-DiscId-[%s]", path, g_data->disc_id);
	if(!mkdir_p(dir)) return;

	//printf("Disc id WS2 = http://musicbrainz.org/ws/2/discid/%s\n", g_data->disc_id);

	for (uint16_t r = 0; r < res->releaseCount; r++) {
		mbrelease_t *rel = &res->release[r];

		fetch_image(rel, dir);

		char *html = g_strdup_printf("%s/%s_MusicBrainz.html", dir, rel->releaseId);
		FILE *fh = fopen(html, "wb");
		g_free(html);
		fprintf(fh, HTML_HEADER);
		gchar *url = g_strdup_printf("http://musicbrainz.org/release/%s", rel->releaseId);
		fprintf(fh, "<p>Release page : <a href=\"%s\">%s</a></p>\n", url, url);
		//printf("Release WS2 = http://musicbrainz.org/ws/2/release/%s\n", rel->releaseId);
		g_free(url);
		size_t sz = 0;
		const char *fmt = "http://musicbrainz.org/release/%s";
		char *c = CurlFetch(&sz, fmt, rel->releaseId);
		if(!c) goto next;
		if(sz<256) goto next;
		char *b = calloc(1, sz); // working copy
		GList *d = NULL;
		for(const char **f = FILTERS;*f;f++) {
			memcpy(b,c,sz);
			for(char *p=b;*p;p++) {
				char *a = strstr(p, *f);
				if(a == NULL) {
					// fprintf(stderr, "BREAKING with %lu bytes remaining out of %lu\n", strlen(p), sz);
					break;
				}
				for(char *e = a+strlen(*f);*e;e++) {
					if(*e != '<' && *e != '"' && !isspace(*e)) continue;
					*e = 0; p = e;
					int found = 0;
					for(const char **x = ANTIPATTERNS;*x;x++) {
						if(strstr(a, *x)) { found=1; break; }
					}
					if(!found) { maystore(&d,a); }
					break;
				}
			}
		}
		// printf("G_LIST_LENGTH = %d\n", g_list_length(d));
		for (GList *c = g_list_first(d); c != NULL; c = g_list_next(c)) {
			char *s = (char *) c->data;
			fprintf(fh,"<pre>%s</pre>\n", s);
		}
		g_free(b);
		g_free(c);
		if(d) g_list_free(d);
next:
		fprintf(fh, "<pre>");
		musicbrainz_print(fh,rel);
		fprintf(fh, "</pre>");
		fprintf(fh, "</body>\n</html>\n");
		fclose(fh);
	}
}

static void musicbrainz_print(FILE *fd, const mbrelease_t *rel)
{
	fprintf(fd,"Release=%s\n", rel->releaseId);
	fprintf(fd,"  ASIN=%s\n", rel->asin);
	fprintf(fd,"  Album=%s\n", rel->albumTitle);
	fprintf(fd,"  AlbumArtist=%s\n", rel->albumArtist.artistName);
	fprintf(fd,"  AlbumArtistSort=%s\n", rel->albumArtist.artistNameSort);
	fprintf(fd,"  ReleaseType=%s\n", rel->releaseType);
	fprintf(fd,"  ReleaseGroupId=%s\n", rel->releaseGroupId);
	for (uint8_t t = 0; t < rel->albumArtist.artistIdCount; t++) {
		fprintf(fd,"  ArtistId=%s\n", rel->albumArtist.artistId[t]);
	}
	fprintf(fd,"  Total Disc=%u\n", rel->discTotal);
	const mbmedium_t *mb = &rel->medium;
	fprintf(fd,"  Medium\n");
	fprintf(fd,"    DiscNum=%u\n", mb->discNum);
	fprintf(fd,"    Title=%s\n", mb->title);
	for (uint16_t u = 0; u < mb->trackCount; u++) {
		const mbtrack_t *td = &mb->track[u];
		fprintf(fd,"    Track %u\n", u);
		fprintf(fd,"      Id=%s\n", td->trackId);
		fprintf(fd,"      Title=%s\n", td->trackName);
		fprintf(fd,"      Artist=%s\n", td->trackArtist.artistName);
		fprintf(fd,"      ArtistSort=%s\n", td->trackArtist.artistNameSort);
		for (uint8_t t = 0; t < td->trackArtist.artistIdCount; t++) {
			fprintf(fd,"      ArtistId=%s\n", td->trackArtist.artistId[t]);
		}
	}
}

#define M_ArraySize(a)  (sizeof(a) / sizeof(a[0]))

// TODO: handle one pixel GIF from Amazon
/*
0000000: 4749 4638 3961 0100 0100 8001 0000 0000  GIF89a..........
0000010: ffff ff21 f904 0100 0001 002c 0000 0000  ...!.......,....
0000020: 0100 0100 0002 024c 0100 3b              .......L..;

unsigned char AmazonOnePixelGIF[] = {
  0x47, 0x49, 0x46, 0x38, 0x39, 0x61, 0x01, 0x00, 0x01, 0x00, 0x80, 0x01,
  0x00, 0x00, 0x00, 0x00, 0xff, 0xff, 0xff, 0x21, 0xf9, 0x04, 0x01, 0x00,
  0x00, 0x01, 0x00, 0x2c, 0x00, 0x00, 0x00, 0x00, 0x01, 0x00, 0x01, 0x00,
  0x00, 0x02, 0x02, 0x4c, 0x01, 0x00, 0x3b
};
unsigned int AmazonOnePixelGIF_len = 43;
*/
static void fetch_image(const mbrelease_t *rel, char *path) {
	if(!rel->asin) return;
	const char *artUrl[] = {
		"http://images.amazon.com/images/P/%s.02._SCLZZZZZZZ_.jpg", /* UK */
		"http://images.amazon.com/images/P/%s.01._SCLZZZZZZZ_.jpg", /* US */
		"http://images.amazon.com/images/P/%s.03._SCLZZZZZZZ_.jpg", /* DE */
		"http://images.amazon.com/images/P/%s.08._SCLZZZZZZZ_.jpg", /* FR */
		"http://images.amazon.com/images/P/%s.09._SCLZZZZZZZ_.jpg"  /* JP */
	};
	const char *artCountry[] = { "UK", "US", "DE", "FR", "JP" };
	uint8_t attempt = 0;
	do {
		size_t sz = 0;
		const char *s = CurlFetch(&sz, artUrl[attempt], rel->asin);
		if(!s) continue;
		if(sz<256) continue;
		// printf(artUrl[attempt], rel->asin); printf("\n");
		char *file = g_strdup_printf("%s/%s_Amazon_%s_%s.jpg", path,
							rel->releaseId, rel->asin, artCountry[attempt]);
		FILE *fd = fopen(file, "wb");
		g_free(file);
		if(fd) {
			fwrite(s,1,sz,fd);
			fclose(fd);
			break;
		}
	} while(++attempt < M_ArraySize(artUrl));
}

int main(int argc, char *argv[])
{
	g_thread_init(NULL);
	gdk_threads_init();
	g_data = g_malloc0(sizeof(shared));
	g_data->updated = g_cond_new();
	g_data->monolock = g_mutex_new();
	g_data->guilock = g_mutex_new();
	load_prefs();
	log_init();
#ifdef ENABLE_NLS
	bindtextdomain(PACKAGE, PACKAGE_LOCALE_DIR);
	bind_textdomain_codeset(PACKAGE, UTF8);
	textdomain(PACKAGE);
#endif
	// cddb_log_set_level(CDDB_LOG_DEBUG);
	gtk_init(&argc, &argv);
	win_main = create_main();
	win_prefs = create_prefs();
	gtk_widget_show(win_main);
	lookup_cdparanoia();
	// recurring timeout to automatically re-scan cdrom once in a while
	gdk_threads_add_timeout(5000, idle, (void *)1);
	// add an idle event to scan the cdrom drive ASAP
	gdk_threads_add_idle(scan_on_startup, NULL);
	gdk_threads_add_timeout(400, cb_gui_update, NULL);
	gdk_threads_add_timeout(7000, gfileinfo_cb, 0);
	gtk_main();
	free_prefs(g_prefs);
	log_end();
	g_cond_free(g_data->updated);
	g_mutex_free(g_data->monolock);
	g_mutex_free(g_data->guilock);
	g_free(g_data);
	libcddb_shutdown();
	return 0;
}

