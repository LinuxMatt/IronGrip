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

// LNR - It's possible that some files may end up on a MS file system,
// so it's best to disallow MS invalid chars as well. I also disallow
// period (dot) because it screws up my file name database software. YMMV
#define	BADCHARS	"./?*|><:\"\\"

#define UTF8 "UTF-8"
#define STR_UNKNOWN "unknown"
#define STR_TEXT "text"
#define PIXMAP_PATH  PACKAGE_DATA_DIR "/" PIXMAPDIR
#define DEFAULT_LOG_FILE "/tmp/irongrip.log"
#define LAME_PRG "lame"
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
#define WDG_RIPPING "win_ripping"
#define WDG_SCROLL "scroll"
#define WDG_ABOUT "about"
#define WDG_DISC "disc"
#define WDG_CDDB "lookup"
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
    int mp3_vbr;
    int mp3_quality;
    int main_window_width;
    int main_window_height;
    int eject_on_done;
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
	GCond *available;
	GList *disc_matches;
	GList *drive_list;
	gchar device[128]; // current cdrom device
	GMutex *barrier;
	GThread *cddb_thread;
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
	double rip_percent;
	gboolean track_format[100];
	int cddb_matches;
	int cddb_thread_running;
	int counter;
	int encode_tracks_completed;
	int numCdparanoiaFailed;
	int numCdparanoiaOk;
	int numLameFailed;
	int numLameOk;
	int rip_tracks_completed;
	int tracks_to_rip;
	pid_t cdparanoia_pid;
	pid_t lame_pid;
} shared;

typedef struct _cdrom {
	gchar *model;
	gchar *device;
	int fd;
} s_drive;

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
static void disable_mp3_widgets(void);
static void dorip();
static void enable_all_main_widgets(void);
static void enable_mp3_widgets(void);
static void get_prefs_from_widgets(prefs *p);
static void on_cancel_clicked(GtkButton *button, gpointer user_data);
static void save_prefs(prefs *p);
static void set_status(char *text);
static void fmt_status(const char *fmt, ...);
static void set_widgets_from_prefs(prefs *p);
static void sigchld();

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

	for (i = 0; i < strlen(format); i++) {
		if ((format[i] == '%') && (i + 1 < strlen(format))) {
			switch (format[i + 1]) {
			case 'A':
				if (artist) len += strlen(artist);
				break;
			case 'L':
				if (album) len += strlen(album);
				break;
			case 'N':
				if ((tracknum > 0) && (tracknum < 100)) len += 2;
				break;
			case 'Y':
				if (year) len += strlen(year);
				break;
			case 'T':
				if (title) len += strlen(title);
				break;
			case 'G':
				if (genre) len += strlen(genre);
				break;
			case '%':
				len += 1;
				break;
			}
			i++;	// skip the character after the %
		} else {
			len++;
		}
	}
	char *ret = g_malloc0(sizeof(char) * (len + 1));
	int pos = 0;
	for (i = 0; i < strlen(format); i++) {
		if ((format[i] == '%') && (i + 1 < strlen(format))) {
			switch (format[i + 1]) {
			case 'A':
				if (artist) {
					strncpy(&ret[pos], artist, strlen(artist));
					pos += strlen(artist);
				}
				break;
			case 'L':
				if (album) {
					strncpy(&ret[pos], album, strlen(album));
					pos += strlen(album);
				}
				break;
			case 'N':
				if ((tracknum > 0) && (tracknum < 100)) {
					ret[pos] = '0' + ((int)tracknum / 10);
					ret[pos + 1] = '0' + (tracknum % 10);
					pos += 2;
				}
				break;
			case 'Y':
				if (year) {
					strncpy(&ret[pos], year, strlen(year));
					pos += strlen(year);
				}
				break;
			case 'T':
				if (title) {
					strncpy(&ret[pos], title, strlen(title));
					pos += strlen(title);
				}
				break;
			case 'G':
				if (genre) {
					strncpy(&ret[pos], genre, strlen(genre));
					pos += strlen(genre);
				}
				break;
			case '%':
				ret[pos] = '%';
				pos += 1;
			}
			i++;	// skip the character after the %
		} else {
			ret[pos] = format[i];
			pos++;
		}
	}
	ret[pos] = '\0';
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
		set_status("Please insert an audio disc in the cdrom drive...");
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
	g_atomic_int_set(&g_data->cddb_thread_running, 0);
	return NULL;
}

