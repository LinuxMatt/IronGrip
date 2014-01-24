/*
 * mblookup.c: MusicBrainz lookup routines.
 *
 * Copyright(C) 2014 Matt Thirtytwo <matt59491@gmail.com>
 *
 * The original source code comes from the ripright program.
 * http://www.mcternan.me.uk/ripright/
 * Copyright(C) 2011 Michael C McTernan <Michael.McTernan.2001@cs.bris.ac.uk>
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * as published by the Free Software Foundation; either version 2
 * of the License, or (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place - Suite 330, Boston, MA  02111-1307, USA.
*/

#define _GNU_SOURCE
#include <stdio.h>
#include <stdlib.h>
#include <ctype.h>
#include <dlfcn.h>
#include <stdarg.h>
#include <stdbool.h>
#include <stdint.h>
#include <string.h>
#include <strings.h>
#include <sys/ioctl.h>
#include <sys/stat.h>
#include <sys/timeb.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <unistd.h>
#include <assert.h>

#ifdef NDEBUG
  #define dprintf(s)  printf("%s:%d: %s: %s\n",__FILE__,__LINE__,__func__,s)
#else
  #define dprintf(s)
#endif

#define M_ArrayLen(a) (sizeof(a) / sizeof(a[0]))

void *x_malloc(size_t size);
void *x_zalloc(size_t size);
void *x_calloc(size_t nmemb, size_t size);
void *x_realloc(void *ptr, size_t size);
char *x_strdup(const char *s) __attribute__((nonnull (1)));

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

void *x_malloc(size_t size)
{
	void *r = malloc(size);
	if (!r) {
		fprintf(stderr, "Fatal: malloc(%lu) failed\n", size);
		exit(EXIT_FAILURE);
	}
	return r;
}

void *x_zalloc(size_t size)
{
    return calloc(1, size);
}

void *x_calloc(size_t nmemb, size_t size)
{
    void *r = calloc(nmemb, size);
    if(!r) {
        fprintf(stderr, "Fatal: calloc(%lu, %lu) failed\n", nmemb, size);
        exit(EXIT_FAILURE);
    }
    return r;
}

void *x_realloc(void *ptr, size_t size)
{
    void *r = realloc(ptr, size);
    if(!r) {
        fprintf(stderr, "Fatal: realloc(%p, %lu) failed\n", ptr, size);
        exit(EXIT_FAILURE);
    }
    return r;
}

char *x_strdup(const char *s)
{
    char *r = strdup(s);
    if(!r) {
        fprintf(stderr, "Fatal: strdup() failed\n");
        exit(EXIT_FAILURE);
    }
    return r;
}

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

	n->tag = x_malloc(l = 64);
	m = 0;

	/* Read until a > */
	do {
		r = getnextchar(n, &n->tag[m++]);

		/* Skip space at the start of a tag eg. < tag> */
		if (m == 1 && isspace(n->tag[0]))
			m = 0;

		if (m == l)
			n->tag = x_realloc(n->tag, l += 256);

	}
	while (r == 1 && n->tag[m - 1] != '>');

	if (n->tag[m - 1] != '>') {
		dprintf("Failed to find closing '>'");
		free(n->tag);
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

	n->content = x_malloc(l = 256);
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
			n->content = x_realloc(n->content, l += 256);

	}
	while (r == 1 && tagcount > 0);

	if (r != 1) {
		dprintf("Failed to find closing tag");
		dprintf(n->tag);
		free(n->tag);
		free(n->content);
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
					char *c, *r = x_strdup(postS + 1);

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
			free(nn->tag);
		}
		if (nn->content) {
			free(nn->content);
		}
		while (nn->freeListLen-- > 0) {
			free(nn->freeList[nn->freeListLen]);
		}
		free(nn);
		*n = NULL;
	}
}

/** Parse some XML from a file descriptor.
 * \param[in,out] n  Pointer to a node pointer. If *n != NULL, it will be freed.
 */
