/*
 * Copyright (C) 2011-2013 by Dave Reisner <dreisner@archlinux.org>
 *
 * Permission is hereby granted, free of charge, to any person obtaining a copy
 * of this software and associated documentation files (the "Software"), to deal
 * in the Software without restriction, including without limitation the rights
 * to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
 * copies of the Software, and to permit persons to whom the Software is
 * furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in
 * all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
 * AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
 * OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN
 * THE SOFTWARE.
 */

#include <ctype.h>
#include <errno.h>
#include <glob.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "macro.h"
#include "repo.h"

struct repo_t *repo_new(const char *reponame)
{
	struct repo_t *repo;

	CALLOC(repo, 1, sizeof(struct repo_t), return NULL);

	if(asprintf(&repo->name, "%s", reponame) == -1) {
		fprintf(stderr, "error: failed to allocate memory\n");
		free(repo);
		return NULL;
	}

	/* assume glorious failure */
	repo->err = 1;

	return repo;
}

void repo_free(struct repo_t *repo)
{
	int i;

	free(repo->name);
	for(i = 0; i < repo->servercount; i++) {
		free(repo->servers[i]);
	}
	free(repo->servers);

	free(repo);
}

int repo_add_server(struct repo_t *repo, const char *server)
{
	if(!repo) {
		return 1;
	}

	repo->servers = realloc(repo->servers,
			sizeof(char *) * (repo->servercount + 1));

	repo->servers[repo->servercount] = strdup(server);
	repo->servercount++;

	return 0;
}

static size_t strtrim(char *str)
{
	char *left = str, *right;

	if(!str || *str == '\0') {
		return 0;
	}

	while(isspace((unsigned char)*left)) {
		left++;
	}
	if(left != str) {
		memmove(str, left, (strlen(left) + 1));
	}

	if(*str == '\0') {
		return 0;
	}

	right = (char *)rawmemchr(str, '\0') - 1;
	while(isspace((unsigned char)*right)) {
		right--;
	}
	*++right = '\0';

	return right - left;
}

static char *split_keyval(char *line, const char *sep)
{
	strsep(&line, sep);
	return line;
}

static int parse_one_file(const char *, char **, struct repo_t ***, int *);

static int parse_include(const char *include, char **section,
		struct repo_t ***repos, int *repocount)
{
	glob_t globbuf;
	size_t i;

	if(glob(include, GLOB_NOCHECK, NULL, &globbuf) != 0) {
		fprintf(stderr, "warning: globbing failed on '%s': out of memory\n",
				include);
		return -ENOMEM;
	}

	for(i = 0; i < globbuf.gl_pathc; i++) {
		parse_one_file(globbuf.gl_pathv[i], section, repos, repocount);
	}

	globfree(&globbuf);

	return 0;
}

static int parse_one_file(const char *filename, char **section,
		struct repo_t ***repos, int *repocount)
{
	FILE *fp;
	char *ptr;
	char line[4096];
	const char * const server = "Server";
	const char * const include = "Include";
	int in_options = 0, r = 0, lineno = 0;
	struct repo_t **active_repos = *repos;

	fp = fopen(filename, "r");
	if(!fp) {
		fprintf(stderr, "error: failed to open %s: %s\n", filename, strerror(errno));
		return -errno;
	}

	while(fgets(line, sizeof(line), fp)) {
		size_t len;
		++lineno;

		/* remove comments */
		ptr = strchr(line, '#');
		if(ptr) {
			*ptr = '\0';
		}

		len = strtrim(line);
		if(len == 0) {
			continue;
		}

		/* found a section header */
		if(line[0] == '[' && line[len - 1] == ']') {
			free(*section);
			*section = strndup(&line[1], len - 2);
			if(len - 2 == 7 && memcmp(*section, "options", 7) == 0) {
				in_options = 1;
			} else {
				in_options = 0;
				active_repos = realloc(active_repos, sizeof(struct repo_t *) * (*repocount + 2));
				active_repos[*repocount] = repo_new(*section);
				(*repocount)++;
			}
		}

		if(strchr(line, '=')) {
			char *key = line, *val = split_keyval(line, "=");
			strtrim(key);
			strtrim(val);

			if(strcmp(key, server) == 0) {
				if(*section == NULL) {
					fprintf(stderr, "error: failed to parse %s on line %d: found 'Server' directive "
							"outside of a section\n", filename, lineno);
					continue;
				}
				if(in_options) {
					fprintf(stderr, "error: failed to parse %s on line %d: found 'Server' directive "
							"in options section\n", filename, lineno);
					continue;
				}
				r = repo_add_server(active_repos[*repocount - 1], val);
				if(r < 0) {
					break;
				}
			} else if(strcmp(key, include) == 0) {
				parse_include(val, section, &active_repos, repocount);
			}
		}
	}

	fclose(fp);

	*repos = active_repos;

	return r;
}

struct repo_t **find_active_repos(const char *filename, int *repocount)
{
	struct repo_t **repos = NULL;
	char *section = NULL;

	*repocount = 0;

	if(parse_one_file(filename, &section, &repos, repocount) < 0) {
		/* TODO: free repos on fail? */
		return NULL;
	}

	free(section);

	return repos;
}

/* vim: set ts=2 sw=2 noet: */