static GList *lookup_disc(cddb_disc_t *disc)
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
	// query cddb to find similar discs
	g_atomic_int_set(&g_data->cddb_thread_running, 1);
	g_data->cddb_disc = disc;
	g_data->cddb_thread = g_thread_create(cddb_thread_run, NULL, TRUE, NULL);

	// show cddb update window
	gdk_threads_enter();
	disable_all_main_widgets();

	GtkLabel *status = GTK_LABEL(LKP_MAIN(WDG_STATUS));
	gtk_label_set_text(status, _("<b>Getting disc info from the internet...</b>"));
	gtk_label_set_use_markup(status, TRUE);

	while (g_atomic_int_get(&g_data->cddb_thread_running) != 0) {
		while (gtk_events_pending())
			gtk_main_iteration();
		Sleep(300);
	}
	gtk_label_set_text(status, "CDDB query finished.");
	enable_all_main_widgets();
	gdk_threads_leave();

	// make a list of all the matches
	GList *matches = NULL;
	for (int i = 0; i < g_data->cddb_matches; i++) {
		cddb_disc_t *possible_match = cddb_disc_clone(disc);
		if (!cddb_read(g_data->cddb_conn, possible_match)) {
			cddb_error_print(cddb_errno(g_data->cddb_conn));
			fatalError("cddb_read() failed.");
		}
		// cddb_disc_print(possible_match);
		matches = g_list_append(matches, possible_match);

		// move to next match
		if (i < g_data->cddb_matches - 1) {
			if (!cddb_query_next(g_data->cddb_conn, disc))
				fatalError("Query index out of bounds.");
		}
	}
	cddb_destroy(g_data->cddb_conn);
	return matches;
}

