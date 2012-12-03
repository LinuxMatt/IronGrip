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
#include <errno.h>
#include <fcntl.h>
#include <gdk/gdkkeysyms.h>
#include <glib.h>
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
#define EXEC_OUTPUT_LOG "/tmp/irongrip.execoutput"

#define LAME_PRG "lame"
#define CDPARANOIA_PRG "cdparanoia"

#define WDG_ALBUM_ARTIST "album_artist"
#define WDG_ALBUM_TITLE "album_title"
#define WDG_ALBUM_GENRE "album_genre"
#define WDG_ALBUM_YEAR "album_year"
#define WDG_TRACKLIST "tracklist"
#define WDG_PICK_DISC "pick_disc"

#define Sleep(x) usleep(x*1000)
#define HOOKUP(component,widget,name) g_object_set_data_full (G_OBJECT (component), name,  gtk_widget_ref (widget), (GDestroyNotify) gtk_widget_unref)
#define HOOKUP_OBJ_NOREF(component,widget,name) g_object_set_data (G_OBJECT (component), name, widget)

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

typedef struct _prefs {
    char* cdrom;
    char* music_dir;
    int make_playlist;
    char* format_music;
    char* format_playlist;
    char* format_albumdir;
    int rip_wav;
    int rip_mp3;
    int mp3_vbr;
    int mp3_bitrate;
    int mp3_quality;
    int main_window_width;
    int main_window_height;
    int eject_on_done;
    int do_cddb_updates;
    int use_proxy;
    char* server_name;
    int port_number;
    int do_log;
    int max_log_size;
    char* log_file;
    char* cddb_server_name;
    int cddb_port_number;
} prefs;

typedef struct _shared {
	FILE * playlist_wav;
	FILE *playlist_mp3;
	FILE *log_fd;
	int rip_tracks_completed;
	bool allDone; /* for stopping the tracking thread */
	double rip_percent;
	double mp3_percent;
	bool aborted; /* for canceling */
	int cddb_query_thread_num_matches;
} shared;

static GCond * available = NULL;
static GList * disc_matches = NULL;
static GMutex * barrier = NULL;
static GThread * cddb_query_thread;
static GThread * encoder;
static GThread * ripper;
static GThread * tracker;
static GtkWidget * win_main = NULL;
static GtkWidget * win_prefs = NULL;
static bool overwriteAll;
static bool overwriteNone;
static bool working; /* ripping or encoding, so that can know not to clear the tracklist on eject */
static cddb_conn_t * cddb_query_thread_conn;
static cddb_disc_t * cddb_query_thread_disc;
static gboolean track_format[100];
static int cddb_query_thread_running;
static int counter;
static int encode_tracks_completed;
static int numCdparanoiaFailed;
static int numCdparanoiaOk;
static int numLameFailed;
static int numLameOk;
static int tracks_to_rip;
static pid_t cdparanoia_pid;
static pid_t lame_pid;
static prefs *global_prefs = NULL;
static shared *global_data = NULL;

#define LOG_NONE	0	//!< Absolutely quiet. DO NOT USE.
#define LOG_FATAL	1	//!< Only errors are reported.
#define LOG_WARN	2	//!< Warnings.
#define LOG_INFO	3	//!< Informational messages
#define LOG_TRACE	4	//!< Trace messages. Debug only.
#define LOG_DEBUG	5	//!< Verbose debugging.

/** Write current time in the log file. */
static void log_time()
{
	struct tm *d;
	struct timeb s_timeb;
	ftime(&s_timeb);
	d = localtime(&s_timeb.time);
	fprintf(global_data->log_fd, "\n#%02d/%02d/%04d %02d:%02d:%02d.%03u|", d->tm_mday, d->tm_mon + 1, 1900 + d->tm_year, d->tm_hour, d->tm_min, d->tm_sec, s_timeb.millitm);
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
	snprintf(time_str, 32, "%02d/%02d/%04d %02d:%02d:%02d.%03u", d->tm_mday, d->tm_mon + 1, 1900 + d->tm_year, d->tm_hour, d->tm_min, d->tm_sec, s_timeb.millitm);
	return time_str;
}
static bool log_init()
{
	struct stat sts;
	char time_str[32];
	char mode_write[] = "w";
	char mode_append[] = "a";
	char *fmode = mode_append;

	global_data->log_fd = NULL; // We will not try to write traces if no log file is opened.

	if(!global_prefs->do_log)
		return TRUE;

	fmode = mode_append;
	if(stat(global_prefs->log_file,&sts)==0) {
		if(sts.st_size>global_prefs->max_log_size) { // File is too big, overwrite it.
			fmode=mode_write;
		}
	}
	if( (global_data->log_fd=fopen(global_prefs->log_file,fmode)) == NULL) {
		return FALSE;
	}
	get_time_str(time_str);
	fprintf(global_data->log_fd, "\n\n ----- %s START -----", time_str);
	fflush(global_data->log_fd);
	return TRUE;
}

static void log_end()
{
	char time_str[32];

	if(global_data->log_fd == NULL)
		return;

	if(!global_prefs->do_log)
		return;

	get_time_str(time_str);
	fprintf(global_data->log_fd, "\n ----- %s STOP -----\n", time_str);
	fflush(global_data->log_fd);
	fclose(global_data->log_fd);
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
	if(global_data->log_fd==NULL)
		global_data->log_fd = stderr;

	if(global_prefs->do_log == 0 && log_level != LOG_FATAL)
		return;

	va_list args;
	log_time();
	fprintf(global_data->log_fd,"0x%04lX|%04d|%s|%s|", pthread_self(), ++n, strloglevel(log_level), func_name);
	va_start(args,fmt);
	vfprintf(global_data->log_fd, fmt, args);
	va_end(args);
	fflush(global_data->log_fd);
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
                log_gen(__func__, LOG_FATAL, "[%s] param value of [%s] is too long (%d)", filename, field, p-s+1);
                return 0;
            }
            *value = g_malloc0(p-s+4);
            memset(*value, 0 , p-s+4);
            memcpy(*value, s, p-s);
            return 1;
        }
        if(end) {
            break;
        }
        p++;
    }
    log_gen(__func__, LOG_WARN, "[%s] param %s not found", filename, field);
    return 0;
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

static long get_field_as_long(char *buf, char *filename, char *field, int *value)
{
	char *num = NULL;
	if(!get_field(buf, filename, field, &num)) {
		log_gen(__func__, LOG_WARN, "missing field %s", field);
		return 0;
	}
	long ret = 0;
	if(read_long(num, &ret)) {
		*value = ret;
	}
	g_free(num);
	return ret;
}