bool XmlParseFd(struct xmlnode **n, int fd)
{
	XmlDestroy(n);

	*n = x_zalloc(sizeof(struct xmlnode));
	(*n)->fd = fd;

	if (!Parse(*n)) {
		free(*n);
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
	*n = x_zalloc(sizeof(struct xmlnode));
	(*n)->fd = -1;
	(*n)->array = s;
	if (!Parse(*n)) {
		free(*n);
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
    mbr->data = x_realloc(mbr->data, mbr->size + realsize + 1);
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
		free(cfdata.data);
	}
	ce_cleanup(ch);

cleanup:
	// dlclose(h); // Would blow up application
	return res;
}

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

/** Free memory associated with a mbartistcredit_t structure.  */
static void freeArtist(mbartistcredit_t * ad)
{
	free(ad->artistName);
	free(ad->artistNameSort);
	for (uint8_t t = 0; t < ad->artistIdCount; t++) {
		free(ad->artistId[t]);
	}
	free(ad->artistId);
}

static void freeMedium(mbmedium_t * md)
{
	free(md->title);
	for (uint16_t t = 0; t < md->trackCount; t++) {
		mbtrack_t *td = &md->track[t];
		free(td->trackName);
		freeArtist(&td->trackArtist);
	}
	free(md->track);
}

static void freeRelease(mbrelease_t * rel)
{
	free(rel->releaseGroupId);
	free(rel->releaseId);
	free(rel->asin);
	free(rel->albumTitle);
	free(rel->releaseType);
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
			cd->artistId = x_realloc(cd->artistId, sizeof(char *) * cd->artistIdCount);

			cd->artistId[cd->artistIdCount - 1] = x_strdup(XmlGetAttribute(artistNode, "id"));

			o = XmlFindSubNode(artistNode, "name");
			if (o) {
				const char *aa = XmlGetContent(o);

				if (cd->artistName == NULL) {
					cd->artistName = x_strdup(aa);
				} else {
					/* Concatenate multiple artists if needed */
					cd->artistName =
						x_realloc(cd->artistName, strlen(aa) + strlen(cd->artistName) + 3);

					strcat(cd->artistName, ", ");
					strcat(cd->artistName, aa);
				}
				XmlDestroy(&o);
			}
			o = XmlFindSubNode(artistNode, "sort-name");
			if (o) {
				cd->artistNameSort = x_strdup(XmlGetContent(o));
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
					td->trackId = x_strdup(id);
				}
				m = XmlFindSubNode(recording, "title");
				if (m) {
					td->trackName = x_strdup(XmlGetContent(m));
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
			md->title = x_strdup(XmlGetContent(n));
			XmlDestroy(&n);
		}
		n = XmlFindSubNode(mediumNode, "track-list");
		if (n) {
			const char *count = XmlGetAttribute(n, "count");

			if (count) {
				struct xmlnode *track = NULL;
				const char *s;

				md->trackCount = atoi(count);
				md->track = x_calloc(sizeof(mbtrack_t), md->trackCount);

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

	cd->releaseId = x_strdup(XmlGetAttribute(releaseNode, "id"));

	n = XmlFindSubNode(releaseNode, "asin");
	if (n) {
		cd->asin = x_strdup(XmlGetContent(n));
		XmlDestroy(&n);
	}
	n = XmlFindSubNode(releaseNode, "title");
	if (n) {
		cd->albumTitle = x_strdup(XmlGetContent(n));
		XmlDestroy(&n);
	}
	n = XmlFindSubNode(releaseNode, "release-group");
	if (n) {
		cd->releaseType = x_strdup(XmlGetAttribute(n, "type"));
		cd->releaseGroupId = x_strdup(XmlGetAttribute(n, "id"));
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
				mediums = x_realloc(mediums, sizeof(mbmedium_t) * (mediumCount + 1));
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

	free(buf);

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
			res->release = x_realloc(res->release, sizeof(mbrelease_t) * res->releaseCount);
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
bool MbLookup(const char *discId, mbresult_t * res)
{
	struct xmlnode *metaNode = NULL, *releaseListNode = NULL;
	void *buf;
	const char *s;

	memset(res, 0, sizeof(mbresult_t));

	s = buf = CurlFetch(NULL, "http://musicbrainz.org/ws/2/discid/%s", discId);
	if (!buf) {
		return false;
	}
	/* Find the metadata node */
	do {
		s = XmlParseStr(&metaNode, s);
	}
	while (metaNode != NULL && XmlTagStrcmp(metaNode, "metadata") != 0);

	free(buf);

	if (metaNode == NULL) {
		return false;
	}
	releaseListNode = XmlFindSubNodeFree(XmlFindSubNodeFree(metaNode, "disc"), "release-list");
	if (releaseListNode) {
		struct xmlnode *releaseNode = NULL;
		const char *s;

		s = XmlGetContent(releaseListNode);
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

void MbFree(mbresult_t * res)
{
	for (uint16_t r = 0; r < res->releaseCount; r++) {
		freeRelease(&res->release[r]);
	}
	free(res->release);
}

/** Print a result structure to stdout.  */
void MbPrint(const mbresult_t * res)
{
	for (uint16_t r = 0; r < res->releaseCount; r++) {
		mbrelease_t *rel = &res->release[r];

		printf("Release=%s\n", rel->releaseId);
		printf("  ASIN=%s\n", rel->asin);
		printf("  Album=%s\n", rel->albumTitle);
		printf("  AlbumArtist=%s\n", rel->albumArtist.artistName);
		printf("  AlbumArtistSort=%s\n", rel->albumArtist.artistNameSort);
		printf("  ReleaseType=%s\n", rel->releaseType);
		printf("  ReleaseGroupId=%s\n", rel->releaseGroupId);

		for (uint8_t t = 0; t < rel->albumArtist.artistIdCount; t++) {
			printf("  ArtistId=%s\n", rel->albumArtist.artistId[t]);
		}

		printf("  Total Disc=%u\n", rel->discTotal);

		const mbmedium_t *mb = &rel->medium;

		printf("  Medium\n");
		printf("    DiscNum=%u\n", mb->discNum);
		printf("    Title=%s\n", mb->title);

		for (uint16_t u = 0; u < mb->trackCount; u++) {
			const mbtrack_t *td = &mb->track[u];
			printf("    Track %u\n", u);
			printf("      Id=%s\n", td->trackId);
			printf("      Title=%s\n", td->trackName);
			printf("      Artist=%s\n", td->trackArtist.artistName);
			printf("      ArtistSort=%s\n", td->trackArtist.artistNameSort);
			for (uint8_t t = 0; t < td->trackArtist.artistIdCount; t++) {
				printf("      ArtistId=%s\n", td->trackArtist.artistId[t]);
			}
		}
	}
}

#define M_ArraySize(a)  (sizeof(a) / sizeof(a[0]))

static void fetch_image(const mbresult_t *res) {
	for (uint16_t r = 0; r < res->releaseCount; r++) {
		mbrelease_t *rel = &res->release[r];
		printf("http://musicbrainz.org/ws/2/release/%s\n", rel->releaseId);
		if(!rel->asin) continue;
		//printf("ASIN=%s\n", rel->asin);
		const char *artUrl[] = {
			"http://images.amazon.com/images/P/%s.02._SCLZZZZZZZ_.jpg", /* UK */
			"http://images.amazon.com/images/P/%s.01._SCLZZZZZZZ_.jpg", /* US */
			"http://images.amazon.com/images/P/%s.03._SCLZZZZZZZ_.jpg", /* DE */
			"http://images.amazon.com/images/P/%s.08._SCLZZZZZZZ_.jpg", /* FR */
			"http://images.amazon.com/images/P/%s.09._SCLZZZZZZZ_.jpg"  /* JP */
		};
		uint8_t attempt = 0;
		do {
			size_t sz = 0;
			const char *s = CurlFetch(&sz, artUrl[attempt], rel->asin);
			if(!s) continue;
			printf(artUrl[attempt], rel->asin);
			printf("\n");
			char f[1024];
			sprintf(f,"%s.jpg",rel->asin);
			FILE *fd = fopen(f, "wb");
			if(fd) {
				fwrite(s,1,sz,fd);
				fclose(fd);
				break;
			}
		} while(++attempt < M_ArraySize(artUrl));
	}
}

int main(int argc, char *argv[])
{
	mbresult_t res;
	if (argc > 1) {
		MbLookup(argv[1], &res);
		fetch_image(&res);
		//MbPrint(&res);
		MbFree(&res);
	}
	return 0;
}

