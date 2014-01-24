/*
 * sort_buffer.c: functions to add strings to a buffer, then qsort them.
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

typedef struct _data {
	char *buf;
	char *p;
	char **list;
	unsigned short i;
	unsigned short size;
	unsigned short max;
	unsigned short uniq;
} s_data;
static void init(s_data *g,int unsigned short uniq) {
	g->size = 4*1024;
	g->max = 40;
	g->uniq = uniq;
	g->p = g->buf = calloc(1,g->size);
	g->list = calloc(g->max,sizeof(char*));
}
static int cmp(const void *p1, const void *p2) {
	return strcmp(* (char * const *) p1, * (char * const *) p2);
}
static void sort(s_data *g) {
	qsort(g->list,g->i,sizeof(char *),cmp);
}
static void dump(s_data *g, char *msg) {
	for(int j=0; j<g->i; j++) {
		printf("%3d %s: [%s]\n", j+1, msg, g->list[j]);
	}
}
static void add(s_data *g, char *s) {
    int l = strlen(s);
    if(g->p - g->buf + l >= g->size - 1) {
        printf("size overflow\n");
        return;
    }
    if(g->i>=g->max) {
        printf("list overflow\n");
        return;
    }
    if(g->uniq) {
        for(int i = 0; i < g->i; i++) {
            if(strcmp(g->list[i], s)==0) {
                return;
            }
        }
    }
    memcpy(g->p, s, l);
    g->list[g->i]=g->p;
    g->p+=l+1;
    g->i++;
}
int main() {
	s_data *g = calloc(1,sizeof(s_data));
	printf("SIZE OF s_data = %lu\n", sizeof(s_data));
	init(g,1);

	add(g,"YANNICK");
	add(g,"ZORRO");
	add(g,"HELLO");
	add(g,"ROBERT");
	add(g,"TAN");
	add(g,"ZORRO");
	add(g,"YANNICK");
	add(g,"ZORRO");
	add(g,"ELEPHANT");

    sort(g);
    dump(g, "sorted");
	return 1;
}