static int load_file(char *url, char **b)
{
	if (url == NULL) return 0;
	gsize sz = 0;
	if(!g_file_get_contents(url, b, &sz, NULL)) {
		log_gen(__func__, LOG_WARN, "Warning: could not load config file: %s", strerror(errno));
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

static void fatalError(const char *message)
{
	fprintf(stderr, "Fatal error: %s\n", message);
	exit(-1);
}

// converts a gtk slider's integer range to a meaningful bitrate
// NOTE: i grabbed these bitrates from the list in the LAME man page
// and from http://wiki.hydrogenaudio.org/index.php?title=LAME#VBR_.28variable_bitrate.29_settings
static int int_to_bitrate(int i, bool vbr)
{
	switch (i) {
	case 0:
		if (vbr)
			return 65;
		else
			return 32;
	case 1:
		if (vbr)
			return 65;
		else
			return 40;
	case 2:
		if (vbr)
			return 65;
		else
			return 48;
	case 3:
		if (vbr)
			return 65;
		else
			return 56;
	case 4:
		if (vbr)
			return 85;
		else
			return 64;
	case 5:
		if (vbr)
			return 100;
		else
			return 80;
	case 6:
		if (vbr)
			return 115;
		else
			return 96;
	case 7:
		if (vbr)
			return 130;
		else
			return 112;
	case 8:
		if (vbr)
			return 165;
		else
			return 128;
	case 9:
		if (vbr)
			return 175;
		else
			return 160;
	case 10:
		if (vbr)
			return 190;
		else
			return 192;
	case 11:
		if (vbr)
			return 225;
		else
			return 224;
	case 12:
		if (vbr)
			return 245;
		else
			return 256;
	case 13:
		if (vbr)
			return 245;
		else
			return 320;
	}
	log_gen(__func__, LOG_WARN, "int_to_bitrate() called with bad parameter (%d)", i);
	return 32;
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
static char *make_filename(const char *path, const char *dir, const char *file, const char *extension)
{
	int len = 1;
	char *ret = NULL;
	int pos = 0;

	if (path) len += strlen(path) + 1;
	if (dir) len += strlen(dir) + 1;
	if (file) len += strlen(file);
	if (extension) len += strlen(extension) + 1;

	ret = g_malloc0(sizeof(char) * len);
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
static char *parse_format(const char *format, int tracknum, const char *year, const char *artist, const char *album, const char *genre, const char *title)
{
	unsigned i = 0;
	int len = 0;
	char *ret = NULL;
	int pos = 0;

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
	ret = g_malloc0(sizeof(char) * (len + 1));

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
	char **paths = g_malloc0(sizeof(char *) * numpaths);
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
static int recursive_mkdir(char *pathAndName, mode_t mode)
{
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
//
// str - the string to trim
// bad - the sting containing all the characters to remove
static void trim_chars(char *str, const char *bad)
{
	int i;
	int pos;
	int len = strlen(str);
	unsigned b;

	for (b = 0; b < strlen(bad); b++) {
		pos = 0;
		for (i = 0; i < len + 1; i++) {
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
#define SANITIZE(x) trim_chars(x, BADCHARS); trim_whitespace(x)

static GtkWidget *lookup_widget(GtkWidget * widget, const gchar * widget_name)
{
	GtkWidget *parent = NULL;
	for (;;) {
		parent = widget->parent;
		if (parent == NULL) break;
		widget = parent;
	}
	GtkWidget *found_widget = (GtkWidget *) g_object_get_data(G_OBJECT(widget), widget_name);
	if (!found_widget)
		g_warning("Widget not found: %s", widget_name);

	return found_widget;
}

#define LKP_MAIN(x) lookup_widget(win_main, x)
#define LKP_PREF(x) lookup_widget(win_prefs, x)
#define DIALOG_INFO_OK(...) show_dialog(GTK_MESSAGE_INFO, GTK_BUTTONS_OK,  __VA_ARGS__)
#define DIALOG_WARNING_OK(...) show_dialog(GTK_MESSAGE_WARNING, GTK_BUTTONS_OK,  __VA_ARGS__)
#define DIALOG_ERROR_OK(...) show_dialog(GTK_MESSAGE_ERROR, GTK_BUTTONS_OK,  __VA_ARGS__)
#define DIALOG_INFO_CLOSE(...) show_dialog(GTK_MESSAGE_INFO, GTK_BUTTONS_CLOSE,  __VA_ARGS__)
#define DIALOG_WARNING_CLOSE(...) show_dialog(GTK_MESSAGE_WARNING, GTK_BUTTONS_CLOSE,  __VA_ARGS__)
#define GET_PREF_TEXT(x) gtk_entry_get_text(GTK_ENTRY(LKP_PREF(x)))
#define GET_MAIN_TEXT(x) gtk_entry_get_text(GTK_ENTRY(LKP_MAIN(x)))

static void show_dialog(GtkMessageType type, GtkButtonsType buttons, char *fmt, ...)  {
    va_list args;
    va_start(args,fmt);
    gchar *txt = g_strdup_vprintf(fmt, args);
    va_end(args);
    GtkWidget* dialog = gtk_message_dialog_new(GTK_WINDOW(win_main), GTK_DIALOG_DESTROY_WITH_PARENT, type, buttons, "%s", txt);
    gtk_dialog_run (GTK_DIALOG (dialog));
    gtk_widget_destroy (dialog);
    g_free(txt);
}

#define CLEAR_TEXT(x) gtk_entry_set_text(GTK_ENTRY(LKP_MAIN(x)), "")

static void clear_widgets()
{
	// hide the widgets for multiple albums
	gtk_widget_hide(LKP_MAIN("disc"));
	gtk_widget_hide(LKP_MAIN(WDG_PICK_DISC));
	// clear the textboxes
	CLEAR_TEXT("album_artist");
	CLEAR_TEXT("album_title");
	CLEAR_TEXT("album_genre");
	CLEAR_TEXT("album_year");
	// clear the tracklist
	gtk_tree_view_set_model(GTK_TREE_VIEW(LKP_MAIN("tracklist")), NULL);
	// disable the "rip" button
	gtk_widget_set_sensitive(LKP_MAIN("rip_button"), FALSE);
}

static bool check_disc(char *cdrom)
{
	// open the device
	int fd = open(cdrom, O_RDONLY | O_NONBLOCK);
	if (fd < 0) {
		log_gen(__func__, LOG_WARN, "Error: Couldn't open %s", cdrom);
		return false;
	}
	static bool alreadyKnowGood = false;	/* check when program just started */
	static bool alreadyCleared = true;	/* no need to clear when program just started */

	bool ret = false;
	int status = ioctl(fd, CDROM_DISC_STATUS, CDSL_CURRENT);
	if (status == CDS_AUDIO || status == CDS_MIXED) {
		if (!alreadyKnowGood) {
			ret = true;
			alreadyKnowGood = true;	/* don't return true again for this disc */
			alreadyCleared = false;	/* clear when disc is removed */
		}
	} else {
		alreadyKnowGood = false;	/* return true when good disc inserted */
		if (!alreadyCleared) {
			alreadyCleared = true;
			clear_widgets();
		}
	}
	close(fd);
	return ret;
}

static GtkTreeModel *create_model_from_disc(cddb_disc_t * disc)
{
	GtkListStore *store = gtk_list_store_new(NUM_COLS, G_TYPE_BOOLEAN,	/* rip? checkbox */
				   G_TYPE_UINT,	/* track number */
				   G_TYPE_STRING,	/* track artist */
				   G_TYPE_STRING,	/* track title */
				   G_TYPE_STRING,	/* track time */
				   G_TYPE_STRING,	/* genre */
				   G_TYPE_STRING	/* year */
	    );

	cddb_track_t *track;
	for (track = cddb_disc_get_track_first(disc); track != NULL; track = cddb_disc_get_track_next(disc)) {
		int seconds = cddb_track_get_length(track);
		char time[6];
		snprintf(time, 6, "%02d:%02d", seconds / 60, seconds % 60);

		char *track_artist = (char *)cddb_track_get_artist(track);
		SANITIZE(track_artist);

		char *track_title = (char *)cddb_track_get_title(track);	//!! this returns const char*
		SANITIZE(track_title);

		char year_str[5];
		snprintf(year_str, 5, "%d", cddb_disc_get_year(disc));
		year_str[4] = '\0';

		GtkTreeIter iter;
		gtk_list_store_append(store, &iter);
		gtk_list_store_set(store, &iter,
				   COL_RIPTRACK, track_format[cddb_track_get_number(track)],
				   COL_TRACKNUM, cddb_track_get_number(track),
				   COL_TRACKARTIST, track_artist,
				   COL_TRACKTITLE, track_title,
				   COL_TRACKTIME, time, COL_GENRE, cddb_disc_get_genre(disc), COL_YEAR, year_str, -1);
	}
	return GTK_TREE_MODEL(store);
}

static void eject_disc(char *cdrom)
{
	int fd = open(cdrom, O_RDONLY | O_NONBLOCK);
	if (fd < 0) {
		log_gen(__func__, LOG_WARN, "Error: Couldn't open %s", cdrom);
		return;
	}
	ioctl(fd, CDROM_LOCKDOOR, 0);
	int eject = ioctl(fd, CDROMEJECT, 0 /* CDSL_CURRENT */);
	if(eject < 0) {
		perror("ioctl");
		log_gen(__func__, LOG_INFO, "Eject returned %d", eject);

		// TODO: use '/usr/bin/eject' on ubuntu because ioctl seems to work only as root
	}
	close(fd);
}

static gpointer cddb_query_thread_run(gpointer data)
{
	global_data->cddb_query_thread_num_matches = cddb_query(cddb_query_thread_conn, cddb_query_thread_disc);
	log_gen(__func__, LOG_INFO, "Got %d matches for CDDB QUERY.", global_data->cddb_query_thread_num_matches);
	if (global_data->cddb_query_thread_num_matches == -1)
		global_data->cddb_query_thread_num_matches = 0;

	g_atomic_int_set(&cddb_query_thread_running, 0);
	return NULL;
}

static void disable_all_main_widgets(void);
static void enable_all_main_widgets(void);

static GList *lookup_disc(cddb_disc_t * disc)
{
	// set up the connection to the cddb server
	cddb_query_thread_conn = cddb_new();
	if (cddb_query_thread_conn == NULL)
		fatalError("cddb_new() failed. Out of memory?");

	cddb_set_server_name(cddb_query_thread_conn, global_prefs->cddb_server_name);
	cddb_set_server_port(cddb_query_thread_conn, global_prefs->cddb_port_number);

	if (global_prefs->use_proxy) {
		cddb_set_http_proxy_server_name(cddb_query_thread_conn, global_prefs->server_name);
		cddb_set_http_proxy_server_port(cddb_query_thread_conn, global_prefs->port_number);
		cddb_http_proxy_enable(cddb_query_thread_conn);
	}
	// query cddb to find similar discs
	g_atomic_int_set(&cddb_query_thread_running, 1);
	cddb_query_thread_disc = disc;
	cddb_query_thread = g_thread_create(cddb_query_thread_run, NULL, TRUE, NULL);

	// show cddb update window
	gdk_threads_enter();
	disable_all_main_widgets();

	GtkWidget *statusLbl = LKP_MAIN("statusLbl");
	gtk_label_set_text(GTK_LABEL(statusLbl), _("<b>Getting disc info from the internet...</b>"));
	gtk_label_set_use_markup(GTK_LABEL(statusLbl), TRUE);

	while (g_atomic_int_get(&cddb_query_thread_running) != 0) {
		while (gtk_events_pending())
			gtk_main_iteration();
		Sleep(300);
	}
	gtk_label_set_text(GTK_LABEL(statusLbl), "Query finished.");
	enable_all_main_widgets();
	gdk_threads_leave();

	// make a list of all the matches
	GList *matches = NULL;
	int i;
	for (i = 0; i < global_data->cddb_query_thread_num_matches; i++) {
		cddb_disc_t *possible_match = cddb_disc_clone(disc);
		if (!cddb_read(cddb_query_thread_conn, possible_match)) {
			cddb_error_print(cddb_errno(cddb_query_thread_conn));
			fatalError("cddb_read() failed.");
		}
		matches = g_list_append(matches, possible_match);

		// move to next match
		if (i < global_data->cddb_query_thread_num_matches - 1) {
			if (!cddb_query_next(cddb_query_thread_conn, disc))
				fatalError("Query index out of bounds.");
		}
	}
	cddb_destroy(cddb_query_thread_conn);
	return matches;
}

static cddb_disc_t *read_disc(char *cdrom)
{
	int fd = open(cdrom, O_RDONLY | O_NONBLOCK);
	if (fd < 0) {
		log_gen(__func__, LOG_WARN, "Error: Couldn't open %s", cdrom);
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
	if (ioctl(fd, CDROMREADTOCHDR, &th) == 0) {
		log_gen(__func__, LOG_INFO, "starting track: %d", th.cdth_trk0);
		log_gen(__func__, LOG_INFO, "ending track: %d", th.cdth_trk1);

		disc = cddb_disc_new();
		if (disc == NULL)
			fatalError("cddb_disc_new() failed. Out of memory?");

		struct cdrom_tocentry te;
		te.cdte_format = CDROM_LBA;
		int i;
		for (i = th.cdth_trk0; i <= th.cdth_trk1; i++) {
			te.cdte_track = i;
			if (ioctl(fd, CDROMREADTOCENTRY, &te) == 0) {
				if (te.cdte_ctrl & CDROM_DATA_TRACK) {
					// track is a DATA track. make sure its "rip" box is not checked by default
					track_format[i] = FALSE;
				} else {
					track_format[i] = TRUE;
				}
				cddb_track_t *track = cddb_track_new();
				if (track == NULL) fatalError("cddb_track_new() failed. Out of memory?");

				char trackname[9];
				cddb_track_set_frame_offset(track, te.cdte_addr.lba + SECONDS_TO_FRAMES(2));
				snprintf(trackname, 9, "Track %d", i);
				cddb_track_set_title(track, trackname);
				cddb_track_set_artist(track, "Unknown Artist");
				cddb_disc_add_track(disc, track);
			}
		}
		te.cdte_track = CDROM_LEADOUT;
		if (ioctl(fd, CDROMREADTOCENTRY, &te) == 0) {
			cddb_disc_set_length(disc, (te.cdte_addr.lba + SECONDS_TO_FRAMES(2)) / SECONDS_TO_FRAMES(1));
		}
	}
end:
	close(fd);
	return disc;
}

#define TOGGLE_ACTIVE(x,y) gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(lookup_widget(win_main, x)), y)

static void update_tracklist(cddb_disc_t * disc)
{
	cddb_track_t *track;
	char *disc_artist = (char *)cddb_disc_get_artist(disc);
	char *disc_genre = (char *)cddb_disc_get_genre(disc);
	char *disc_title = (char *)cddb_disc_get_title(disc);

	if (disc_artist != NULL) {
		SANITIZE(disc_artist);
		gtk_entry_set_text(GTK_ENTRY(LKP_MAIN("album_artist")), disc_artist);

		bool singleartist= true;
		for (track = cddb_disc_get_track_first(disc); track != NULL; track = cddb_disc_get_track_next(disc)) {
			if (strcmp(disc_artist, cddb_track_get_artist(track)) != 0) {
				singleartist = false;
				break;
			}
		}
		TOGGLE_ACTIVE("single_artist", singleartist);
	}
	if (disc_title != NULL) {
		SANITIZE(disc_title);
		gtk_entry_set_text(GTK_ENTRY(LKP_MAIN("album_title")), disc_title);
	}
	if (disc_genre) {
		SANITIZE(disc_genre);
		gtk_entry_set_text(GTK_ENTRY(LKP_MAIN("album_genre")), disc_genre);
	} else {
		gtk_entry_set_text(GTK_ENTRY(LKP_MAIN("album_genre")), "Unknown");
	}
	unsigned disc_year = cddb_disc_get_year(disc);
	if (disc_year == 0) disc_year = 1900;

	char disc_year_char[5];
	snprintf(disc_year_char, 5, "%d", disc_year);
	gtk_entry_set_text(GTK_ENTRY(LKP_MAIN("album_year")), disc_year_char);
	TOGGLE_ACTIVE("single_genre", true);
	GtkTreeModel *model = create_model_from_disc(disc);
	gtk_tree_view_set_model(GTK_TREE_VIEW(LKP_MAIN("tracklist")), model);
	g_object_unref(model);
}

#define WIDGET_SHOW(x) gtk_widget_show(lookup_widget(win_main, x))

static void refresh(char *cdrom, int force)
{
	if (working) // don't do anything
		return;

	if (!check_disc(cdrom) && !force) {
		return;
	}
	cddb_disc_t *disc = read_disc(cdrom);
	if (disc == NULL) return;

	gtk_widget_set_sensitive(LKP_MAIN("rip_button"), TRUE);

	// show the temporary info
	gtk_entry_set_text(GTK_ENTRY(LKP_MAIN("album_artist")), "Unknown Artist");
	gtk_entry_set_text(GTK_ENTRY(LKP_MAIN("album_title")), "Unknown Album");
	update_tracklist(disc);

	if (!global_prefs->do_cddb_updates && !force)
		return;

	disc_matches = lookup_disc(disc);
	cddb_disc_destroy(disc);
	if (disc_matches == NULL) {
		log_gen(__func__, LOG_WARN, "No disc matches !");
		return;
	}
	// fill in and show the album drop-down box
	if (g_list_length(disc_matches) > 1) {
		GtkListStore *store = gtk_list_store_new(2, G_TYPE_STRING, G_TYPE_STRING);
		GtkTreeIter iter;
		GList *curr;
		cddb_disc_t *tempdisc;

		for (curr = g_list_first(disc_matches); curr != NULL; curr = g_list_next(curr)) {
			char *artist = NULL;
			char *title = NULL;

			tempdisc = (cddb_disc_t *) curr->data;
			artist =  (char *) cddb_disc_get_artist(tempdisc);
			title =  (char *) cddb_disc_get_title(tempdisc);
			log_gen(__func__, LOG_INFO, "Artist: [%s] Title [%s]", artist, title);

			gtk_list_store_append(store, &iter);
			gtk_list_store_set(store, &iter, 0, artist, 1, title, -1);
		}
		gtk_combo_box_set_model(GTK_COMBO_BOX(LKP_MAIN(WDG_PICK_DISC)), GTK_TREE_MODEL(store));
		gtk_combo_box_set_active(GTK_COMBO_BOX(LKP_MAIN(WDG_PICK_DISC)), 1);
		gtk_combo_box_set_active(GTK_COMBO_BOX(LKP_MAIN(WDG_PICK_DISC)), 0);
		WIDGET_SHOW("disc");
		WIDGET_SHOW(WDG_PICK_DISC);
	}
	update_tracklist((cddb_disc_t *) g_list_nth_data(disc_matches, 0));
}

static gboolean for_each_row_deselect(GtkTreeModel * model, GtkTreePath * path, GtkTreeIter * iter, gpointer data)
{
    gtk_list_store_set(GTK_LIST_STORE(model), iter, COL_RIPTRACK, 0, -1);
    return FALSE;
}

static gboolean for_each_row_select(GtkTreeModel * model, GtkTreePath * path, GtkTreeIter * iter, gpointer data)
{
    gtk_list_store_set(GTK_LIST_STORE(model), iter, COL_RIPTRACK, 1, -1);
    return FALSE;
}

static gboolean idle(gpointer data)
{
    refresh(global_prefs->cdrom, 0);
    return (data != NULL);
}

static gboolean scan_on_startup(gpointer data)
{
    log_gen(__func__, LOG_INFO, "scan_on_startup");
    refresh(global_prefs->cdrom, 0);
    return (data != NULL);
}

static void show_aboutbox(void)
{
	GError* error = NULL;
	GdkPixbuf* icon = NULL;
	GdkPixbufLoader *loader = gdk_pixbuf_loader_new();

	gchar *license = PROGRAM_NAME
					" is distributed under the GNU General Public Licence\n"
					"version 2, please see COPYING file for the complete text.\n";

	gchar* authors[] = { AUTHOR,
						"",
						"IronGrip is a fork of the Asunder program.",
						"Many thanks to the authors and contributors",
						"of the Asunder project.",
						"",
						"See file 'README.md' for details.",
						NULL };

	gchar* comments = N_("An application to save tracks from an Audio CD as WAV and/or MP3.");
	gchar* copyright = {"Copyright 2005 Eric Lathrop (Asunder)\n"
						"Copyright 2007 Andrew Smith (Asunder)\n"
						"Copyright 2012 " AUTHOR};

	gchar* name = PROGRAM_NAME;
	gchar* version = VERSION;
	gchar* website = HOMEPAGE;
	gchar* website_label = website;

	gdk_pixbuf_loader_write(loader, pixmaps_irongrip_png, pixmaps_irongrip_png_len, &error);
	if (error) {
		g_warning ("Unable to load logo : %s\n", error->message);
		g_error_free (error);
		return;
	}
	gdk_pixbuf_loader_close(loader,NULL);
	icon = gdk_pixbuf_loader_get_pixbuf(loader);
	if (!icon) {
		return;
	}

	gtk_show_about_dialog (
			GTK_WINDOW(LKP_MAIN("main")),
			"authors", authors,
			// "artists", artists,
			"comments", comments,
			"copyright", copyright,
			// "documenters", documenters,
			"logo", icon,
			"name", name,
			"program-name", name,
			"version", version,
			"website", website,
			"website-label", website_label,
			// "translator-credits", translators,
			"license", license,
			NULL);
}

static void on_about_clicked(GtkToolButton * toolbutton, gpointer user_data)
{
    show_aboutbox();
}

static gboolean on_year_focus_out_event(GtkWidget * widget, GdkEventFocus * event, gpointer user_data)
{
	const gchar *ctext = gtk_entry_get_text(GTK_ENTRY(widget));
	gchar *text = g_malloc0(5);
	strncpy(text, ctext, 5);
	text[4] = '\0';

	if ((text[0] != '1' && text[0] != '2') || text[1] < '0' || text[1] > '9' ||
	    text[2] < '0' || text[2] > '9' || text[3] < '0' || text[3] > '9') {
		sprintf(text, "1900");
	}
	gtk_entry_set_text(GTK_ENTRY(widget), text);
	g_free(text);
	return FALSE;
}

static gboolean on_field_focus_out_event(GtkWidget * widget)
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

static gboolean on_album_artist_focus_out_event(GtkWidget * widget, GdkEventFocus * event, gpointer user_data)
{
	return on_field_focus_out_event(widget);
}
static gboolean on_album_title_focus_out_event(GtkWidget * widget, GdkEventFocus * event, gpointer user_data)
{
	return on_field_focus_out_event(widget);
}
static gboolean on_album_genre_focus_out_event(GtkWidget * widget, GdkEventFocus * event, gpointer user_data)
{
	return on_field_focus_out_event(widget);
}

static void on_artist_edited(GtkCellRendererText * cell, gchar * path_string, gchar * new_text, gpointer user_data)
{
	GtkListStore *store = GTK_LIST_STORE(gtk_tree_view_get_model(GTK_TREE_VIEW(LKP_MAIN(WDG_TRACKLIST))));
	SANITIZE(new_text);
	GtkTreeIter iter;
	gtk_tree_model_get_iter_from_string(GTK_TREE_MODEL(store), &iter, path_string);
	if (new_text[0] == '\0')
		gtk_list_store_set(store, &iter, COL_TRACKARTIST, STR_UNKNOWN, -1);
	else
		gtk_list_store_set(store, &iter, COL_TRACKARTIST, new_text, -1);
}

static void on_genre_edited(GtkCellRendererText * cell, gchar * path_string, gchar * new_text, gpointer user_data)
{
	GtkListStore *store = GTK_LIST_STORE(gtk_tree_view_get_model(GTK_TREE_VIEW(LKP_MAIN(WDG_TRACKLIST))));
	SANITIZE(new_text);
	GtkTreeIter iter;
	gtk_tree_model_get_iter_from_string(GTK_TREE_MODEL(store), &iter, path_string);
	if (new_text[0] == '\0')
		gtk_list_store_set(store, &iter, COL_GENRE, STR_UNKNOWN, -1);
	else
		gtk_list_store_set(store, &iter, COL_GENRE, new_text, -1);
}

static void on_deselect_all_click(GtkMenuItem * menuitem, gpointer data)
{
	GtkTreeModel *model = gtk_tree_view_get_model(GTK_TREE_VIEW(LKP_MAIN(WDG_TRACKLIST)));
	gtk_tree_model_foreach(model, for_each_row_deselect, NULL);
}

static void on_vbr_toggled(GtkToggleButton * togglebutton, gpointer user_data)
{
	/* update the displayed vbr, as it's different for vbr and non-vbr */
	bool vbr = gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(togglebutton));
	GtkRange *range = GTK_RANGE(LKP_PREF("mp3bitrate"));
	char bitrate[8];
	snprintf(bitrate, 8, _("%dKbps"), int_to_bitrate((int)gtk_range_get_value(range), vbr));
	gtk_label_set_text(GTK_LABEL(LKP_PREF("bitrate_lbl_2")), bitrate);
}

static void on_mp3bitrate_value_changed(GtkRange * range, gpointer user_data)
{
	bool vbr = gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(LKP_PREF("mp3_vbr")));
	char bitrate[8];
	snprintf(bitrate, 8, _("%dKbps"), int_to_bitrate((int)gtk_range_get_value(range), vbr));
	gtk_label_set_text(GTK_LABEL(LKP_PREF("bitrate_lbl_2")), bitrate);
}

static void on_mp3_quality_changed(GtkComboBox * combobox, gpointer user_data)
{
	// gint index = gtk_combo_box_get_active (GTK_COMBO_BOX (combobox));
	// printf("SELECTED INDEX=[%d]\n", index);

	gchar * p_text = gtk_combo_box_get_active_text (GTK_COMBO_BOX (combobox));
	// printf("TEXT=[%s]\n", p_text);
	g_free(p_text);
}

static void on_pick_disc_changed(GtkComboBox * combobox, gpointer user_data)
{
	cddb_disc_t *disc = g_list_nth_data(disc_matches, gtk_combo_box_get_active(combobox));
	update_tracklist(disc);
}

static GtkWidget* create_prefs (void);
static void on_preferences_clicked(GtkToolButton * toolbutton, gpointer user_data)
{
	win_prefs = create_prefs();
	gtk_widget_show(win_prefs);
}

static bool prefs_are_valid(void);
static void get_prefs_from_widgets(prefs * p);
static void save_prefs(prefs * p);

static void on_prefs_response(GtkDialog * dialog, gint response_id, gpointer user_data)
{
	if (response_id == GTK_RESPONSE_OK) {
		if (!prefs_are_valid()) return;

		get_prefs_from_widgets(global_prefs);
		save_prefs(global_prefs);
	}
	gtk_widget_destroy(GTK_WIDGET(dialog));
}

static void set_widgets_from_prefs(prefs * p);
static void on_prefs_show(GtkWidget * widget, gpointer user_data)
{
	set_widgets_from_prefs(global_prefs);
}

static void on_press_f2(void)
{
	GtkWidget *treeView = LKP_MAIN(WDG_TRACKLIST);
	if (!GTK_WIDGET_HAS_FOCUS(treeView)) return;

	GtkTreePath *treePath;
	GtkTreeViewColumn *focusColumn;
	gtk_tree_view_get_cursor(GTK_TREE_VIEW(treeView), &treePath, &focusColumn);
	if (treePath == NULL || focusColumn == NULL)
		return;

	gtk_tree_view_set_cursor(GTK_TREE_VIEW(treeView), treePath, focusColumn, TRUE);
}

static void on_lookup_clicked(GtkToolButton * toolbutton, gpointer user_data)
{
	gdk_threads_leave();
	refresh(global_prefs->cdrom, 1);
	gdk_threads_enter();
}

static void dorip();

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

static void on_rip_button_clicked(GtkButton * button, gpointer user_data)
{
	GtkListStore *store = GTK_LIST_STORE(gtk_tree_view_get_model(GTK_TREE_VIEW(LKP_MAIN(WDG_TRACKLIST))));
	if (store == NULL) {
		DIALOG_ERROR_OK(_("No CD is inserted. Please insert a CD into the CD-ROM drive."));
		return;
	}
	if(!lookup_cdparanoia()) return;
	dorip();
}

static void disable_mp3_widgets(void);
static void enable_mp3_widgets(void);


static void on_rip_mp3_toggled(GtkToggleButton * button, gpointer user_data)
{
	if (gtk_toggle_button_get_active(button) && !program_exists(LAME_PRG)) {
		DIALOG_ERROR_OK(_("The '%s' program was not found in your 'PATH'."
						" %s requires that program in order to create MP3 files. All MP3 functionality is disabled."
						" You should install '%s' on this computer if you want to convert audio tracks to MP3."),
						LAME_PRG, PROGRAM_NAME, LAME_PRG);

		global_prefs->rip_mp3 = 0;
		gtk_toggle_button_set_active(button, global_prefs->rip_mp3);
	}
	if (!gtk_toggle_button_get_active(button))
		disable_mp3_widgets();
	else
		enable_mp3_widgets();
}

static void on_rip_toggled(GtkCellRendererToggle * cell, gchar * path_string, gpointer user_data)
{
	log_gen(__func__, LOG_INFO, "on_rip_toggled !");
	GtkListStore *store = GTK_LIST_STORE(gtk_tree_view_get_model(GTK_TREE_VIEW(LKP_MAIN(WDG_TRACKLIST))));
	GtkTreeIter iter;
	gtk_tree_model_get_iter_from_string(GTK_TREE_MODEL(store), &iter, path_string);
	int toggled;
	gtk_tree_model_get(GTK_TREE_MODEL(store), &iter, COL_RIPTRACK, &toggled, -1);
	gtk_list_store_set(store, &iter, COL_RIPTRACK, !toggled, -1);
}

static void on_select_all_click(GtkMenuItem * menuitem, gpointer data)
{
	GtkTreeModel *model = gtk_tree_view_get_model(GTK_TREE_VIEW(LKP_MAIN(WDG_TRACKLIST)));
	gtk_tree_model_foreach(model, for_each_row_select, NULL);
}

static void on_single_artist_toggled(GtkToggleButton * togglebutton, gpointer user_data)
{
	GtkTreeViewColumn *col = gtk_tree_view_get_column(GTK_TREE_VIEW(LKP_MAIN("tracklist")), COL_TRACKARTIST);	//lnr
	gtk_tree_view_column_set_visible(col, !gtk_toggle_button_get_active(togglebutton));
}

static void on_single_genre_toggled(GtkToggleButton * togglebutton, gpointer user_data)
{
	GtkTreeViewColumn *col = gtk_tree_view_get_column(GTK_TREE_VIEW(LKP_MAIN("tracklist")), COL_GENRE);
	gtk_tree_view_column_set_visible(col, !gtk_toggle_button_get_active(togglebutton));
}

static void on_title_edited(GtkCellRendererText * cell, gchar * path_string, gchar * new_text, gpointer user_data)
{
	GtkListStore *store = GTK_LIST_STORE(gtk_tree_view_get_model(GTK_TREE_VIEW(LKP_MAIN(WDG_TRACKLIST))));
	SANITIZE(new_text);
	GtkTreeIter iter;
	gtk_tree_model_get_iter_from_string(GTK_TREE_MODEL(store), &iter, path_string);
	if (new_text[0] == '\0')
		gtk_list_store_set(store, &iter, COL_TRACKTITLE, STR_UNKNOWN, -1);
	else
		gtk_list_store_set(store, &iter, COL_TRACKTITLE, new_text, -1);
}

static gboolean on_ripcol_clicked(GtkWidget * treeView, GdkEventButton * event, gpointer user_data)
{
	if (!GTK_WIDGET_SENSITIVE(LKP_MAIN("rip_button"))) {
		return FALSE;
	}
	GtkTreeModel *model = gtk_tree_view_get_model(GTK_TREE_VIEW(LKP_MAIN(WDG_TRACKLIST)));
	gtk_tree_model_foreach(model, for_each_row_deselect, NULL);
	return TRUE;
}

static gboolean on_tracklist_mouse_click(GtkWidget * treeView, GdkEventButton * event, gpointer user_data)
{
	if (event->type != GDK_BUTTON_PRESS || event->button != 3 || !GTK_WIDGET_SENSITIVE(LKP_MAIN("rip_button"))) {
		return FALSE;
	}
	GtkWidget *menu = gtk_menu_new();
	GtkWidget *menuItem = gtk_menu_item_new_with_label(_("Select all for ripping"));
	g_signal_connect(menuItem, "activate", G_CALLBACK(on_select_all_click), NULL);
	gtk_menu_shell_append(GTK_MENU_SHELL(menu), menuItem);
	gtk_widget_show_all(menu);
	menuItem = gtk_menu_item_new_with_label(_("Deselect all for ripping"));
	g_signal_connect(menuItem, "activate", G_CALLBACK(on_deselect_all_click), NULL);
	gtk_menu_shell_append(GTK_MENU_SHELL(menu), menuItem);
	gtk_widget_show_all(menu);
	gtk_menu_popup(GTK_MENU(menu), NULL, NULL, NULL, NULL, event->button, gdk_event_get_time((GdkEvent *) event));
	/* no need for signal to propagate */
	return TRUE;
}

static void on_window_close(GtkWidget * widget, GdkEventFocus * event, gpointer user_data)
{
	gtk_window_get_size(GTK_WINDOW(win_main), &global_prefs->main_window_width, &global_prefs->main_window_height);
	save_prefs(global_prefs);
	gtk_main_quit();
}
static gchar* get_config_path(const gchar *file_suffix);

static void read_completion_file(GtkListStore * list, const char *name)
{
	gchar *file = get_config_path(name);
	log_gen(__func__, LOG_INFO, "Reading completion data for %s in %s", name, file);
	FILE *data = fopen(file, "r");
	if (data == NULL) {
		g_free(file);
		return;
	}
	char buf[1024];
	int i;
	for (i = 0; i < COMPLETION_MAX; i++) {
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

static void create_completion(GtkWidget * entry, const char *name)
{
	log_gen(__func__, LOG_INFO, "create_completion for %s", name);
	GtkListStore *list = gtk_list_store_new(1, G_TYPE_STRING);
	GtkEntryCompletion *compl = gtk_entry_completion_new();
	gtk_entry_completion_set_model(compl, GTK_TREE_MODEL(list));
	gtk_entry_completion_set_inline_completion(compl, FALSE);
	gtk_entry_completion_set_popup_completion(compl, TRUE);
	gtk_entry_completion_set_popup_set_width(compl, TRUE);
	gtk_entry_completion_set_text_column(compl, 0);
	gtk_entry_set_completion(GTK_ENTRY(entry), compl);
	gchar *str = g_strdup(name);
	g_object_set_data(G_OBJECT(entry), COMPLETION_NAME_KEY, str);
	read_completion_file(list, name);
}

static gboolean save_history_cb(GtkTreeModel * model, GtkTreePath * path, GtkTreeIter * iter, gpointer data)
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

static GtkTreeModel *get_tree_model(GtkWidget * entry)
{
	GtkEntryCompletion *compl = gtk_entry_get_completion(GTK_ENTRY(entry));
	if (compl == NULL) return NULL;
	GtkTreeModel *model = gtk_entry_completion_get_model(compl);
	return model;
}

static void save_completion(GtkWidget * entry)
{
	GtkTreeModel *model = get_tree_model(entry);
	if (model == NULL) return;

	const gchar *name = g_object_get_data(G_OBJECT(entry), COMPLETION_NAME_KEY);
	if (name == NULL) return;

	gchar *file = get_config_path(name);
	log_gen(__func__, LOG_INFO, "Saving completion data for %s in %s", name, file);
	FILE *fd = fopen(file, "w");
	if (fd) {
		gtk_tree_model_foreach(model, save_history_cb, fd);
		fclose(fd);
	}
	g_free(file);
}

static void add_completion(GtkWidget * entry)
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

static GtkWidget *ripping_bar( GtkWidget *table, char *name, int y);
static void on_cancel_clicked(GtkButton * button, gpointer user_data);

static GtkWidget *create_main(void)
{
	GtkWidget *main_win = gtk_window_new(GTK_WINDOW_TOPLEVEL);
	gtk_window_set_title(GTK_WINDOW(main_win), PROGRAM_NAME " v." VERSION);
	gtk_window_set_default_size(GTK_WINDOW(main_win), global_prefs->main_window_width, global_prefs->main_window_height);
	GdkPixbuf *main_icon_pixbuf = gtk_icon_theme_load_icon ( gtk_icon_theme_get_default(), GTK_STOCK_CDROM, 32, 0, NULL);
	if (main_icon_pixbuf) {
		gtk_window_set_icon(GTK_WINDOW(main_win), main_icon_pixbuf);
		g_object_unref(main_icon_pixbuf);
	}
	GtkWidget *vbox1 = gtk_vbox_new(FALSE, 0);
	gtk_widget_show(vbox1);
	gtk_container_add(GTK_CONTAINER(main_win), vbox1);

	GtkWidget *toolbar = gtk_toolbar_new();
	gtk_widget_show(toolbar);
	gtk_box_pack_start(GTK_BOX(vbox1), toolbar, FALSE, FALSE, 0);
	gtk_toolbar_set_style(GTK_TOOLBAR(toolbar), GTK_TOOLBAR_BOTH_HORIZ);

	GtkWidget *icon = gtk_image_new_from_stock(GTK_STOCK_REFRESH, gtk_toolbar_get_icon_size(GTK_TOOLBAR(toolbar)));
	gtk_widget_show(icon);
	GtkWidget *lookup = (GtkWidget *) gtk_tool_button_new(icon, _("CDDB Lookup"));
	GtkTooltips *tooltips = gtk_tooltips_new();
	gtk_tooltips_set_tip(tooltips, lookup, _("Look up into the CDDB for information about this audio disc"), NULL);
	gtk_widget_show(lookup);
	gtk_container_add(GTK_CONTAINER(toolbar), lookup);
	gtk_tool_item_set_is_important(GTK_TOOL_ITEM(lookup), TRUE);

	GtkWidget *preferences = (GtkWidget *) gtk_tool_button_new_from_stock("gtk-preferences");
	gtk_widget_show(preferences);
	gtk_container_add(GTK_CONTAINER(toolbar), preferences);
	gtk_tool_item_set_is_important(GTK_TOOL_ITEM(preferences), TRUE);

	GtkWidget *separator1 = (GtkWidget *) gtk_separator_tool_item_new();
	gtk_widget_show(separator1);
	gtk_container_add(GTK_CONTAINER(toolbar), separator1);

	GtkWidget *rip_icon = gtk_image_new_from_stock("gtk-cdrom", GTK_ICON_SIZE_BUTTON);
	gtk_widget_show(rip_icon);
	GtkWidget *rip_button = (GtkWidget *) gtk_tool_button_new(rip_icon, _("Rip"));
	gtk_widget_show(rip_button);
	gtk_container_add(GTK_CONTAINER(toolbar), (GtkWidget *) rip_button);
	gtk_tool_item_set_is_important(GTK_TOOL_ITEM(rip_button), TRUE);
	// disable the "rip" button
	// it will be enabled when check_disc() finds a disc in the drive
	gtk_widget_set_sensitive(rip_button, FALSE);

	GtkWidget *separator2 = (GtkWidget *) gtk_separator_tool_item_new();
	gtk_tool_item_set_expand (GTK_TOOL_ITEM(separator2), TRUE);
	gtk_separator_tool_item_set_draw((GtkSeparatorToolItem *)separator2, FALSE);
	gtk_widget_show(separator2);
	gtk_container_add(GTK_CONTAINER(toolbar), separator2);

	GtkWidget *about = (GtkWidget *) gtk_tool_button_new_from_stock("gtk-about");
	gtk_widget_show(about);
	gtk_container_add(GTK_CONTAINER(toolbar), about);
	gtk_tool_item_set_is_important(GTK_TOOL_ITEM(about), TRUE);

	GtkWidget *table2 = gtk_table_new(3, 3, FALSE);
	gtk_widget_show(table2);
	gtk_box_pack_start(GTK_BOX(vbox1), table2, FALSE, TRUE, 3);

	GtkWidget *album_artist = gtk_entry_new();
	create_completion(album_artist, WDG_ALBUM_ARTIST);
	gtk_widget_show(album_artist);
	gtk_table_attach(GTK_TABLE(table2), album_artist, 1, 2, 1, 2, (GtkAttachOptions) (GTK_EXPAND | GTK_FILL), (GtkAttachOptions) (0), 0, 0);

	GtkWidget *album_title = gtk_entry_new();
	gtk_widget_show(album_title);
	create_completion(album_title, WDG_ALBUM_TITLE);
	gtk_table_attach(GTK_TABLE(table2), album_title, 1, 2, 2, 3, (GtkAttachOptions) (GTK_EXPAND | GTK_FILL), (GtkAttachOptions) (0), 0, 0);

	GtkWidget *pick_disc = gtk_combo_box_new();
	gtk_table_attach(GTK_TABLE(table2), pick_disc, 1, 2, 0, 1, (GtkAttachOptions) (GTK_FILL), (GtkAttachOptions) (GTK_FILL), 0, 0);


	GtkWidget *album_genre = gtk_entry_new();
	create_completion(album_genre, WDG_ALBUM_GENRE);
	gtk_widget_show(album_genre);
	gtk_table_attach(GTK_TABLE(table2), album_genre, 1, 2, 3, 4, (GtkAttachOptions) (GTK_EXPAND | GTK_FILL), (GtkAttachOptions) (0), 0, 0);

	GtkWidget *disc = gtk_label_new(_("Disc:"));
	gtk_table_attach(GTK_TABLE(table2), disc, 0, 1, 0, 1, (GtkAttachOptions) (GTK_FILL), (GtkAttachOptions) (0), 3, 0);
	gtk_misc_set_alignment(GTK_MISC(disc), 0, 0.49);

	GtkWidget *artist_label = gtk_label_new(_("Album Artist:"));
	gtk_misc_set_alignment(GTK_MISC(artist_label), 0, 0);
	gtk_widget_show(artist_label);
	gtk_table_attach(GTK_TABLE(table2), artist_label, 0, 1, 1, 2, (GtkAttachOptions) (GTK_FILL), (GtkAttachOptions) (0), 3, 0);

	GtkWidget *title_label = gtk_label_new(_("Album Title:"));
	gtk_misc_set_alignment(GTK_MISC(title_label), 0, 0);
	gtk_widget_show(title_label);
	gtk_table_attach(GTK_TABLE(table2), title_label, 0, 1, 2, 3, (GtkAttachOptions) (GTK_FILL), (GtkAttachOptions) (0), 3, 0);

	GtkWidget *single_artist = gtk_check_button_new_with_mnemonic(_("Single Artist"));
	gtk_widget_show(single_artist);
	gtk_table_attach(GTK_TABLE(table2), single_artist, 2, 3, 1, 2, (GtkAttachOptions) (GTK_FILL), (GtkAttachOptions) (0), 3, 0);

	GtkWidget *genre_label = gtk_label_new(_("Genre / Year:"));
	gtk_misc_set_alignment(GTK_MISC(genre_label), 0, 0);
	gtk_widget_show(genre_label);
	gtk_table_attach(GTK_TABLE(table2), genre_label, 0, 1, 3, 4, (GtkAttachOptions) (GTK_FILL), (GtkAttachOptions) (0), 3, 0);

	GtkWidget *single_genre = gtk_check_button_new_with_mnemonic(_("Single Genre"));
	//~ gtk_widget_show( single_genre );
	//~ gtk_table_attach( GTK_TABLE( table2 ), single_genre, 2, 3, 3, 4,
	//~ (GtkAttachOptions) ( GTK_FILL ),
	//~ (GtkAttachOptions) (0), 3, 0);

	GtkWidget *album_year = gtk_entry_new();
	gtk_widget_show(album_year);
	gtk_table_attach(GTK_TABLE(table2), album_year, 2, 3, 3, 4, (GtkAttachOptions) (GTK_FILL), (GtkAttachOptions) (0), 3, 0);

	GtkWidget *scroll = gtk_scrolled_window_new(NULL, NULL);
	gtk_widget_show(scroll);
	gtk_box_pack_start(GTK_BOX(vbox1), scroll, TRUE, TRUE, 0);
	gtk_scrolled_window_set_policy(GTK_SCROLLED_WINDOW(scroll), GTK_POLICY_AUTOMATIC, GTK_POLICY_AUTOMATIC);

	GtkWidget *tracklist = gtk_tree_view_new();
	gtk_widget_show(tracklist);
	gtk_container_add(GTK_CONTAINER(scroll), tracklist);
	gtk_tree_view_set_rules_hint(GTK_TREE_VIEW(tracklist), TRUE);
	gtk_tree_view_set_enable_search(GTK_TREE_VIEW(tracklist), FALSE);

	GtkWidget *rip_box = gtk_vbox_new(FALSE, 0);
	gtk_widget_show(rip_box);
	gtk_container_set_border_width(GTK_CONTAINER(rip_box), 10);
	GtkWidget *table = gtk_table_new(3, 2, FALSE);
	gtk_widget_show(table);
	gtk_box_pack_start(GTK_BOX(rip_box), table, TRUE, TRUE, 10);
	GtkWidget *progress_total = ripping_bar( table,"Total progress", 0);
	GtkWidget *progress_rip = ripping_bar( table,"Ripping", 1);
	GtkWidget *progress_encode = ripping_bar( table,"Encoding", 2);
	GtkWidget *cancel = gtk_button_new_from_stock("gtk-cancel");
	gtk_widget_show(cancel);
	GTK_WIDGET_SET_FLAGS(cancel, GTK_CAN_DEFAULT);
	gtk_box_pack_start(GTK_BOX(rip_box), cancel, FALSE, FALSE, 24);
	g_signal_connect((gpointer) cancel, "clicked", G_CALLBACK(on_cancel_clicked), NULL);

	GtkWidget *win_ripping = gtk_frame_new(NULL);
	gtk_container_set_border_width(GTK_CONTAINER(win_ripping), 20);
	gtk_frame_set_label(GTK_FRAME(win_ripping), "Progress");
	gtk_container_add(GTK_CONTAINER(win_ripping), rip_box);
	gtk_box_pack_start(GTK_BOX(vbox1), win_ripping, TRUE, FALSE, 10);

	// TRackList
	// set up all the columns for the track listing widget
	GtkCellRenderer *cell = NULL;
	cell = gtk_cell_renderer_toggle_new();
	g_object_set(cell, "activatable", TRUE, NULL);
	g_signal_connect(cell, "toggled", (GCallback) on_rip_toggled, NULL);
	gtk_tree_view_insert_column_with_attributes(GTK_TREE_VIEW(tracklist), -1, _("Rip"), cell, "active", COL_RIPTRACK, NULL);
	GtkTreeViewColumn *col = gtk_tree_view_get_column(GTK_TREE_VIEW(tracklist), COL_RIPTRACK);
	gtk_tree_view_column_set_clickable(col, TRUE);

	cell = gtk_cell_renderer_text_new();
	gtk_tree_view_insert_column_with_attributes(GTK_TREE_VIEW(tracklist), -1, _("Track"), cell, STR_TEXT, COL_TRACKNUM, NULL);

	cell = gtk_cell_renderer_text_new();
	g_object_set(cell, "editable", TRUE, NULL);
	g_signal_connect(cell, "edited", (GCallback) on_artist_edited, NULL);
	gtk_tree_view_insert_column_with_attributes(GTK_TREE_VIEW(tracklist), -1, _("Artist"), cell, STR_TEXT, COL_TRACKARTIST, NULL);

	cell = gtk_cell_renderer_text_new();
	g_object_set(cell, "editable", TRUE, NULL);
	g_signal_connect(cell, "edited", (GCallback) on_title_edited, NULL);
	gtk_tree_view_insert_column_with_attributes(GTK_TREE_VIEW(tracklist), -1, _("Title"), cell, STR_TEXT, COL_TRACKTITLE, NULL);

	cell = gtk_cell_renderer_text_new();
	g_object_set(cell, "editable", TRUE, NULL);
	g_signal_connect(cell, "edited", (GCallback) on_genre_edited, NULL);
	gtk_tree_view_insert_column_with_attributes(GTK_TREE_VIEW(tracklist), -1, _("Genre"), cell, STR_TEXT, COL_GENRE, NULL);

	cell = gtk_cell_renderer_text_new();
	gtk_tree_view_insert_column_with_attributes(GTK_TREE_VIEW(tracklist), -1, _("Time"), cell, STR_TEXT, COL_TRACKTIME, NULL);

	// set up the columns for the album selecting dropdown box
	cell = gtk_cell_renderer_text_new();
	gtk_cell_layout_pack_start(GTK_CELL_LAYOUT(pick_disc), cell, TRUE);
	gtk_cell_layout_set_attributes(GTK_CELL_LAYOUT(pick_disc), cell, STR_TEXT, 0, NULL);
	cell = gtk_cell_renderer_text_new();
	gtk_cell_layout_pack_start(GTK_CELL_LAYOUT(pick_disc), cell, TRUE);
	gtk_cell_layout_set_attributes(GTK_CELL_LAYOUT(pick_disc), cell, STR_TEXT, 1, NULL);

	// Bottom HBOX
	GtkWidget *hbox5 = gtk_hbox_new(FALSE, 5);
	gtk_box_pack_start(GTK_BOX(vbox1), hbox5, FALSE, TRUE, 5);
	gtk_widget_show(hbox5);

	GtkWidget *statusLbl = gtk_label_new("Welcome to " PROGRAM_NAME);
	gtk_label_set_use_markup(GTK_LABEL(statusLbl), TRUE);
	gtk_misc_set_alignment(GTK_MISC(statusLbl), 0, 0.5);
	gtk_box_pack_start(GTK_BOX(hbox5), statusLbl, TRUE, TRUE, 0);
	gtk_widget_show(statusLbl);

	GtkWidget *fillerBox = gtk_hbox_new(FALSE, 0);
	gtk_box_pack_start(GTK_BOX(hbox5), fillerBox, TRUE, TRUE, 0);
	gtk_widget_show(hbox5);

	g_signal_connect((gpointer) main_win, "delete_event", G_CALLBACK(on_window_close), NULL);
	g_signal_connect((gpointer) tracklist, "button-press-event", G_CALLBACK(on_tracklist_mouse_click), NULL);
	g_signal_connect((gpointer) col, "clicked", G_CALLBACK(on_ripcol_clicked), NULL);
	g_signal_connect((gpointer) lookup, "clicked", G_CALLBACK(on_lookup_clicked), NULL);
	g_signal_connect((gpointer) preferences, "clicked", G_CALLBACK(on_preferences_clicked), NULL);
	g_signal_connect((gpointer) about, "clicked", G_CALLBACK(on_about_clicked), NULL);
	g_signal_connect((gpointer) album_artist, "focus_out_event", G_CALLBACK(on_album_artist_focus_out_event), NULL);
	g_signal_connect((gpointer) album_title, "focus_out_event", G_CALLBACK(on_album_title_focus_out_event), NULL);
	g_signal_connect((gpointer) pick_disc, "changed", G_CALLBACK(on_pick_disc_changed), NULL);
	g_signal_connect((gpointer) single_artist, "toggled", G_CALLBACK(on_single_artist_toggled), NULL);
	g_signal_connect((gpointer) rip_button, "clicked", G_CALLBACK(on_rip_button_clicked), NULL);
	g_signal_connect((gpointer) album_genre, "focus_out_event", G_CALLBACK(on_album_genre_focus_out_event), NULL);
	g_signal_connect((gpointer) single_genre, "toggled", G_CALLBACK(on_single_genre_toggled), NULL);
	g_signal_connect((gpointer) album_year, "focus_out_event", G_CALLBACK(on_year_focus_out_event), NULL);

	/* KEYBOARD accelerators */
	GtkAccelGroup *accelGroup;
	guint accelKey;
	GdkModifierType accelModifier;
	GClosure *closure = NULL;

	accelGroup = gtk_accel_group_new();
	gtk_window_add_accel_group(GTK_WINDOW(main_win), accelGroup);

	gtk_accelerator_parse("<Control>W", &accelKey, &accelModifier);
	closure = g_cclosure_new(G_CALLBACK(on_window_close), NULL, NULL);
	gtk_accel_group_connect(accelGroup, accelKey, accelModifier, GTK_ACCEL_VISIBLE, closure);

	gtk_accelerator_parse("<Control>Q", &accelKey, &accelModifier);
	closure = g_cclosure_new(G_CALLBACK(on_window_close), NULL, NULL);
	gtk_accel_group_connect(accelGroup, accelKey, accelModifier, GTK_ACCEL_VISIBLE, closure);

	gtk_accelerator_parse("F2", &accelKey, &accelModifier);
	closure = g_cclosure_new(G_CALLBACK(on_press_f2), NULL, NULL);
	gtk_accel_group_connect(accelGroup, accelKey, accelModifier, GTK_ACCEL_VISIBLE, closure);
	/* END KEYBOARD accelerators */

	/* Store pointers to all widgets, for use by lookup_widget(). */
	HOOKUP(main_win, about, "about");
	HOOKUP(main_win, album_artist, WDG_ALBUM_ARTIST);
	HOOKUP(main_win, album_genre, WDG_ALBUM_GENRE);
	HOOKUP(main_win, album_title, WDG_ALBUM_TITLE);
	HOOKUP(main_win, album_year, WDG_ALBUM_YEAR);
	HOOKUP(main_win, artist_label, "artist_label");
	HOOKUP(main_win, disc, "disc");
	HOOKUP(main_win, genre_label, "genre_label");
	HOOKUP(main_win, lookup, "lookup");
	HOOKUP(main_win, pick_disc, WDG_PICK_DISC);
	HOOKUP(main_win, preferences, "preferences");
	HOOKUP(main_win, rip_button, "rip_button");
	HOOKUP(main_win, single_artist, "single_artist");
	HOOKUP(main_win, single_genre, "single_genre");
	HOOKUP(main_win, statusLbl, "statusLbl");
	HOOKUP(main_win, title_label, "title_label");
	HOOKUP(main_win, tracklist, WDG_TRACKLIST);
	HOOKUP(main_win, scroll, "scroll");
	HOOKUP(main_win, progress_total, "progress_total");
	HOOKUP(main_win, progress_rip, "progress_rip");
	HOOKUP(main_win, progress_encode, "progress_encode");
	HOOKUP(main_win, cancel, "cancel");
	HOOKUP(main_win, win_ripping, "win_ripping");
	HOOKUP_OBJ_NOREF(main_win, main_win, "main");

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
	gtk_widget_show(notebook1);
	gtk_box_pack_start(GTK_BOX(vbox), notebook1, TRUE, TRUE, 0);

	/* GENERAL tab */
	vbox = gtk_vbox_new(FALSE, 5);
	gtk_container_set_border_width(GTK_CONTAINER(vbox), 5);
	gtk_widget_show(vbox);
	gtk_container_add(GTK_CONTAINER(notebook1), vbox);

	GtkWidget *label = gtk_label_new(_("Destination folder"));
	gtk_misc_set_alignment(GTK_MISC(label), 0, 0);
	gtk_widget_show(label);
	gtk_box_pack_start(GTK_BOX(vbox), label, FALSE, FALSE, 0);
	gtk_label_set_use_markup(GTK_LABEL(label), TRUE);

	GtkWidget *music_dir = gtk_file_chooser_button_new(_("Destination folder"), GTK_FILE_CHOOSER_ACTION_SELECT_FOLDER);
	gtk_widget_show(music_dir);
	gtk_box_pack_start(GTK_BOX(vbox), music_dir, FALSE, FALSE, 0);

	GtkWidget *make_playlist = gtk_check_button_new_with_mnemonic(_("Create M3U playlist"));
	gtk_widget_show(make_playlist);
	gtk_box_pack_start(GTK_BOX(vbox), make_playlist, FALSE, FALSE, 0);

	GtkWidget *hbox12 = gtk_hbox_new(FALSE, 0);
	gtk_widget_show(hbox12);
	gtk_box_pack_start(GTK_BOX(vbox), hbox12, FALSE, FALSE, 0);

	label = gtk_label_new(_("CD-ROM device: "));
	gtk_widget_show(label);
	gtk_box_pack_start(GTK_BOX(hbox12), label, FALSE, FALSE, 0);

	GtkWidget *cdrom = gtk_entry_new();
	gtk_widget_show(cdrom);
	gtk_box_pack_start(GTK_BOX(hbox12), cdrom, TRUE, TRUE, 0);

	GtkTooltips *tooltips = gtk_tooltips_new();
	gtk_tooltips_set_tip(tooltips, cdrom, _("Default: /dev/cdrom\n" "Other example: /dev/hdc\n" "Other example: /dev/sr0"), NULL);

	GtkWidget *eject_on_done = gtk_check_button_new_with_mnemonic(_("Eject disc when finished"));
	gtk_widget_show(eject_on_done);
	gtk_box_pack_start(GTK_BOX(vbox), eject_on_done, FALSE, FALSE, 5);

	GtkWidget *hboxFill = gtk_hbox_new(FALSE, 0);
	gtk_widget_show(hboxFill);
	gtk_box_pack_start(GTK_BOX(vbox), hboxFill, TRUE, TRUE, 0);

	label = gtk_label_new(_("General"));
	gtk_widget_show(label);
	gtk_notebook_set_tab_label(GTK_NOTEBOOK(notebook1), gtk_notebook_get_nth_page(GTK_NOTEBOOK(notebook1), 0), label);
	/* END GENERAL tab */

	/* FILENAMES tab */
	vbox = gtk_vbox_new(FALSE, 5);
	gtk_container_set_border_width(GTK_CONTAINER(vbox), 5);
	gtk_widget_show(vbox);
	gtk_container_add(GTK_CONTAINER(notebook1), vbox);

	GtkWidget *frame2 = gtk_frame_new(NULL);
	gtk_widget_show(frame2);
	gtk_box_pack_start(GTK_BOX(vbox), frame2, FALSE, FALSE, 0);

	vbox = gtk_vbox_new(FALSE, 0);
	gtk_container_set_border_width(GTK_CONTAINER(vbox), 5);
	gtk_widget_show(vbox);
	gtk_container_add(GTK_CONTAINER(frame2), vbox);

	label = gtk_label_new(_ ("%A - Artist\n%L - Album\n%N - Track number (2-digit)\n%Y - Year (4-digit or \"0\")\n%T - Song title"));
	gtk_widget_show(label);
	gtk_box_pack_start(GTK_BOX(vbox), label, FALSE, FALSE, 0);
	gtk_misc_set_alignment(GTK_MISC(label), 0, 0);

	label = gtk_label_new(_("%G - Genre"));
	gtk_widget_show(label);
	gtk_box_pack_start(GTK_BOX(vbox), label, FALSE, FALSE, 0);
	gtk_misc_set_alignment(GTK_MISC(label), 0, 0);

	// problem is that the same albumdir is used (threads.c) for all formats
	//~ label = gtk_label_new (_("%F - Format (e.g. FLAC)"));
	//~ gtk_widget_show (label);
	//~ gtk_box_pack_start (GTK_BOX (vbox), label, FALSE, FALSE, 0);
	//~ gtk_misc_set_alignment (GTK_MISC (label), 0, 0);

	GtkWidget *table1 = gtk_table_new(3, 2, FALSE);
	gtk_widget_show(table1);
	gtk_box_pack_start(GTK_BOX(vbox), table1, TRUE, TRUE, 0);

	label = gtk_label_new(_("Album directory: "));
	gtk_widget_show(label);
	gtk_table_attach(GTK_TABLE(table1), label, 0, 1, 0, 1, (GtkAttachOptions) (GTK_FILL), (GtkAttachOptions) (0), 0, 0);
	gtk_misc_set_alignment(GTK_MISC(label), 0, 0);

	label = gtk_label_new(_("Playlist file: "));
	gtk_widget_show(label);
	gtk_table_attach(GTK_TABLE(table1), label, 0, 1, 1, 2, (GtkAttachOptions) (GTK_FILL), (GtkAttachOptions) (0), 0, 0);
	gtk_misc_set_alignment(GTK_MISC(label), 0, 0);

	label = gtk_label_new(_("Music file: "));
	gtk_widget_show(label);
	gtk_table_attach(GTK_TABLE(table1), label, 0, 1, 2, 3, (GtkAttachOptions) (GTK_FILL), (GtkAttachOptions) (0), 0, 0);
	gtk_misc_set_alignment(GTK_MISC(label), 0, 0);

	GtkWidget *format_albumdir = gtk_entry_new();
	gtk_widget_show(format_albumdir);
	gtk_table_attach(GTK_TABLE(table1), format_albumdir, 1, 2, 0, 1, (GtkAttachOptions) (GTK_EXPAND | GTK_FILL), (GtkAttachOptions) (0), 0, 0);

	tooltips = gtk_tooltips_new();
	gtk_tooltips_set_tip(tooltips, format_albumdir, _("This is relative to the destination folder (from the General tab).\n" "Can be blank.\n" "Default: %A - %L\n" "Other example: %A/%L"), NULL);

	GtkWidget *format_playlist = gtk_entry_new();
	gtk_widget_show(format_playlist);
	gtk_table_attach(GTK_TABLE(table1), format_playlist, 1, 2, 1, 2, (GtkAttachOptions) (GTK_EXPAND | GTK_FILL), (GtkAttachOptions) (0), 0, 0);
	tooltips = gtk_tooltips_new();
	gtk_tooltips_set_tip(tooltips, format_playlist, _("This will be stored in the album directory.\n" "Can be blank.\n" "Default: %A - %L"), NULL);

	GtkWidget *format_music = gtk_entry_new();
	gtk_widget_show(format_music);
	gtk_table_attach(GTK_TABLE(table1), format_music, 1, 2, 2, 3, (GtkAttachOptions) (GTK_EXPAND | GTK_FILL), (GtkAttachOptions) (0), 0, 0);
	tooltips = gtk_tooltips_new();
	gtk_tooltips_set_tip(tooltips, format_music, _("This will be stored in the album directory.\n" "Cannot be blank.\n" "Default: %A - %T\n" "Other example: %N - %T"), NULL);

	label = gtk_label_new(_("Filename formats"));
	gtk_widget_show(label);
	gtk_frame_set_label_widget(GTK_FRAME(frame2), label);
	gtk_label_set_use_markup(GTK_LABEL(label), TRUE);

	label = gtk_label_new(_("Filenames"));
	gtk_widget_show(label);
	gtk_notebook_set_tab_label(GTK_NOTEBOOK(notebook1), gtk_notebook_get_nth_page(GTK_NOTEBOOK(notebook1), 1), label);
	/* END FILENAMES tab */

	/* ENCODE tab */
	vbox = gtk_vbox_new(FALSE, 5);
	gtk_container_set_border_width(GTK_CONTAINER(vbox), 5);
	gtk_widget_show(vbox);
	gtk_container_add(GTK_CONTAINER(notebook1), vbox);

	/* WAV */
	GtkWidget *frame0 = gtk_frame_new(NULL);
	gtk_widget_show(frame0);
	gtk_box_pack_start(GTK_BOX(vbox), frame0, FALSE, FALSE, 0);
	GtkWidget *alignment1 = gtk_alignment_new(0.5, 0.5, 1, 1);
	gtk_widget_show(alignment1);
	gtk_container_add(GTK_CONTAINER(frame0), alignment1);
	gtk_alignment_set_padding(GTK_ALIGNMENT(alignment1), 1, 1, 8, 8);
	GtkWidget *vbox0 = gtk_vbox_new(FALSE, 0);
	gtk_widget_show(vbox0);
	gtk_container_add(GTK_CONTAINER(alignment1), vbox0);

	GtkWidget *rip_wav = gtk_check_button_new_with_mnemonic(_("WAV (uncompressed)"));
	gtk_widget_show(rip_wav);
	gtk_frame_set_label_widget(GTK_FRAME(frame0), rip_wav);

	label = gtk_label_new(_("WAV files retain maximum sound quality, but they are very big."
							" You should keep WAV files if you intend to create Audio discs."
							" WAV files can be converted back to audio tracks with CD burning software."));
	gtk_label_set_line_wrap(GTK_LABEL(label), TRUE);
	gtk_widget_show(label);
	gtk_box_pack_start(GTK_BOX(vbox0), label, FALSE, FALSE, 0);

	/* END WAV */

	/* MP3 */
	GtkWidget *frame3 = gtk_frame_new(NULL);
	gtk_widget_show(frame3);
	gtk_box_pack_start(GTK_BOX(vbox), frame3, TRUE, FALSE, 0);

	GtkWidget *alignment8 = gtk_alignment_new(0.5, 0.5, 1, 1);
	gtk_widget_show(alignment8);
	gtk_container_add(GTK_CONTAINER(frame3), alignment8);
	gtk_alignment_set_padding(GTK_ALIGNMENT(alignment8), 1, 1, 8, 8);

	GtkWidget *vbox2 = gtk_vbox_new(FALSE, 8);
	gtk_widget_show(vbox2);
	gtk_container_add(GTK_CONTAINER(alignment8), vbox2);

	GtkWidget *mp3_vbr = gtk_check_button_new_with_mnemonic(_("Variable bit rate (VBR)"));
	gtk_widget_show(mp3_vbr);
	gtk_box_pack_start(GTK_BOX(vbox2), mp3_vbr, FALSE, FALSE, 0);
	g_signal_connect((gpointer) mp3_vbr, "toggled", G_CALLBACK(on_vbr_toggled), NULL);

	tooltips = gtk_tooltips_new();
	gtk_tooltips_set_tip(tooltips, mp3_vbr, _("Better quality for the same size."), NULL);

	GtkWidget *combo_mp3_quality = gtk_combo_box_new_text();
	gtk_widget_show(combo_mp3_quality);
	tooltips = gtk_tooltips_new();
	gtk_tooltips_set_tip(tooltips, combo_mp3_quality, _("Choosing 'High quality' is recommended."), NULL);
	gtk_combo_box_append_text(GTK_COMBO_BOX (combo_mp3_quality), "Low quality");
	gtk_combo_box_append_text(GTK_COMBO_BOX (combo_mp3_quality), "Good quality");
	gtk_combo_box_append_text(GTK_COMBO_BOX (combo_mp3_quality), "High quality");
	gtk_combo_box_append_text(GTK_COMBO_BOX (combo_mp3_quality), "Maximum quality");
	gtk_combo_box_set_active (GTK_COMBO_BOX (combo_mp3_quality), 2);
	gtk_box_pack_start(GTK_BOX(vbox2), combo_mp3_quality, FALSE, FALSE, 0);
	g_signal_connect((gpointer) combo_mp3_quality, "changed", G_CALLBACK(on_mp3_quality_changed), NULL);

	GtkWidget *hbox9 = gtk_hbox_new(FALSE, 0);
	gtk_widget_show(hbox9);
	gtk_box_pack_start(GTK_BOX(vbox2), hbox9, FALSE, FALSE, 0);

	label = gtk_label_new(_("Bitrate : "));
	gtk_widget_show(label);
	gtk_box_pack_start(GTK_BOX(hbox9), label, FALSE, FALSE, 0);
	HOOKUP(prefs, label, "bitrate_lbl");

	GtkWidget *mp3bitrate = gtk_hscale_new(GTK_ADJUSTMENT(gtk_adjustment_new(0, 0, 14, 1, 1, 1)));
	//gtk_widget_show(mp3bitrate);
	gtk_box_pack_start(GTK_BOX(hbox9), mp3bitrate, FALSE, FALSE, 5);
	gtk_scale_set_draw_value(GTK_SCALE(mp3bitrate), FALSE);
	gtk_scale_set_digits(GTK_SCALE(mp3bitrate), 0);
	g_signal_connect((gpointer) mp3bitrate, "value_changed", G_CALLBACK(on_mp3bitrate_value_changed), NULL);
	tooltips = gtk_tooltips_new();
	gtk_tooltips_set_tip(tooltips, mp3bitrate, _("Higher bitrate is better quality but also bigger file. Most people use 192Kbps."), NULL);


	char kbps_text[10];
	snprintf(kbps_text, 10, _("%dKbps"), 32);
	label = gtk_label_new(kbps_text);
	gtk_widget_show(label);
	gtk_box_pack_start(GTK_BOX(hbox9), label, FALSE, FALSE, 0);
	HOOKUP(prefs, label, "bitrate_lbl_2");

	GtkWidget *rip_mp3 = gtk_check_button_new_with_mnemonic(_("MP3 (lossy compression)"));
	gtk_widget_show(rip_mp3);
	gtk_frame_set_label_widget(GTK_FRAME(frame3), rip_mp3);
	g_signal_connect((gpointer) rip_mp3, "toggled", G_CALLBACK(on_rip_mp3_toggled), NULL);
	/* END MP3 */

	label = gtk_label_new(_("Encode"));
	gtk_widget_show(label);
	gtk_notebook_set_tab_label(GTK_NOTEBOOK(notebook1), gtk_notebook_get_nth_page(GTK_NOTEBOOK(notebook1), 2), label);
	/* END ENCODE tab */

	/* ADVANCED tab */
	vbox = gtk_vbox_new(FALSE, 5);
	gtk_container_set_border_width(GTK_CONTAINER(vbox), 5);
	gtk_widget_show(vbox);
	gtk_container_add(GTK_CONTAINER(notebook1), vbox);

	GtkWidget *frame = gtk_frame_new(NULL);
	gtk_frame_set_label(GTK_FRAME(frame), "CDDB");
	gtk_widget_show(frame);
	gtk_box_pack_start(GTK_BOX(vbox), frame, FALSE, FALSE, 0);

	GtkWidget *frameVbox = gtk_vbox_new(FALSE, 0);
	gtk_widget_show(frameVbox);
	gtk_container_add(GTK_CONTAINER(frame), frameVbox);

	GtkWidget *do_cddb_updates = gtk_check_button_new_with_mnemonic(_("Get disc info from the internet automatically"));
	gtk_widget_show(do_cddb_updates);
	gtk_box_pack_start(GTK_BOX(frameVbox), do_cddb_updates, FALSE, FALSE, 0);

	GtkWidget *hbox = gtk_hbox_new(FALSE, 0);
	gtk_widget_show(hbox);
	gtk_box_pack_start(GTK_BOX(frameVbox), hbox, FALSE, FALSE, 1);

	label = gtk_label_new(_("Server: "));
	gtk_widget_show(label);
	gtk_box_pack_start(GTK_BOX(hbox), label, FALSE, FALSE, 5);

	GtkWidget *cddbServerName = gtk_entry_new();
	gtk_widget_show(cddbServerName);
	gtk_box_pack_start(GTK_BOX(hbox), cddbServerName, TRUE, TRUE, 5);
	HOOKUP(prefs, cddbServerName, "cddb_server_name");

	tooltips = gtk_tooltips_new();
	gtk_tooltips_set_tip(tooltips, cddbServerName, _("The CDDB server to get disc info from (default is freedb.freedb.org)"), NULL);

	hbox = gtk_hbox_new(FALSE, 0);
	gtk_widget_show(hbox);
	gtk_box_pack_start(GTK_BOX(frameVbox), hbox, FALSE, FALSE, 1);

	label = gtk_label_new(_("Port: "));
	gtk_widget_show(label);
	gtk_box_pack_start(GTK_BOX(hbox), label, FALSE, FALSE, 5);

	GtkWidget *cddbPortNum = gtk_entry_new();
	gtk_widget_show(cddbPortNum);
	gtk_box_pack_start(GTK_BOX(hbox), cddbPortNum, TRUE, TRUE, 5);
	HOOKUP(prefs, cddbPortNum, "cddb_port_number");

	tooltips = gtk_tooltips_new();
	gtk_tooltips_set_tip(tooltips, cddbPortNum, _("The CDDB server port (default is 8880)"), NULL);

	frame = gtk_frame_new(NULL);
	gtk_widget_show(frame);
	gtk_box_pack_start(GTK_BOX(vbox), frame, FALSE, FALSE, 0);

	GtkWidget *useProxy = gtk_check_button_new_with_mnemonic(_("Use an HTTP proxy to connect to the internet"));
	gtk_widget_show(useProxy);
	gtk_frame_set_label_widget(GTK_FRAME(frame), useProxy);
	HOOKUP(prefs, useProxy, "use_proxy");

	frameVbox = gtk_vbox_new(FALSE, 0);
	gtk_widget_show(frameVbox);
	gtk_container_add(GTK_CONTAINER(frame), frameVbox);

	hbox = gtk_hbox_new(FALSE, 0);
	gtk_widget_show(hbox);
	gtk_box_pack_start(GTK_BOX(frameVbox), hbox, FALSE, FALSE, 1);

	label = gtk_label_new(_("Server: "));
	gtk_widget_show(label);
	gtk_box_pack_start(GTK_BOX(hbox), label, FALSE, FALSE, 5);

	GtkWidget *serverName = gtk_entry_new();
	gtk_widget_show(serverName);
	gtk_box_pack_start(GTK_BOX(hbox), serverName, TRUE, TRUE, 5);
	HOOKUP(prefs, serverName, "server_name");

	hbox = gtk_hbox_new(FALSE, 0);
	gtk_widget_show(hbox);
	gtk_box_pack_start(GTK_BOX(frameVbox), hbox, FALSE, FALSE, 1);

	label = gtk_label_new(_("Port: "));
	gtk_widget_show(label);
	gtk_box_pack_start(GTK_BOX(hbox), label, FALSE, FALSE, 5);

	GtkWidget *portNum = gtk_entry_new();
	gtk_widget_show(portNum);
	gtk_box_pack_start(GTK_BOX(hbox), portNum, TRUE, TRUE, 5);
	HOOKUP(prefs, portNum, "port_number");

	gchar *lbl = g_strdup_printf("Log to %s", global_prefs->log_file);
	GtkWidget *log_file = gtk_check_button_new_with_label(lbl);
	gtk_widget_show(log_file);
	gtk_box_pack_start(GTK_BOX(vbox), log_file, FALSE, FALSE, 0);
	HOOKUP(prefs, log_file, "do_log");
	g_free(lbl);

	hboxFill = gtk_hbox_new(FALSE, 0);
	gtk_widget_show(hboxFill);
	gtk_box_pack_start(GTK_BOX(vbox), hboxFill, TRUE, TRUE, 0);

	label = gtk_label_new(_("Advanced"));
	gtk_widget_show(label);
	gtk_notebook_set_tab_label(GTK_NOTEBOOK(notebook1), gtk_notebook_get_nth_page(GTK_NOTEBOOK(notebook1), 3), label);
	/* END ADVANCED tab */

	GtkWidget *dialog_action_area1 = GTK_DIALOG(prefs)->action_area;
	gtk_widget_show(dialog_action_area1);
	gtk_button_box_set_layout(GTK_BUTTON_BOX(dialog_action_area1), GTK_BUTTONBOX_END);

	GtkWidget *cancelbutton1 = gtk_button_new_from_stock("gtk-cancel");
	gtk_widget_show(cancelbutton1);
	gtk_dialog_add_action_widget(GTK_DIALOG(prefs), cancelbutton1, GTK_RESPONSE_CANCEL);
	GTK_WIDGET_SET_FLAGS(cancelbutton1, GTK_CAN_DEFAULT);

	GtkWidget *okbutton1 = gtk_button_new_from_stock("gtk-ok");
	gtk_widget_show(okbutton1);
	gtk_dialog_add_action_widget(GTK_DIALOG(prefs), okbutton1, GTK_RESPONSE_OK);
	GTK_WIDGET_SET_FLAGS(okbutton1, GTK_CAN_DEFAULT);

	g_signal_connect((gpointer) prefs, "response", G_CALLBACK(on_prefs_response), NULL);
	g_signal_connect((gpointer) prefs, "realize", G_CALLBACK(on_prefs_show), NULL);

	/* Store pointers to all widgets, for use by lookup_widget(). */
	HOOKUP_OBJ_NOREF(prefs, prefs, "prefs");
	HOOKUP(prefs, music_dir, "music_dir");
	HOOKUP(prefs, make_playlist, "make_playlist");
	HOOKUP(prefs, cdrom, "cdrom");
	HOOKUP(prefs, eject_on_done, "eject_on_done");
	HOOKUP(prefs, format_music, "format_music");
	HOOKUP(prefs, format_albumdir, "format_albumdir");
	HOOKUP(prefs, format_playlist, "format_playlist");
	HOOKUP(prefs, rip_wav, "rip_wav");
	HOOKUP(prefs, mp3_vbr, "mp3_vbr");
	HOOKUP(prefs, mp3bitrate, "mp3bitrate");
	HOOKUP(prefs, combo_mp3_quality, "combo_mp3_quality");
	HOOKUP(prefs, rip_mp3, "rip_mp3");
	HOOKUP(prefs, do_cddb_updates, "do_cddb_updates");
	return prefs;
}

static GtkWidget *ripping_bar( GtkWidget *table, char *name, int y) {
	GtkWidget *label = gtk_label_new(_(name));
	gtk_widget_show(label);
	gtk_table_attach(GTK_TABLE(table), label, 0, 1, y, y+1, (GtkAttachOptions) (GTK_FILL), (GtkAttachOptions) (0), 5, 10);
	gtk_misc_set_alignment(GTK_MISC(label), 0, 0.5);
	GtkWidget *progress = gtk_progress_bar_new();
	gtk_widget_show(progress);
	gtk_table_attach(GTK_TABLE(table), progress, 1, 2, y, y+1, (GtkAttachOptions) (GTK_EXPAND | GTK_FILL), (GtkAttachOptions) (0), 5, 10);
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
	gtk_widget_set_sensitive(LKP_MAIN("tracklist"), FALSE);
	disable_widget("lookup");
	disable_widget("preferences");
	disable_widget("disc");
	disable_widget(WDG_ALBUM_ARTIST);
	disable_widget("artist_label");
	disable_widget("title_label");
	disable_widget(WDG_ALBUM_TITLE);
	disable_widget("single_artist");
	disable_widget("rip_button");
	disable_widget(WDG_ALBUM_GENRE);
	disable_widget("genre_label");
	disable_widget("single_genre");
	disable_widget(WDG_ALBUM_YEAR);
}

static void enable_all_main_widgets(void)
{
	gtk_widget_set_sensitive(LKP_MAIN("tracklist"), TRUE);
	enable_widget("lookup");
	enable_widget("preferences");
	enable_widget("about");
	enable_widget("disc");
	enable_widget(WDG_ALBUM_ARTIST);
	enable_widget("artist_label");
	enable_widget("title_label");
	enable_widget(WDG_ALBUM_TITLE);
	enable_widget("single_artist");
	enable_widget("rip_button");
	enable_widget(WDG_ALBUM_GENRE);
	enable_widget("genre_label");
	enable_widget("single_genre");
	enable_widget(WDG_ALBUM_YEAR);
}

static void disable_mp3_widgets(void)
{
	gtk_widget_set_sensitive(LKP_PREF("mp3_vbr"), FALSE);
	gtk_widget_set_sensitive(LKP_PREF("bitrate_lbl"), FALSE);
	gtk_widget_set_sensitive(LKP_PREF("mp3bitrate"), FALSE);
	gtk_widget_set_sensitive(LKP_PREF("combo_mp3_quality"), FALSE);
	gtk_widget_set_sensitive(LKP_PREF("bitrate_lbl_2"), FALSE);
}

static void enable_mp3_widgets(void)
{
	gtk_widget_set_sensitive(LKP_PREF("mp3_vbr"), TRUE);
	gtk_widget_set_sensitive(LKP_PREF("bitrate_lbl"), TRUE);
	gtk_widget_set_sensitive(LKP_PREF("mp3bitrate"), TRUE);
	gtk_widget_set_sensitive(LKP_PREF("combo_mp3_quality"), TRUE);
	gtk_widget_set_sensitive(LKP_PREF("bitrate_lbl_2"), TRUE);
}

static void show_completed_dialog(int numOk, int numFailed)
{
#define CREATOK " created successfully"
#define CREATKO "There was an error creating "
    if (numFailed > 0) {
		DIALOG_WARNING_CLOSE(ngettext(CREATKO "%d file", CREATKO "%d files", numFailed), numFailed);
	} else {
		DIALOG_INFO_CLOSE(ngettext("%d file" CREATOK, "%d files" CREATOK, numOk), numOk);
	}
}

static prefs *new_prefs()
{
	prefs *p = g_malloc0(sizeof(prefs));
	return p;
}

static shared *new_shared_data()
{
	shared *p = g_malloc0(sizeof(shared));
	p->rip_tracks_completed=0;
	p->rip_percent = 0.0;
	p->mp3_percent = 0.0;
	p->aborted = false;
	p->playlist_mp3 = NULL;
	p->playlist_wav = NULL;
	p->log_fd = NULL;
	return p;
}

static void clear_prefs(prefs * p)
{
	g_free(p->cdrom);
	p->cdrom = NULL;

	g_free(p->music_dir);
	p->music_dir = NULL;

	g_free(p->format_music);
	p->format_music = NULL;

	g_free(p->format_playlist);
	p->format_playlist = NULL;

	g_free(p->format_albumdir);
	p->format_albumdir = NULL;

	g_free(p->server_name);
	p->server_name = NULL;

	g_free(p->cddb_server_name);
	p->cddb_server_name = NULL;
}

// free memory allocated for prefs struct
// also frees any strings pointed to in the struct
/*
static void delete_prefs(prefs * p)
{
	clear_prefs(p);
	free(p);
}*/

// returns a new prefs struct with all members set to nice default values
// this struct must be freed with delete_prefs()
static prefs *get_default_prefs()
{
	prefs *p = new_prefs();
	p->do_cddb_updates = 1;
	p->do_log = 0;
	p->max_log_size = 5000; // kb
	p->log_file = g_strdup(DEFAULT_LOG_FILE);
	p->eject_on_done = 0;
	p->main_window_height = 450;
	p->main_window_width = 600;
	p->make_playlist = 1;
	p->mp3_bitrate = 10;
	p->mp3_quality = 2; // 0:low, 1:good, 2:high, 3:max
	p->mp3_vbr = 1;
	p->rip_mp3 = 1;
	p->rip_wav = 0;
	p->use_proxy = 0;
	p->port_number = DEFAULT_PROXY_PORT;
	p->cddb_port_number = DEFAULT_CDDB_SERVER_PORT;
	p->music_dir = g_strdup_printf("%s/%s", getenv("HOME"), "irongripped");
	p->cdrom = g_strdup("/dev/cdrom");
	p->format_music = g_strdup("%N - %A - %T");
	p->format_playlist = g_strdup("%A - %L");
	p->format_albumdir = g_strdup("%A - %L - %Y");
	p->server_name = g_strdup("10.0.0.1");
	p->cddb_server_name = g_strdup(DEFAULT_CDDB_SERVER);
	return p;
}

static void set_pref_text(char *widget_name, char *text)
{
	gtk_entry_set_text(GTK_ENTRY(lookup_widget(win_prefs, widget_name)), text);
}

static void set_pref_toggle(char *widget_name, int on_off)
{
	gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(lookup_widget(win_prefs, widget_name)), on_off);
}

static char * prefs_get_music_dir(prefs * p);
// sets up all of the widgets in the preferences dialog to
// match the given prefs struct
static void set_widgets_from_prefs(prefs * p)
{
	set_pref_text("cddb_server_name", p->cddb_server_name);
	set_pref_text("cdrom", p->cdrom);
	set_pref_text("format_albumdir", p->format_albumdir);
	set_pref_text("format_music", p->format_music);
	set_pref_text("format_playlist", p->format_playlist);
	set_pref_text("server_name", p->server_name);

	gtk_file_chooser_set_current_folder(GTK_FILE_CHOOSER(LKP_PREF("music_dir")), prefs_get_music_dir(p));
	gtk_range_set_value(GTK_RANGE(LKP_PREF("mp3bitrate")), p->mp3_bitrate);
	gtk_combo_box_set_active(GTK_COMBO_BOX(LKP_PREF("combo_mp3_quality")), p->mp3_quality);

	set_pref_toggle("do_cddb_updates", p->do_cddb_updates);
	set_pref_toggle("do_log", p->do_log);
	set_pref_toggle("eject_on_done", p->eject_on_done);
	set_pref_toggle("make_playlist", p->make_playlist);
	set_pref_toggle("mp3_vbr", p->mp3_vbr);
	set_pref_toggle("rip_mp3", p->rip_mp3);
	set_pref_toggle("rip_wav", p->rip_wav);
	set_pref_toggle("use_proxy", p->use_proxy);

	char tempStr[10];
	snprintf(tempStr, 10, "%d", p->port_number);
	set_pref_text("port_number", tempStr);
	snprintf(tempStr, 10, "%d", p->cddb_port_number);
	set_pref_text("cddb_port_number", tempStr);

	/* disable widgets if needed */
	if (!(p->rip_mp3)) disable_mp3_widgets();
}

static gchar* get_pref_text(char *widget_name)
{
	const gchar *s = GET_PREF_TEXT(widget_name);
	return g_strdup(s);
}

static int get_pref_toggle(char *widget_name)
{
	return gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(lookup_widget(win_prefs, widget_name)));
}

// populates a prefs struct from the current state of the widgets
static void get_prefs_from_widgets(prefs * p)
{
	clear_prefs(p);

	gchar *tocopy = gtk_file_chooser_get_filename(GTK_FILE_CHOOSER(LKP_PREF("music_dir")));
	p->music_dir = g_strdup(tocopy);
	g_free(tocopy);
	p->mp3_bitrate = (int)gtk_range_get_value(GTK_RANGE(LKP_PREF("mp3bitrate")));
	p->mp3_quality = gtk_combo_box_get_active(GTK_COMBO_BOX(LKP_PREF("combo_mp3_quality")));

	p->cdrom = get_pref_text("cdrom");
	p->format_music = get_pref_text("format_music");
	p->format_playlist = get_pref_text("format_playlist");
	p->format_albumdir = get_pref_text("format_albumdir");
	p->cddb_server_name = get_pref_text("cddb_server_name");
	p->server_name = get_pref_text("server_name");

	p->make_playlist = get_pref_toggle("make_playlist");
	p->rip_wav = get_pref_toggle("rip_wav");
	p->rip_mp3 = get_pref_toggle("rip_mp3");
	p->mp3_vbr = get_pref_toggle("mp3_vbr");
	p->eject_on_done = get_pref_toggle("eject_on_done");
	p->do_cddb_updates = get_pref_toggle("do_cddb_updates");
	p->use_proxy = get_pref_toggle("use_proxy");
	p->do_log = get_pref_toggle("do_log");

	p->port_number = atoi(GET_PREF_TEXT("port_number"));
	p->cddb_port_number = atoi(GET_PREF_TEXT("cddb_port_number"));
}

static gchar* get_config_path(const gchar *file_suffix)
{
	// TODO get a config file path, using XDG_CONFIG_HOME if available
	// TODO fix allocation scheme
	// TODO create .config dir if necessary
	// TODO check that parent directories exist

	const gchar *home = g_getenv("HOME");
	gchar *filename = NULL;
	if(file_suffix == NULL) {
		filename = g_strdup_printf("%s/.config/%s.conf", home, PACKAGE);
	} else {
		filename = g_strdup_printf("%s/.config/%s.%s", home, PACKAGE, file_suffix);
	}
	return filename;
}

// store the given prefs struct to the config file
static void save_prefs(prefs * p)
{
	gchar *file = get_config_path(NULL);

	FILE *fd = fopen(file, "w");
	if (fd == NULL) {
		log_gen(__func__, LOG_WARN, "Warning: could not save fd file: %s", strerror(errno));
		return;
	}
	log_gen(__func__, LOG_INFO, "Saving configuration");
	fprintf(fd, "DEVICE=%s\n", p->cdrom);
	fprintf(fd, "MUSIC_DIR=%s\n", p->music_dir);
	fprintf(fd, "MAKE_PLAYLIST=%d\n", p->make_playlist);
	fprintf(fd, "FORMAT_MUSIC=%s\n", p->format_music);
	fprintf(fd, "FORMAT_PLAYLIST=%s\n", p->format_playlist);
	fprintf(fd, "FORMAT_ALBUMDIR=%s\n", p->format_albumdir);
	fprintf(fd, "RIP_WAV=%d\n", p->rip_wav);
	fprintf(fd, "RIP_MP3=%d\n", p->rip_mp3);
	fprintf(fd, "MP3_VBR=%d\n", p->mp3_vbr);
	fprintf(fd, "MP3_BITRATE=%d\n", p->mp3_bitrate);
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
	fprintf(fd, "CDDB_PORT=%d\n", p->cddb_port_number);
	fclose(fd);
}

static int is_valid_port_number(int number);

// load the prefereces from the config file into the given prefs struct
static void load_prefs(prefs * p)
{
	gchar *file = get_config_path(NULL);
	char *buf = NULL;
	char *s = NULL; // start position into buffer
	if(!is_file(file)) {
		return;
	}
	if(!load_file(file, &buf)) {
		return;
	}
	s = buf; // Start of HEADER section
	get_field(s, file, "DEVICE", &(p->cdrom));
	get_field(s, file, "MUSIC_DIR", &(p->music_dir));
	get_field_as_long(s,file,"MAKE_PLAYLIST", &(p->make_playlist));
	get_field_as_long(s,file,"RIP_WAV", &(p->rip_wav));
	get_field_as_long(s,file,"RIP_MP3", &(p->rip_mp3));
	get_field_as_long(s,file,"MP3_VBR", &(p->mp3_vbr));

	int i = 0;
	get_field_as_long(s,file,"MP3_BITRATE", &i);
	if (i >= 32) p->mp3_bitrate = i;
	i=0;
	get_field_as_long(s,file,"MP3_QUALITY", &i);
	if (i > -1) p->mp3_quality = i;
	i=0;
	get_field_as_long(s,file,"WINDOW_WIDTH", &i);
	if (i >= 200) p->main_window_width = i;
	i=0;
	get_field_as_long(s,file,"WINDOW_HEIGHT", &i);
	if (i >= 150) p->main_window_height = i;

	get_field_as_long(s,file,"DO_LOG", &(p->do_log));
	get_field_as_long(s,file,"MAX_LOG_SIZE", &(p->max_log_size));
	get_field(s, file, "LOG_FILE", &(p->log_file));
	get_field_as_long(s,file,"EJECT", &(p->eject_on_done));
	get_field_as_long(s,file,"CDDB_UPDATE", &(p->do_cddb_updates));
	get_field_as_long(s,file,"USE_PROXY",&(p->use_proxy));
	get_field_as_long(s,file,"PORT",&(p->port_number));
	if (p->port_number == 0 || !is_valid_port_number(p->port_number)) {
		printf("bad port number read from config file, using %d instead\n", DEFAULT_PROXY_PORT);
		p->port_number = DEFAULT_PROXY_PORT;
	}
	get_field_as_long(s,file,"CDDB_PORT",&(p->cddb_port_number));
	if (p->cddb_port_number == 0 || !is_valid_port_number(p->cddb_port_number)) {
		printf("bad port number read from config file, using 888 instead\n");
		p->cddb_port_number = DEFAULT_CDDB_SERVER_PORT;
	}
	get_field(s, file, "SERVER_NAME", &(p->server_name));
	get_field(s, file, "CDDB_SERVER_NAME", &(p->cddb_server_name));
	get_field(s, file, "FORMAT_MUSIC", &(p->format_music));
	get_field(s, file, "FORMAT_PLAYLIST", &(p->format_playlist));
	get_field(s, file, "FORMAT_ALBUMDIR", &(p->format_albumdir));
	if(buf) {
		g_free(buf);
	}
}

// use this method when reading the "music_dir" field of a prefs struct
// it will make sure it always points to a nice directory
static char *prefs_get_music_dir(prefs * p)
{
	struct stat s;
	if (stat(p->music_dir, &s) == 0) {
		return p->music_dir;
	}
	gchar *newdir = g_strdup_printf("%s/%s", getenv("HOME"), "irongripped");
	DIALOG_ERROR_OK("The music directory '%s' does not exist.\n\nThe music directory will be reset to '%s'.", p->music_dir, newdir);
	g_free(p->music_dir);
	p->music_dir = newdir;
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

static bool string_has_slashes(const char* string);

static bool prefs_are_valid(void)
{
	bool somethingWrong = false;

	// playlistfile
	if (string_has_slashes(GET_PREF_TEXT("format_playlist"))) {
		DIALOG_ERROR_OK(_("Invalid characters in the '%s' field"), _("Playlist file: "));
		somethingWrong = true;
	}
	// musicfile
	if (string_has_slashes(GET_PREF_TEXT("format_music"))) {
		DIALOG_ERROR_OK( _("Invalid characters in the '%s' field"), _("Music file: "));
		somethingWrong = true;
	}
	if (strlen(GET_PREF_TEXT("format_music")) == 0) {
		DIALOG_ERROR_OK( _("'%s' cannot be empty"), _("Music file: "));
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
	int cddb_port_number;
	rc = sscanf(GET_PREF_TEXT("cddb_port_number"), "%d", &cddb_port_number);
	if (rc != 1 || !is_valid_port_number(cddb_port_number)) {
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
	if (overwriteAll) return true;
	if (overwriteNone) return false;

	GtkWidget *dialog = gtk_dialog_new_with_buttons(_("Overwrite?"),
					     GTK_WINDOW(win_main),
					     GTK_DIALOG_DESTROY_WITH_PARENT,
					     GTK_STOCK_YES,
					     GTK_RESPONSE_ACCEPT, GTK_STOCK_NO, GTK_RESPONSE_REJECT, NULL);
	gtk_dialog_set_default_response(GTK_DIALOG(dialog), GTK_RESPONSE_REJECT);

	char *lastSlash = strrchr(pathAndName, '/');
	lastSlash++;

	char msgStr[1024];
	snprintf(msgStr, 1024, _("The file '%s' already exists. Do you want to overwrite it?\n"), lastSlash);
	GtkWidget *label = gtk_label_new(msgStr);
	gtk_widget_show(label);
	gtk_box_pack_start(GTK_BOX(GTK_DIALOG(dialog)->vbox), label, TRUE, TRUE, 0);

	GtkWidget *checkbox = gtk_check_button_new_with_mnemonic(_("Remember the answer for _all the files made from this CD"));
	gtk_widget_show(checkbox);
	gtk_box_pack_start(GTK_BOX(GTK_DIALOG(dialog)->vbox), checkbox, TRUE, TRUE, 0);

	int rc = gtk_dialog_run(GTK_DIALOG(dialog));
	if (gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(checkbox))) {
		if (rc == GTK_RESPONSE_ACCEPT)
			overwriteAll = true;
		else
			overwriteNone = true;
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
    global_data->aborted = true;
    if (cdparanoia_pid != 0) {
        kill(cdparanoia_pid, SIGKILL);
	}
    if (lame_pid != 0) {
        kill(lame_pid, SIGKILL);
	}
    /* wait until all the worker threads are done */
    while (cdparanoia_pid != 0 || lame_pid != 0) {
        Sleep(300);
		log_gen(__func__, LOG_INFO, "w1 cdparanoia=%d lame=%d", cdparanoia_pid, lame_pid);
    }
    g_cond_signal(available);
    g_thread_join(ripper);
    g_thread_join(encoder);
    g_thread_join(tracker);
    // gdk_flush();
    working = false;
    show_completed_dialog(numCdparanoiaOk + numLameOk, numCdparanoiaFailed + numLameFailed);
    gtk_window_set_title(GTK_WINDOW(win_main), PROGRAM_NAME);
    gtk_widget_hide(LKP_MAIN("win_ripping"));
	gtk_widget_show(LKP_MAIN("scroll"));
	enable_all_main_widgets();
}
static void on_cancel_clicked(GtkButton * button, gpointer user_data)
{
	log_gen(__func__, LOG_WARN, "on_cancel_clicked !!");
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
			DIALOG_ERROR_OK( "Unable to create WAV playlist \"%s\": %s", filename, strerror(errno));
		} else {
			fprintf(*file, "#EXTM3U\n");
		}
	}
}

static int exec_with_output(const char * args[], int toread, pid_t * p);
static void sigchld();

// uses cdparanoia to rip a WAV track from a cdrom
//
// cdrom    - the path to the cdrom device
// tracknum - the track to rip
// filename - the name of the output WAV file
// progress - the percent done
static void cdparanoia(char *cdrom, int tracknum, char *filename)
{
	char trackstring[3];
	snprintf(trackstring, 3, "%d", tracknum);
	const char *args[] = { CDPARANOIA_PRG, "-e", "-d", cdrom, trackstring, filename, NULL };

	int fd = exec_with_output(args, STDERR_FILENO, &cdparanoia_pid);
	fd_set readfds;
	FD_ZERO(&readfds);
	FD_SET(fd, &readfds);

	log_gen(__func__, LOG_TRACE, "Start of ripping track %d, fd=%d", tracknum, fd);

	// to convert the progress number stat cdparanoia spits out
	// into sector numbers divide by 1176
	// note: only use the "[wrote]" numbers
	char buf[256];
	int size = 0;
	do {
		int pos;
		pos = -1;
		do {
			/* Setup a timeout for read */
			struct timeval timeout;
			timeout.tv_sec = 16;
			timeout.tv_usec = 0;
			if(select(32, &readfds, NULL, NULL, &timeout) != 1) {
				log_gen(__func__, LOG_WARN, "cdparanoia is FROZEN !");
				goto cleanup;
			}
			pos++;
			size = read(fd, &buf[pos], 1);
			/* signal interrupted read(), try again */
			if (size == -1) {
				if(errno == EINTR) {
					log_gen(__func__, LOG_WARN, "signal interrupted read(), try again at position %d", pos);
					pos--;
					size = 1;
				} else {
					perror("Read error from cdparanoia");
					log_gen(__func__, LOG_WARN, "read error");
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
				global_data->rip_percent = (double)(sector - start) / (end - start);
			}
		} else {
			printf("Unknown data : %s\n", buf);
		}
	} while (size > 0);

cleanup:
	log_gen(__func__, LOG_TRACE, "End of ripping track %d", tracknum);
	close(fd);
	sigchld();
	/* don't go on until the signal for the previous call is handled */
	if (cdparanoia_pid != 0) {
		kill(cdparanoia_pid, SIGKILL);
	}
}

static void set_status(char *text);

// the thread that handles ripping tracks to WAV files
static gpointer rip_thread(gpointer data)
{
	GtkListStore *store = GTK_LIST_STORE(gtk_tree_view_get_model(GTK_TREE_VIEW(LKP_MAIN(WDG_TRACKLIST))));
	gboolean single_artist = gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(LKP_MAIN("single_artist")));
	const char *albumartist = GET_MAIN_TEXT(WDG_ALBUM_ARTIST);
	const char *albumtitle = GET_MAIN_TEXT(WDG_ALBUM_TITLE);
	const char *albumyear = GET_MAIN_TEXT(WDG_ALBUM_YEAR);
	const char *albumgenre = GET_MAIN_TEXT(WDG_ALBUM_GENRE);

	GtkTreeIter iter;
	gboolean rowsleft = gtk_tree_model_get_iter_first(GTK_TREE_MODEL(store), &iter);

	while (rowsleft) {
		int riptrack;
		int tracknum;
		const char *trackartist;
		const char *trackyear = 0;
		const char *tracktitle;
		gtk_tree_model_get(GTK_TREE_MODEL(store), &iter,
				   COL_RIPTRACK, &riptrack,
				   COL_TRACKNUM, &tracknum,
				   COL_TRACKARTIST, &trackartist,
				   COL_TRACKTITLE, &tracktitle, COL_YEAR, &trackyear, -1);

		if (single_artist) {
			trackartist = albumartist;
		}
		if (riptrack) {
			char *albumdir = parse_format(global_prefs->format_albumdir, 0, albumyear, albumartist, albumtitle, albumgenre, NULL);
			//~ musicfilename = parse_format(global_prefs->format_music, tracknum, trackyear, trackartist, albumtitle, tracktitle);
			char *musicfilename = parse_format(global_prefs->format_music, tracknum, albumyear, trackartist, albumtitle, albumgenre, tracktitle);
			char *wavfilename = make_filename(prefs_get_music_dir(global_prefs), albumdir, musicfilename, "wav");

			// TODO: set_status("Ripping track %d to \"%s\"\n", tracknum, wavfilename);

			if (global_data->aborted) g_thread_exit(NULL);

			struct stat statStruct;
			bool doRip = true;;

			int rc = stat(wavfilename, &statStruct);
			if (rc == 0) {
				gdk_threads_enter();
				if (!confirmOverwrite(wavfilename)) doRip = false;
				gdk_threads_leave();
			}
			if (doRip) cdparanoia(global_prefs->cdrom, tracknum, wavfilename);

			g_free(albumdir);
			g_free(musicfilename);
			g_free(wavfilename);
			global_data->rip_percent = 0.0;
			global_data->rip_tracks_completed++;
			log_gen(__func__, LOG_INFO, "NOW rip_tracks_completed = %d", global_data->rip_tracks_completed);
		}
		g_mutex_lock(barrier);
		counter++;
		g_mutex_unlock(barrier);
		g_cond_signal(available);

		if (global_data->aborted) g_thread_exit(NULL);
		rowsleft = gtk_tree_model_iter_next(GTK_TREE_MODEL(store), &iter);
	}
	// no more tracks to rip, safe to eject
	if (global_prefs->eject_on_done) {
		set_status("Trying to eject disc...");
		eject_disc(global_prefs->cdrom);
	}
	set_status("Finished");
	return NULL;
}

static gpointer encode_thread(gpointer data);
static gpointer track_thread(gpointer data);

static void set_status(char *text) {
	GtkWidget *s = LKP_MAIN("statusLbl");
	gtk_label_set_text(GTK_LABEL(s), _(text));
	gtk_label_set_use_markup(GTK_LABEL(s), TRUE);
}

static void reset_counters()
{
	working = true;
	global_data->aborted = false;
	global_data->allDone = false;
	counter = 0;
	barrier = g_mutex_new();
	available = g_cond_new();
	global_data->mp3_percent = 0.0;
	global_data->rip_tracks_completed = 0;
	encode_tracks_completed = 0;
	numCdparanoiaFailed = 0;
	numLameFailed = 0;
	numCdparanoiaOk = 0;
	numLameOk = 0;
	global_data->rip_percent = 0.0;
}

// spawn needed threads and begin ripping
static void dorip()
{
	reset_counters();

	GtkListStore *store = GTK_LIST_STORE(gtk_tree_view_get_model(GTK_TREE_VIEW(LKP_MAIN(WDG_TRACKLIST))));
	GtkTreeIter iter;
	gboolean rowsleft = gtk_tree_model_get_iter_first(GTK_TREE_MODEL(store), &iter);

	const char *albumartist = gtk_entry_get_text(GTK_ENTRY(LKP_MAIN(WDG_ALBUM_ARTIST)));
	const char *albumtitle = gtk_entry_get_text(GTK_ENTRY(LKP_MAIN(WDG_ALBUM_TITLE)));
	const char *albumyear = gtk_entry_get_text(GTK_ENTRY(LKP_MAIN(WDG_ALBUM_YEAR)));
	const char *albumgenre = gtk_entry_get_text(GTK_ENTRY(LKP_MAIN(WDG_ALBUM_GENRE)));
	char *albumdir = parse_format(global_prefs->format_albumdir, 0, albumyear, albumartist, albumtitle, albumgenre, NULL);
	char *playlist = parse_format(global_prefs->format_playlist, 0, albumyear, albumartist, albumtitle, albumgenre, NULL);

	overwriteAll = false;
	overwriteNone = false;

	set_status("make sure there's at least one format to rip to");
	if (!global_prefs->rip_wav && !global_prefs->rip_mp3) {
		DIALOG_ERROR_OK(_("No ripping/encoding method selected. Please enable one from the 'Preferences' menu."));
		g_free(albumdir);
		g_free(playlist);
		return;
	}
	set_status("make sure there's some tracks to rip");
	rowsleft = gtk_tree_model_get_iter_first(GTK_TREE_MODEL(store), &iter);
	tracks_to_rip = 0;
	while (rowsleft) {
		int riptrack;
		gtk_tree_model_get(GTK_TREE_MODEL(store), &iter, COL_RIPTRACK, &riptrack, -1);
		if (riptrack) tracks_to_rip++;
		rowsleft = gtk_tree_model_iter_next(GTK_TREE_MODEL(store), &iter);
	}
	if (tracks_to_rip == 0) {
		DIALOG_ERROR_OK(_("No tracks were selected for ripping/encoding. Please select at least one track."));
		g_free(albumdir);
		g_free(playlist);
		return;
	}

	set_status("Verify album directory...");
	/* CREATE the album directory */
	char *dirpath = make_filename(prefs_get_music_dir(global_prefs), albumdir, NULL, NULL);
	log_gen(__func__, LOG_INFO, "Making album directory '%s'", dirpath);

	if (recursive_mkdir(dirpath, S_IRWXU | S_IRWXG | S_IRWXO) != 0 && errno != EEXIST) {
		DIALOG_ERROR_OK("Unable to create directory '%s': %s", dirpath, strerror(errno));
		g_free(dirpath);
		g_free(albumdir);
		g_free(playlist);
		return;
	}
	g_free(dirpath);
	/* END CREATE the album directory */

	if (global_prefs->make_playlist) {
		set_status("Creating playlists");

		if (global_prefs->rip_wav) {
			char *filename = make_filename(prefs_get_music_dir(global_prefs), albumdir, playlist, "wav.m3u");
			make_playlist(filename, &(global_data->playlist_wav));
			g_free(filename);
		}
		if (global_prefs->rip_mp3) {
			char *filename = make_filename(prefs_get_music_dir(global_prefs), albumdir, playlist, "mp3.m3u");
			make_playlist(filename, &(global_data->playlist_mp3));
			g_free(filename);
		}
	}
	g_free(albumdir);
	g_free(playlist);

	set_status("Working...");
	disable_all_main_widgets();
	gtk_widget_show(LKP_MAIN("win_ripping"));
	gtk_widget_hide(LKP_MAIN("scroll"));

	ripper = g_thread_create(rip_thread, NULL, TRUE, NULL);
	encoder = g_thread_create(encode_thread, NULL, TRUE, NULL);
	tracker = g_thread_create(track_thread, NULL, TRUE, NULL);
}

static void lamehq(int tracknum, char *artist, char *album, char *title, char *genre, char *year, char *wavfilename, char *mp3filename)
{
	log_gen(__func__, LOG_INFO, "lame() Genre: %s Artist: %s Title: %s", genre, artist, title);

	// nice lame -q 0 -v -V 0 $file #  && rm $file
	int pos = 0;
	const char *args[32];
	args[pos++] = LAME_PRG;
	args[pos++] = "--nohist";
	args[pos++] = "-q";
	args[pos++] = "0";
	args[pos++] = "-V";
	args[pos++] = "0";
	args[pos++] = "--id3v2-only";

	char tracknum_text[3];
	snprintf(tracknum_text, 3, "%d", tracknum);
	if ((tracknum > 0) && (tracknum < 100)) {
		args[pos++] = "--tn";
		args[pos++] = tracknum_text;
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
	// lame refuses to accept some genres that come from cddb, and users get upset
	// No longer an issue - users can now edit the genre field -lnr
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

	int fd = exec_with_output(args, STDERR_FILENO, &lame_pid);
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
		} while ((buf[pos] != '\r') && (buf[pos] != '\n') && (size > 0) && (pos < 256));
		buf[pos] = '\0';

		int sector;
		int end;
		if (sscanf(buf, "%d/%d", &sector, &end) == 2) {
			global_data->mp3_percent = (double)sector / end;
		}
	} while (size > 0);
	close(fd);
	Sleep(200);
	sigchld();
}

// the thread that handles encoding WAV files to all other formats
static gpointer encode_thread(gpointer data)
{
	char *musicfilename = NULL;
	char *trackartist = NULL;
	char *tracktime = NULL;
	char *tracktitle = NULL;
	char *trackyear;

	GtkListStore *store = GTK_LIST_STORE(gtk_tree_view_get_model(GTK_TREE_VIEW(LKP_MAIN(WDG_TRACKLIST))));
	gboolean single_artist = gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(LKP_MAIN("single_artist")));
	gboolean single_genre = gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(LKP_MAIN("single_genre")));

	GtkWidget *album_artist_widget = LKP_MAIN(WDG_ALBUM_ARTIST);
	const char *temp_album_artist = gtk_entry_get_text(GTK_ENTRY(album_artist_widget));
	char *album_artist = g_strdup(temp_album_artist);
	add_completion(album_artist_widget);
	save_completion(album_artist_widget);

	const char *temp_year = gtk_entry_get_text(GTK_ENTRY(LKP_MAIN(WDG_ALBUM_YEAR)));
	char *album_year = g_strdup(temp_year);

	GtkWidget *album_title_widget = LKP_MAIN(WDG_ALBUM_TITLE);
	const char *temp_album_title = gtk_entry_get_text(GTK_ENTRY(album_title_widget));
	char *album_title = g_strdup(temp_album_title);
	add_completion(album_title_widget);
	save_completion(album_title_widget);

	GtkWidget *album_genre_widget = LKP_MAIN(WDG_ALBUM_GENRE);
	const char *temp_album_genre = gtk_entry_get_text(GTK_ENTRY(album_genre_widget));
	char *album_genre = g_strdup(temp_album_genre);
	add_completion(album_genre_widget);
	save_completion(album_genre_widget);

	GtkTreeIter iter;
	gboolean rowsleft = gtk_tree_model_get_iter_first(GTK_TREE_MODEL(store), &iter);

	while (rowsleft) {
		g_mutex_lock(barrier);
		while ((counter < 1) && (!global_data->aborted)) {
			g_cond_wait(available, barrier);
		}
		counter--;
		g_mutex_unlock(barrier);
		if (global_data->aborted) g_thread_exit(NULL);

		int riptrack;
		int tracknum;
		char *genre = NULL;
		gtk_tree_model_get(GTK_TREE_MODEL(store), &iter,
				   COL_RIPTRACK, &riptrack,
				   COL_TRACKNUM, &tracknum,
				   COL_TRACKARTIST, &trackartist,
				   COL_TRACKTITLE, &tracktitle,
				   COL_TRACKTIME, &tracktime, COL_GENRE, &genre, COL_YEAR, &trackyear, -1);

		int min;
		int sec;
		sscanf(tracktime, "%d:%d", &min, &sec);

		if (single_artist) {
			trackartist = album_artist;
		}
		if (single_genre) genre = album_genre;

		if (riptrack) {
			char *albumdir = parse_format(global_prefs->format_albumdir, 0, album_year, album_artist, album_title, genre, NULL);
			//~ musicfilename = parse_format(global_prefs->format_music, tracknum, trackyear, trackartist, album_title, tracktitle);
			musicfilename = parse_format(global_prefs->format_music, tracknum, album_year, trackartist, album_title, genre, tracktitle);
			char *wavfilename = NULL;
			wavfilename = make_filename(prefs_get_music_dir(global_prefs), albumdir, musicfilename, "wav");
			char *mp3filename = NULL;
			mp3filename = make_filename(prefs_get_music_dir(global_prefs), albumdir, musicfilename, "mp3");

			if (global_prefs->rip_mp3) {
				log_gen(__func__, LOG_INFO, "Encoding track %d to \"%s\"", tracknum, mp3filename);
				if (global_data->aborted) g_thread_exit(NULL);

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
					lamehq(tracknum, trackartist, album_title, tracktitle, genre, album_year, wavfilename, mp3filename);
				}
				if (global_data->aborted) g_thread_exit(NULL);

				if (global_data->playlist_mp3) {
					fprintf(global_data->playlist_mp3, "#EXTINF:%d,%s - %s\n", (min * 60) + sec, trackartist, tracktitle);
					fprintf(global_data->playlist_mp3, "%s\n", basename(mp3filename));
					fflush(global_data->playlist_mp3);
				}
			}
			if (!global_prefs->rip_wav) {
				log_gen(__func__, LOG_INFO, "Removing track %d WAV file", tracknum);
				if (unlink(wavfilename) != 0) {
					log_gen(__func__, LOG_WARN, "Unable to delete WAV file \"%s\": %s", wavfilename, strerror(errno));
				}
			} else {
				if (global_data->playlist_wav) {
					fprintf(global_data->playlist_wav, "#EXTINF:%d,%s - %s\n", (min * 60) + sec, trackartist, tracktitle);
					fprintf(global_data->playlist_wav, "%s\n", basename(wavfilename));
					fflush(global_data->playlist_wav);
				}
			}
			g_free(albumdir);
			g_free(musicfilename);
			g_free(wavfilename);
			g_free(mp3filename);

			global_data->mp3_percent = 0.0;
			encode_tracks_completed++;
		}
		if (global_data->aborted) g_thread_exit(NULL);

		rowsleft = gtk_tree_model_iter_next(GTK_TREE_MODEL(store), &iter);
	}
	g_free(album_artist);
	g_free(album_title);
	g_free(album_genre);

	if (global_data->playlist_wav)
		fclose(global_data->playlist_wav);
	global_data->playlist_wav = NULL;
	if (global_data->playlist_mp3)
		fclose(global_data->playlist_mp3);
	global_data->playlist_mp3 = NULL;

	g_mutex_free(barrier);
	barrier = NULL;
	g_cond_free(available);
	available = NULL;

	log_gen(__func__, LOG_INFO, "rip_percent= %f %%", global_data->rip_percent);
	/* wait until all the worker threads are done */
	while (cdparanoia_pid != 0 || lame_pid != 0) {
		printf("w2\n");
		Sleep(300);
	}
	Sleep(200);
	log_gen(__func__, LOG_INFO, "Waking up to allDone");
	global_data->allDone = true;		// so the tracker thread will exit
	working = false;
	gdk_threads_enter();
	show_completed_dialog(numCdparanoiaOk + numLameOk, numCdparanoiaFailed + numLameFailed);
	gtk_widget_hide(LKP_MAIN("win_ripping"));
	gtk_widget_show(LKP_MAIN("scroll"));
	enable_all_main_widgets();
	gdk_threads_leave();
	return NULL;
}

// the thread that calculates the progress of the other threads and updates the progress bars
static gpointer track_thread(gpointer data)
{
	int parts = 1;
	if (global_prefs->rip_mp3) parts++;
	gdk_threads_enter();
	GtkProgressBar *progress_total = GTK_PROGRESS_BAR(LKP_MAIN("progress_total"));
	GtkProgressBar *progress_rip = GTK_PROGRESS_BAR(LKP_MAIN("progress_rip"));
	GtkProgressBar *progress_encode = GTK_PROGRESS_BAR(LKP_MAIN("progress_encode"));
	gtk_progress_bar_set_fraction(progress_total, 0.0);
	gtk_progress_bar_set_text(progress_total, _("Waiting..."));
	gtk_progress_bar_set_fraction(progress_rip, 0.0);
	gtk_progress_bar_set_text(progress_rip, _("Waiting..."));
	if (parts > 1) {
		gtk_progress_bar_set_fraction(progress_encode, 0.0);
		gtk_progress_bar_set_text(progress_encode, _("Waiting..."));
	} else {
		gtk_progress_bar_set_fraction(progress_encode, 1.0);
		gtk_progress_bar_set_text(progress_encode, "100% (0/0)");
	}
	gdk_threads_leave();

	double prip;
	char srip[32];
	double pencode = 0;
	char sencode[13];
	double ptotal = 0.0;;
	bool started = false;

	while (!global_data->allDone && ptotal < 1.0) {
		if (global_data->aborted) {
			log_gen(__func__, LOG_WARN, "Aborted 1");
			g_thread_exit(NULL);
		}
		if(!started && global_data->rip_percent <= 0.0 && (parts == 1 || global_data->mp3_percent <= 0.0)) {
			Sleep(400);
			continue;
		}
		started = true;
		prip = (global_data->rip_tracks_completed + global_data->rip_percent) / tracks_to_rip;
		snprintf(srip, 32, "%02.2f%% (%d/%d)", (prip * 100), (global_data->rip_tracks_completed < tracks_to_rip) ? (global_data->rip_tracks_completed + 1) : tracks_to_rip, tracks_to_rip);
		if(prip>1.0) {
			log_gen(__func__, LOG_WARN, "prip overflow=%.2f%% completed=%d rip=%.2f%% remain=%d", prip, global_data->rip_tracks_completed, global_data->rip_percent, tracks_to_rip);
			prip=1.0;
		}
		if (parts > 1) {
			pencode = ((double)encode_tracks_completed / (double)tracks_to_rip) + ((global_data->mp3_percent) / (parts - 1) / tracks_to_rip);
			snprintf(sencode, 13, "%d%% (%d/%d)", (int)(pencode * 100), (encode_tracks_completed < tracks_to_rip) ? (encode_tracks_completed + 1) : tracks_to_rip, tracks_to_rip);
			ptotal = prip / parts + pencode * (parts - 1) / parts;
		} else {
			ptotal = prip;
		}
		char stotal[9];
		snprintf(stotal, 9, "%02.2f%%", ptotal * 100);

		char windowTitle[32];	/* "IronGrip - 100%" */
		strcpy(windowTitle, PROGRAM_NAME " - ");
		strcat(windowTitle, stotal);
		if (global_data->aborted) {
			log_gen(__func__, LOG_WARN, "Aborted 2");
			g_thread_exit(NULL);
		}

		gdk_threads_enter();
		gtk_progress_bar_set_fraction(progress_rip, prip);
		gtk_progress_bar_set_text(progress_rip, srip);
		if (parts > 1) {
			gtk_progress_bar_set_fraction(progress_encode, pencode);
			gtk_progress_bar_set_text(progress_encode, sencode);
		}
		gtk_progress_bar_set_fraction(progress_total, ptotal);
		gtk_progress_bar_set_text(progress_total, stotal);
		gtk_window_set_title(GTK_WINDOW(win_main), windowTitle);
		gdk_threads_leave();
		Sleep(200);
	}
	gdk_threads_enter();
	gtk_window_set_title(GTK_WINDOW(win_main), "Iron Grip");
	gdk_threads_leave();
	log_gen(__func__, LOG_TRACE, "The End.");
	return NULL;
}

// signal handler to find out when our child has exited
static void sigchld()
{
	if (global_prefs == NULL)
		return;

	int status = -1;
	pid_t pid = wait(&status);
	log_gen(__func__, LOG_INFO, "waited for %d, status=%d (know about wav %d, mp3 %d)",
			pid, status, cdparanoia_pid, lame_pid);

	if (pid != cdparanoia_pid && pid != lame_pid) {
		log_gen(__func__, LOG_FATAL, "unknown pid, report bug please");
	}
	if (WIFEXITED(status)) {
		// log_gen(__func__, LOG_INFO, "exited, status=%d", WEXITSTATUS(status));
	} else if (WIFSIGNALED(status)) {
		log_gen(__func__, LOG_INFO, "killed by signal %d", WTERMSIG(status));
	} else if (WIFSTOPPED(status)) {
		log_gen(__func__, LOG_INFO, "stopped by signal %d", WSTOPSIG(status));
	} else if (WIFCONTINUED(status)) {
		log_gen(__func__, LOG_INFO, "continued");
	}

	if (status != 0) {
		if (pid == cdparanoia_pid) {
			cdparanoia_pid = 0;
			if (global_prefs->rip_wav)
				numCdparanoiaFailed++;
		} else if (pid == lame_pid) {
			lame_pid = 0;
			numLameFailed++;
		}
	} else {
		if (pid == cdparanoia_pid) {
			cdparanoia_pid = 0;
			if (global_prefs->rip_wav)
				numCdparanoiaOk++;
		} else if (pid == lame_pid) {
			lame_pid = 0;
			numLameOk++;
		}
	}
}

// fork() and exec() the file listed in "args"
//
// args - a valid array for execvp()
// toread - the file descriptor to pipe back to the parent
// p - a place to write the PID of the exec'ed process
//
// returns - a file descriptor that reads whatever the program outputs on "toread"
static int exec_with_output(const char *args[], int toread, pid_t * p)
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
	log_gen(__func__, LOG_INFO, "%d started: %s ", *p, args[0]);
	// int count;
	// for (count = 1; args[count] != NULL; count++) log_gen(__func__, LOG_INFO, "%s ", args[count]);

	// close the side of the pipe we don't need
	close(pipefd[1]);
	return pipefd[0];
}

int main(int argc, char *argv[])
{
	global_data = new_shared_data();
	global_prefs = get_default_prefs();
	load_prefs(global_prefs);
	log_init();

#ifdef ENABLE_NLS
	/* initialize gettext */
	bindtextdomain(PACKAGE, PACKAGE_LOCALE_DIR);
	bind_textdomain_codeset(PACKAGE, UTF8);	/* so that gettext() returns UTF-8 strings */
	textdomain(PACKAGE);
#endif

	g_thread_init(NULL);
	gdk_threads_init();
	gtk_init(&argc, &argv);

	win_main = create_main();
	gtk_widget_show(win_main);

	if(!lookup_cdparanoia()) {
		// exit(-1);
	}
	// set up recurring timeout to automatically re-scan the cdrom once in a while
	g_timeout_add_seconds(10, idle, (void *)1);

	// add an idle event to scan the cdrom drive ASAP
	g_idle_add(scan_on_startup, NULL);

	gtk_main();

	log_end();
	return 0;
}