static cddb_disc_t *read_disc(char *cdrom)
{
	int fd = open(cdrom, O_RDONLY | O_NONBLOCK);
	if (fd < 0) {
		TRACEWARN("Couldn't open '%s'", cdrom);
		return NULL;
	}
	cddb_disc_t *disc = NULL;

	// read disc status info
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

	g_data->disc_matches = lookup_disc(disc);
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

static void on_pick_disc_changed(GtkComboBox *combobox, gpointer user_data)
{
	cddb_disc_t *disc = g_list_nth_data(g_data->disc_matches,
										gtk_combo_box_get_active(combobox));
	update_tracklist(disc);
}

static void on_preferences_clicked(GtkToolButton *button, gpointer user_data)
{
	gtk_widget_show(win_prefs);
}

static void on_prefs_response(GtkDialog *dialog, gint response, gpointer data)
{
	if (response == GTK_RESPONSE_OK) {
		if (!prefs_are_valid()) return;
		get_prefs_from_widgets(g_prefs);
		save_prefs(g_prefs);
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
	gdk_threads_leave();
	refresh(1);
	gdk_threads_enter();
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
	dorip();
}

static void on_rip_mp3_toggled(GtkToggleButton *button, gpointer user_data)
{
	if (gtk_toggle_button_get_active(button) && !program_exists(LAME_PRG)) {
		DIALOG_ERROR_OK(_("The '%s' program was not found in your 'PATH'."
			" %s requires that program in order to create MP3 files. All MP3"
			" functionality is disabled."
			" You should install '%s' on this computer if you want to convert"
			" audio tracks to MP3."), LAME_PRG, PROGRAM_NAME, LAME_PRG);

		g_prefs->rip_mp3 = 0;
		gtk_toggle_button_set_active(button, g_prefs->rip_mp3);
	}
	if (!gtk_toggle_button_get_active(button))
		disable_mp3_widgets();
	else
		enable_mp3_widgets();
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

static void read_completion_file(GtkListStore *list, const char *name)
{
	gchar *file = get_config_path(name);
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

	gchar *file = get_config_path(name);
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
							" as WAV and/or MP3.");

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
	GtkTooltips *tooltips = gtk_tooltips_new();
	gtk_tooltips_set_tip(tooltips, lookup,
		_("Look up into the CDDB for information about this audio disc"), NULL);
	gtk_widget_show(lookup);
	gtk_container_add(GTK_CONTAINER(toolbar), lookup);
	gtk_tool_item_set_is_important(GTK_TOOL_ITEM(lookup), TRUE);

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

	GtkWidget *music_dir = gtk_file_chooser_button_new(_("Destination folder"),
										GTK_FILE_CHOOSER_ACTION_SELECT_FOLDER);
	gtk_widget_show(music_dir);
	BOXPACK(vbox, music_dir, FALSE, FALSE, 0);

	GtkWidget *make_m3u = gtk_check_button_new_with_mnemonic(
													_("Create M3U playlist"));
	gtk_widget_show(make_m3u);
	BOXPACK(vbox, make_m3u, FALSE, FALSE, 0);

	/* CDROM drives */
	GtkWidget *hbox = gtk_hbox_new(FALSE, 0);
	gtk_widget_show(hbox);
	BOXPACK(vbox, hbox, FALSE, FALSE, 0);
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
	BOXPACK(vbox, hbox, FALSE, FALSE, 0);
	label = gtk_label_new(_("Path to device: "));
	gtk_widget_show(label);
	BOXPACK(hbox, label, FALSE, FALSE, 0);

	GtkWidget *cdrom = gtk_entry_new();
	gtk_widget_show(cdrom);
	gtk_widget_set_sensitive(cdrom, FALSE);
	BOXPACK(hbox, cdrom, TRUE, TRUE, 0);
	GtkTooltips *tip = gtk_tooltips_new();
	gtk_tooltips_set_tip(tip, cdrom, _("Default: /dev/cdrom\n"
			"Other example: /dev/hdc\n" "Other example: /dev/sr0"), NULL);
	CONNECT_SIGNAL(cdrom, "focus_out_event", on_cdrom_focus_out);

	CONNECT_SIGNAL(cdrom_drives, "changed", on_s_drive_changed);
	/* END of CDROM drives */

	GtkWidget *eject_on_done = gtk_check_button_new_with_mnemonic(
											_("Eject disc when finished"));
	gtk_widget_show(eject_on_done);
	BOXPACK(vbox, eject_on_done, FALSE, FALSE, 5);

	label = gtk_label_new(_("General"));
	gtk_widget_show(label);
	gtk_notebook_set_tab_label(tabs, gtk_notebook_get_nth_page(tabs, 0), label);
	/* END GENERAL tab */

	/* FILENAMES tab */
	vbox = gtk_vbox_new(FALSE, 5);
	gtk_container_set_border_width(GTK_CONTAINER(vbox), 5);
	gtk_widget_show(vbox);
	gtk_container_add(GTK_CONTAINER(notebook1), vbox);

	GtkWidget *frame = gtk_frame_new(NULL);
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

	label = gtk_label_new(_("Filename formats"));
	gtk_widget_show(label);
	gtk_frame_set_label_widget(GTK_FRAME(frame), label);
	gtk_label_set_use_markup(GTK_LABEL(label), TRUE);

	label = gtk_label_new(_("Filenames"));
	gtk_widget_show(label);
	gtk_notebook_set_tab_label(tabs, gtk_notebook_get_nth_page(tabs, 1), label);
	/* END FILENAMES tab */

	/* ENCODE tab */
	vbox = gtk_vbox_new(FALSE, 0);
	gtk_container_set_border_width(GTK_CONTAINER(vbox), 5);
	gtk_widget_show(vbox);
	gtk_container_add(GTK_CONTAINER(notebook1), vbox);

	/* WAV */
	frame = gtk_frame_new(NULL);
	gtk_widget_show(frame);
	BOXPACK(vbox, frame, FALSE, FALSE, 0);
	GtkWidget *alignment = gtk_alignment_new(0.5, 0.5, 1, 1);
	gtk_widget_show(alignment);
	gtk_container_add(GTK_CONTAINER(frame), alignment);
	gtk_alignment_set_padding(GTK_ALIGNMENT(alignment), 1, 1, 8, 8);
	GtkWidget *wbox = gtk_vbox_new(FALSE, 0);
	gtk_widget_show(wbox);
	gtk_container_add(GTK_CONTAINER(alignment), wbox);

	GtkWidget *rip_wav = gtk_check_button_new_with_mnemonic(
													_("WAV (uncompressed)"));
	gtk_widget_show(rip_wav);
	gtk_frame_set_label_widget(GTK_FRAME(frame), rip_wav);

	label = gtk_label_new(
			_("WAV files retain maximum sound quality, but they are very big."
			" You should keep WAV files if you intend to create Audio discs."
			" WAV files can be converted back to audio tracks with CD burning"
			" software."));

	gtk_label_set_line_wrap(GTK_LABEL(label), TRUE);
	gtk_widget_show(label);
	BOXPACK(wbox, label, FALSE, FALSE, 0);
	/* END WAV */

	/* MP3 */
	frame = gtk_frame_new(NULL);
	gtk_widget_show(frame);
	BOXPACK(vbox, frame, FALSE, FALSE, 0);

	alignment = gtk_alignment_new(0.5, 0.5, 1, 1);
	gtk_widget_show(alignment);
	gtk_container_add(GTK_CONTAINER(frame), alignment);
	gtk_alignment_set_padding(GTK_ALIGNMENT(alignment), 1, 1, 8, 8);

	vbox = gtk_vbox_new(FALSE, 6);
	gtk_widget_show(vbox);
	gtk_container_add(GTK_CONTAINER(alignment), vbox);

	GtkWidget *mp3_vbr = gtk_check_button_new_with_mnemonic(
											_("Variable bit rate (VBR)"));
	gtk_widget_show(mp3_vbr);
	BOXPACK(vbox, mp3_vbr, FALSE, FALSE, 0);
	CONNECT_SIGNAL(mp3_vbr, "toggled", on_vbr_toggled);

	tip = gtk_tooltips_new();
	gtk_tooltips_set_tip(tip, mp3_vbr,
							_("Better quality for the same size."), NULL);

	GtkWidget *hboxcombo = gtk_hbox_new(FALSE, 0);
	gtk_widget_show(hboxcombo);
	BOXPACK(vbox, hboxcombo, FALSE, FALSE, 1);

	label = gtk_label_new(_("Quality : "));
	gtk_widget_show(label);
	BOXPACK(hboxcombo, label, FALSE, FALSE, 0);

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
	BOXPACK(vbox, hbox, FALSE, FALSE, 0);

	label = gtk_label_new(_("Bitrate : "));
	gtk_widget_show(label);
	BOXPACK(hbox, label, FALSE, FALSE, 0);

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

	label = gtk_label_new(_("Encode"));
	gtk_widget_show(label);
	gtk_notebook_set_tab_label(tabs, gtk_notebook_get_nth_page(tabs, 2), label);
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
	gtk_notebook_set_tab_label(tabs, gtk_notebook_get_nth_page(tabs, 3), label);
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
	HOOKUP(prefs, music_dir, "music_dir");
	HOOKUP(prefs, make_m3u, "make_playlist");
	HOOKUP(prefs, cdrom, "cdrom");
	HOOKUP(prefs, cdrom_drives, WDG_CDROM_DRIVES);
	HOOKUP(prefs, eject_on_done, "eject_on_done");
	HOOKUP(prefs, fmt_music, WDG_FMT_MUSIC);
	HOOKUP(prefs, fmt_albumdir, WDG_FMT_ALBUMDIR);
	HOOKUP(prefs, fmt_playlist, WDG_FMT_PLAYLIST);
	HOOKUP(prefs, rip_wav, "rip_wav");
	HOOKUP(prefs, mp3_vbr, WDG_MP3VBR);
	HOOKUP(prefs, mp3_quality, WDG_MP3_QUALITY);
	HOOKUP(prefs, rip_mp3, "rip_mp3");
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

static void enable_widget(char *name) {
	gtk_widget_set_sensitive(LKP_MAIN(name), TRUE);
}

static void disable_widget(char *name) {
	gtk_widget_set_sensitive(LKP_MAIN(name), FALSE);
}

static void disable_all_main_widgets(void)
{
	gtk_widget_set_sensitive(LKP_MAIN(WDG_TRACKLIST), FALSE);
	disable_widget(WDG_DISC);
	disable_widget(WDG_LBL_GENRE);
	disable_widget(WDG_LBL_ALBUMTITLE);
	disable_widget(WDG_LBL_ARTIST);
	disable_widget(WDG_CDDB);
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

static void enable_all_main_widgets(void)
{
	gtk_widget_set_sensitive(LKP_MAIN(WDG_TRACKLIST), TRUE);
	enable_widget(WDG_DISC);
	enable_widget(WDG_LBL_GENRE);
	enable_widget(WDG_LBL_ALBUMTITLE);
	enable_widget(WDG_LBL_ARTIST);
	enable_widget(WDG_CDDB);
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
}

static void enable_mp3_widgets(void)
{
	gtk_widget_set_sensitive(LKP_PREF(WDG_MP3VBR), TRUE);
	gtk_widget_set_sensitive(LKP_PREF(WDG_MP3_QUALITY), TRUE);
	gtk_widget_set_sensitive(LKP_PREF(WDG_BITRATE), TRUE);
}

static void show_completed_dialog(int numOk, int numFailed)
{
#define CREATOK " created successfully"
#define CREATKO "There was an error creating "
    if(numFailed > 0) {
		DIALOG_WARNING_CLOSE(
				ngettext(CREATKO "%d file", CREATKO "%d files", numFailed),
				numFailed);
	} else {
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
	p->eject_on_done = 0;
	p->main_window_height = 380;
	p->main_window_width = 520;
	p->make_playlist = 1;
	p->mp3_quality = 2; // 0:low, 1:good, 2:high, 3:max
	p->mp3_vbr = 1;
	p->rip_mp3 = 1;
	p->rip_wav = 0;
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
	gtk_file_chooser_set_current_folder(
			GTK_FILE_CHOOSER(LKP_PREF("music_dir")), p->music_dir);

	gtk_combo_box_set_active(COMBO_MP3Q, p->mp3_quality);

	set_pref_toggle("do_cddb_updates", p->do_cddb_updates);
	set_pref_toggle("do_log", p->do_log);
	set_pref_toggle("eject_on_done", p->eject_on_done);
	set_pref_toggle("make_playlist", p->make_playlist);
	set_pref_toggle(WDG_MP3VBR, p->mp3_vbr);
	set_pref_toggle("rip_mp3", p->rip_mp3);
	set_pref_toggle("rip_wav", p->rip_wav);
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
	gtk_combo_box_set_model(COMBO_DRIVE, GTK_TREE_MODEL(store));
	g_list_free_full(cmb, free_drive);
	// disable mp3 widgets if needed
	if(!(p->rip_mp3)) disable_mp3_widgets();
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
	gchar *tocopy = gtk_file_chooser_get_filename(
						GTK_FILE_CHOOSER(LKP_PREF("music_dir")));
	szcopy(p->music_dir, tocopy);
	g_free(tocopy);
	p->mp3_quality = gtk_combo_box_get_active(COMBO_MP3Q);

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
	szcopy(p->format_music, GET_PREF_TEXT(WDG_FMT_MUSIC));
	szcopy(p->format_playlist, GET_PREF_TEXT(WDG_FMT_PLAYLIST));
	szcopy(p->format_albumdir, GET_PREF_TEXT(WDG_FMT_ALBUMDIR));
	szcopy(p->cddb_server_name, GET_PREF_TEXT("cddb_server_name"));
	szcopy(p->server_name, GET_PREF_TEXT("server_name"));
	p->make_playlist = get_pref_toggle("make_playlist");
	p->rip_wav = get_pref_toggle("rip_wav");
	p->rip_mp3 = get_pref_toggle("rip_mp3");
	p->mp3_vbr = get_pref_toggle(WDG_MP3VBR);
	p->eject_on_done = get_pref_toggle("eject_on_done");
	p->do_cddb_updates = get_pref_toggle("do_cddb_updates");
	p->use_proxy = get_pref_toggle("use_proxy");
	p->cddb_nocache = get_pref_toggle("cddb_nocache");
	p->do_log = get_pref_toggle("do_log");
	p->port_number = atoi(GET_PREF_TEXT("port_number"));
	p->cddb_port= atoi(GET_PREF_TEXT("cddb_port"));
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

static gchar* get_config_path(const gchar *file_suffix)
{
	gchar *filename = NULL;
	const gchar *home = g_getenv("HOME");
	gchar *path = g_strdup_printf("%s/.config/%s", home, PACKAGE);
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
	fprintf(fd, "RIP_MP3=%d\n", p->rip_mp3);
	fprintf(fd, "MP3_VBR=%d\n", p->mp3_vbr);
	fprintf(fd, "MP3_QUALITY=%d\n", p->mp3_quality);
	fprintf(fd, "WINDOW_WIDTH=%d\n", p->main_window_width);
	fprintf(fd, "WINDOW_HEIGHT=%d\n", p->main_window_height);
	fprintf(fd, "EJECT=%d\n", p->eject_on_done);
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
	get_field_as_long(s,file,"RIP_MP3", &(p->rip_mp3));
	get_field_as_long(s,file,"MP3_VBR", &(p->mp3_vbr));

	int i = 0;
	if(get_field_as_long(s,file,"MP3_QUALITY", &i)) {;
		if(i > -1 && i < 4) p->mp3_quality = i;
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
	struct stat s;
	if (stat(p->music_dir, &s) == 0) {
		return p->music_dir;
	}
	gchar *newdir = get_default_music_dir();
	if(strcmp(newdir,p->music_dir)) {
		DIALOG_ERROR_OK("The music directory '%s' does not exist.\n\n"
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
	// dont'ask yet
	return true;
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
    // wait until all the worker threads are done
    while (g_data->cdparanoia_pid != 0 || g_data->lame_pid != 0) {
        Sleep(300);
		TRACEINFO("cdparanoia=%d lame=%d", g_data->cdparanoia_pid,
												g_data->lame_pid);
    }
    g_cond_signal(g_data->available);
    g_thread_join(g_data->ripper);
    g_thread_join(g_data->encoder);
    g_thread_join(g_data->tracker);
    // gdk_flush();
    g_data->working = false;
    show_completed_dialog(g_data->numCdparanoiaOk + g_data->numLameOk,
						g_data->numCdparanoiaFailed + g_data->numLameFailed);
    gtk_window_set_title(GTK_WINDOW(win_main), PROGRAM_NAME);
    gtk_widget_hide(LKP_MAIN(WDG_RIPPING));
	gtk_widget_show(LKP_MAIN(WDG_SCROLL));
	enable_all_main_widgets();
	set_status("Job cancelled.");
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
	const char *args[] = { CDPARANOIA_PRG, "-e", "-d", cdrom, trk,
							filename, NULL };

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
		// printf("%s\n", buf);
		int start;
		int end;
		int sector;
		if ((buf[0] == 'R') && (buf[1] == 'i')) {
			sscanf(buf, "Ripping from sector %d", &start);
		} else if (buf[0] == '\t') {
			sscanf(buf, "\t to sector %d", &end);
		} else if (buf[0] == '#') {
			int code;
			char type[20];
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
	TRACEINFO("End of ripping track %d", tracknum);
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
	fmt_status("Ripping track %d ...", tracknum);

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
		fmt_status("Trying to eject disc in '%s'...", g_data->device);
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
	g_data->rip_tracks_completed = 0;
	g_data->encode_tracks_completed = 0;
	g_data->numCdparanoiaFailed = 0;
	g_data->numLameFailed = 0;
	g_data->numCdparanoiaOk = 0;
	g_data->numLameOk = 0;
	g_data->rip_percent = 0.0;
}

// spawn needed threads and begin ripping
static void dorip()
{
	reset_counters();
	set_status("make sure there's at least one format to rip to");
	if (!g_prefs->rip_wav && !g_prefs->rip_mp3) {
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

	set_status("Verify album directory...");
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
		set_status("Creating playlists");

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
	}
	g_free(albumdir);
	g_free(playlist);

	set_status("Working...");
	disable_all_main_widgets();
	gtk_widget_show(LKP_MAIN(WDG_RIPPING));
	gtk_widget_hide(LKP_MAIN(WDG_SCROLL));

	g_data->ripper = g_thread_create(rip_thread, NULL, TRUE, NULL);
	g_data->encoder = g_thread_create(encode_thread, NULL, TRUE, NULL);
	g_data->tracker = g_thread_create(track_thread, NULL, TRUE, NULL);
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
	args[pos++] = "0";
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

	char tr_txt[3];
	snprintf(tr_txt, 3, "%d", tracknum);
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
	int size = 0;
	do {
		char buf[256];
		pos = -1;
		do {
			pos++;
			size = read(fd, &buf[pos], 1);

			/* signal interrupted read(), try again */
			if (size == -1 && errno == EINTR) {
				pos--;
				size = 1;
			}
		} while ((buf[pos] != '\r') && (buf[pos] != '\n')
								&& (size > 0) && (pos < 256));
		buf[pos] = '\0';

		int sector;
		int end;
		if (sscanf(buf, "%d/%d", &sector, &end) == 2) {
			g_data->mp3_percent = (double)sector / end;
		}
	} while (size > 0);
	close(fd);
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
	g_free(musicfilename);
	g_free(albumdir);
	int min;
	int sec;
	sscanf(tracktime, "%d:%d", &min, &sec);

	if (g_prefs->rip_mp3) {
		TRACEINFO("Encoding track %d to '%s'", tracknum, mp3filename);
		if (g_data->aborted) g_thread_exit(NULL);

		struct stat statStruct;
		int	rc = stat(mp3filename, &statStruct);
		bool doEncode = true;
		if (rc == 0) {
			gdk_threads_enter();
			if (!confirmOverwrite(mp3filename))
				doEncode = false;
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
	g_data->encode_tracks_completed++;
}

// the thread that handles encoding WAV files to all other formats
static gpointer encode_thread(gpointer data)
{
	set_main_completion(WDG_ALBUM_ARTIST);
	set_main_completion(WDG_ALBUM_TITLE);
	set_main_completion(WDG_ALBUM_GENRE);

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

	g_mutex_free(g_data->barrier);
	g_data->barrier = NULL;
	g_cond_free(g_data->available);
	g_data->available = NULL;

	TRACEINFO("rip_percent= %f %%", g_data->rip_percent);
	/* wait until all the worker threads are done */
	while (g_data->cdparanoia_pid != 0 || g_data->lame_pid != 0) {
		// printf("w2\n");
		Sleep(300);
	}
	Sleep(200);
	TRACEINFO("Waking up to allDone");
	g_data->allDone = true;		// so the tracker thread will exit
	g_data->working = false;
	gdk_threads_enter();
	show_completed_dialog(g_data->numCdparanoiaOk + g_data->numLameOk,
						g_data->numCdparanoiaFailed + g_data->numLameFailed);
	gtk_widget_hide(LKP_MAIN(WDG_RIPPING));
	gtk_widget_show(LKP_MAIN(WDG_SCROLL));
	enable_all_main_widgets();
	gdk_threads_leave();
	return NULL;
}

// calculates the progress of the other threads and updates the progress bars
static gpointer track_thread(gpointer data)
{
	int parts = 1;
	if (g_prefs->rip_mp3) parts++;
	gdk_threads_enter();
	GtkProgressBar *pbar_total = GTK_PROGRESS_BAR(LKP_MAIN(WDG_PROGRESS_TOTAL));
	GtkProgressBar *pbar_rip = GTK_PROGRESS_BAR(LKP_MAIN(WDG_PROGRESS_RIP));
	GtkProgressBar *pbar_encode = GTK_PROGRESS_BAR(LKP_MAIN(WDG_PROGRESS_ENCODE));
	gtk_progress_bar_set_fraction(pbar_total, 0.0);
	gtk_progress_bar_set_text(pbar_total, _("Waiting..."));
	gtk_progress_bar_set_fraction(pbar_rip, 0.0);
	gtk_progress_bar_set_text(pbar_rip, _("Waiting..."));
	if (parts > 1) {
		gtk_progress_bar_set_fraction(pbar_encode, 0.0);
		gtk_progress_bar_set_text(pbar_encode, _("Waiting..."));
	} else {
		gtk_progress_bar_set_fraction(pbar_encode, 1.0);
		gtk_progress_bar_set_text(pbar_encode, "100% (0/0)");
	}
	gdk_threads_leave();

	double prip;
	int torip;
	int completed;
	char srip[32];
	double pencode = 0;
	char sencode[13];
	double ptotal = 0.0;;
	bool started = false;

	while (!g_data->allDone && ptotal < 1.0) {
		if (g_data->aborted) {
			TRACEWARN("Aborted 1");
			g_thread_exit(NULL);
		}
		if(!started && g_data->rip_percent <= 0.0
			&& (parts == 1 || g_data->mp3_percent <= 0.0)) {
			Sleep(400);
			continue;
		}
		started = true;
		torip = g_data->tracks_to_rip;
		completed = g_data->rip_tracks_completed;

		prip = (completed + g_data->rip_percent) / torip;
		snprintf(srip, 32, "%02.2f%% (%d/%d)", (prip * 100),
				(completed < torip) ? (completed + 1) : torip, torip);

		if(prip>1.0) {
			TRACEWARN("prip overflow=%.2f%% completed=%d rip=%.2f%% remain=%d",
						prip, completed, g_data->rip_percent, torip);
			prip=1.0;
		}
		if (parts > 1) {
			completed = g_data->encode_tracks_completed;
			pencode = ((double)completed / (double)torip)
					+ ((g_data->mp3_percent) / (parts - 1) / torip);

			snprintf(sencode, 13, "%d%% (%d/%d)", (int)(pencode * 100),
				(completed < torip) ? (completed + 1) : torip, torip);

			ptotal = prip / parts + pencode * (parts - 1) / parts;
		} else {
			ptotal = prip;
		}
		if (g_data->aborted) {
			TRACEWARN("Aborted 2");
			g_thread_exit(NULL);
		}
		gdk_threads_enter();
		gtk_progress_bar_set_fraction(pbar_rip, prip);
		gtk_progress_bar_set_text(pbar_rip, srip);
		if (parts > 1) {
			gtk_progress_bar_set_fraction(pbar_encode, pencode);
			gtk_progress_bar_set_text(pbar_encode, sencode);
		}
		gtk_progress_bar_set_fraction(pbar_total, ptotal);
		gchar *stotal = g_strdup_printf("%02.2f%%", ptotal * 100);
		gtk_progress_bar_set_text(pbar_total, stotal);
		gchar *windowTitle = g_strdup_printf("%s - %s", PROGRAM_NAME, stotal);
		gtk_window_set_title(GTK_WINDOW(win_main), windowTitle);
		g_free(windowTitle);
		g_free(stotal);
		gdk_threads_leave();
		Sleep(200);
	}
	gdk_threads_enter();
	set_status("Ready.");
	gtk_window_set_title(GTK_WINDOW(win_main), PROGRAM_NAME);
	gdk_threads_leave();
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
	TRACEINFO("waited for %d, status=%d (know about wav %d, mp3 %d)",
			pid, status, g_data->cdparanoia_pid, g_data->lame_pid);

	if (pid != g_data->cdparanoia_pid && pid != g_data->lame_pid) {
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
		}
	} else {
		if (pid == g_data->cdparanoia_pid) {
			g_data->cdparanoia_pid = 0;
			if (g_prefs->rip_wav)
				g_data->numCdparanoiaOk++;
		} else if (pid == g_data->lame_pid) {
			g_data->lame_pid = 0;
			g_data->numLameOk++;
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
	TRACEINFO("%d started: %s ", *p, args[0]);
	// close the side of the pipe we don't need
	close(pipefd[1]);
	return pipefd[0];
}

int main(int argc, char *argv[])
{
	g_data = g_malloc0(sizeof(shared));
	load_prefs();
	log_init();
#ifdef ENABLE_NLS
	bindtextdomain(PACKAGE, PACKAGE_LOCALE_DIR);
	bind_textdomain_codeset(PACKAGE, UTF8);
	textdomain(PACKAGE);
#endif
	// cddb_log_set_level(CDDB_LOG_DEBUG);
	g_thread_init(NULL);
	gdk_threads_init();
	gtk_init(&argc, &argv);
	win_main = create_main();
	win_prefs = create_prefs();
	gtk_widget_show(win_main);
	lookup_cdparanoia();
	// recurring timeout to automatically re-scan cdrom once in a while
	g_timeout_add(10000, idle, (void *)1);
	// add an idle event to scan the cdrom drive ASAP
	g_idle_add(scan_on_startup, NULL);
	gtk_main();
	free_prefs(g_prefs);
	log_end();
	g_free(g_data);
	libcddb_shutdown();
	return 0;
}

