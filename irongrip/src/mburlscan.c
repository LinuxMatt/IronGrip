/*
 * mburlscan.c: Scan urls in MusicBrainz html page.
 *
 * Copyright(C) 2014 Matt Thirtytwo <matt59491@gmail.com>
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
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <ctype.h>

// http://www.discogs.com/viewimages?release=368143

char *FILTERS[] = { "http://", "coverartarchive.org/release/", NULL };
char *PATTERNS[] =  { "ecx.images-amazon.com", "www.allmusic.com/album/", "www.discogs.com/master/", "www.discogs.com/release/",  "www.youtube.com/watch?v=", NULL };
char *ANTIPATTERNS[] =  { "musicbrainz.org", "www.w3.org/", "purl.org/", "metabrainz.org/", NULL };

void maystore(char *d, char *a) {
	for(char **y = PATTERNS;*y;y++) {
		if(strstr(a, *y)) {
			// printf("%s\n", a);
			if(!strstr(d,a)) {
				strcat(d,a);
				strcat(d,"\n");
			}
			break;
		}
	}
}
int main(int argc, char *argv[])
{
	if(argc < 2) {
		printf("usage: %s <file>\n", argv[0]);
		return 2;
	}
	struct stat	st;
	if (stat(argv[1], &st)) {
		printf("Cannot stat %s\n", argv[1]);
		return 1;
	}
	FILE *fd = fopen(argv[1], "rb");
	if(!fd) {
		printf("Cannot open %s\n", argv[1]);
		return 1;
	}
	size_t sz = st.st_size+1;
	char *b = calloc(1, sz); // working copy
	char *c = calloc(1, sz); // original
	char *d = calloc(1, sz); // output
	fread(c, 1, sz-1, fd);
	fclose (fd);
	for(char **f = FILTERS;*f;f++) {
		memcpy(b,c,sz);
		for(char *p=b;*p;p++) {
			char *a = strstr(p, *f);
			if(a == NULL) {
				fprintf(stderr, "BREAKING with %lu bytes remaining out of %lu\n", strlen(p), sz);
				break;
			}
			for(char *e = a+strlen(*f);*e;e++) {
				if(*e != '<' && *e != '"' && !isspace(*e)) continue;
				*e = 0; p = e;
				int found = 0;
				for(char **x = ANTIPATTERNS;*x;x++) {
					if(strstr(a, *x)) { found=1; break; }
				}
				if(!found) { maystore(d,a); }
				break;
			}
		}
	}
	printf("%s\n", d);
	return 0;
}

