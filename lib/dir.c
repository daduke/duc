
#include "config.h"

#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <errno.h>
#include <assert.h>
#include <stdint.h>
#include <libgen.h>

#include "duc.h"
#include "db.h"
#include "buffer.h"
#include "private.h"
#include "varint.h"


struct duc_dir *duc_dir_new(struct duc *duc, dev_t dev, ino_t ino)
{
	struct duc_dir *dir = duc_malloc(sizeof(struct duc_dir));
	memset(dir, 0, sizeof *dir);

	dir->duc = duc;
	dir->dev = dev;
	dir->ino = ino;
	dir->path = NULL;
	dir->ent_cur = 0;
	dir->ent_count = 0;
	dir->size_total = 0;
	dir->ent_pool = 32768;
	dir->ent_list = duc_malloc(dir->ent_pool);

	return dir;
}


int duc_dir_add_ent(struct duc_dir *dir, const char *name, off_t size, mode_t mode, dev_t dev, ino_t ino)
{
	if((dir->ent_count+1) * sizeof(struct duc_dirent) > dir->ent_pool) {
		dir->ent_pool *= 2;
		dir->ent_list = duc_realloc(dir->ent_list, dir->ent_pool);
	}

	struct duc_dirent *ent = &dir->ent_list[dir->ent_count];
	dir->ent_count ++;

	ent->name = duc_strdup(name);
	ent->size = size;
	ent->mode = mode;
	ent->dev = dev;
	ent->ino = ino;

	return 0;
}


static int fn_comp_ent(const void *a, const void *b)
{
	const struct duc_dirent *ea = a;
	const struct duc_dirent *eb = b;
	return(ea->size < eb->size);
}


static int mkkey(dev_t dev, ino_t ino, char *key, size_t keylen)
{
	return snprintf(key, keylen, "%jx/%jx", dev, ino);
}


off_t duc_dir_get_size(duc_dir *dir)
{
	return dir->size_total;
}


char *duc_dir_get_path(duc_dir *dir)
{
	return strdup(dir->path);
}


/*
 * Serialize duc_dir into a database record
 */

int duc_db_write_dir(struct duc_dir *dir)
{
	struct buffer *b = buffer_new(NULL, 0);
		
	buffer_put_varint(b, dir->dev_parent);
	buffer_put_varint(b, dir->ino_parent);
	buffer_put_varint(b, dir->size_total);
	buffer_put_varint(b, dir->file_count);
	buffer_put_varint(b, dir->dir_count);


	int i;
	struct duc_dirent *ent = dir->ent_list;

	for(i=0; i<dir->ent_count; i++) {
		buffer_put_string(b, ent->name);
		buffer_put_varint(b, ent->size);
		buffer_put_varint(b, ent->mode);
		buffer_put_varint(b, ent->dev);
		buffer_put_varint(b, ent->ino);
		ent++;
	}

	char key[32];
	size_t keyl = mkkey(dir->dev, dir->ino, key, sizeof key);
	int r = db_put(dir->duc->db, key, keyl, b->data, b->len);
	if(r != 0) {
		dir->duc->err = r;
		return -1;
	}

	buffer_free(b);

	return 0;
}


/*
 * Read database record and deserialize into duc_dir
 */

struct duc_dir *duc_db_read_dir(struct duc *duc, dev_t dev, ino_t ino)
{
	struct duc_dir *dir = duc_dir_new(duc, dev, ino);

	char key[32];
	size_t keyl;
	size_t vall;

	keyl = mkkey(dev, ino, key, sizeof key);
	char *val = db_get(duc->db, key, keyl, &vall);
	if(val == NULL) {
		duc_log(duc, LG_WRN, "Key %s not found in database\n", key);
		duc->err = DUC_E_PATH_NOT_FOUND;
		return NULL;
	}

	struct buffer *b = buffer_new(val, vall);

	uint64_t v;
	buffer_get_varint(b, &v); dir->dev_parent = v;
	buffer_get_varint(b, &v); dir->ino_parent = v;
	buffer_get_varint(b, &v); dir->size_total = v;
	buffer_get_varint(b, &v); dir->file_count = v;
	buffer_get_varint(b, &v); dir->dir_count = v;
	
	while(b->ptr < b->len) {

		uint64_t size;
		uint64_t dev;
		uint64_t ino;
		uint64_t mode;
		char *name;

		buffer_get_string(b, &name);
		buffer_get_varint(b, &size);
		buffer_get_varint(b, &mode);
		buffer_get_varint(b, &dev);
		buffer_get_varint(b, &ino);
	
		if(name) {
			duc_dir_add_ent(dir, name, size, mode, dev, ino);
			free(name);
		}
	}

	buffer_free(b);

	qsort(dir->ent_list, dir->ent_count, sizeof(struct duc_dirent), fn_comp_ent);

	return dir;
}


duc_dir *duc_dir_openent(duc_dir *dir, struct duc_dirent *e)
{
	duc_dir *dir2 = duc_db_read_dir(dir->duc, e->dev, e->ino);
	if(dir2) {
		asprintf(&dir2->path, "%s/%s", dir->path, e->name);
	}
	return dir2;
}


duc_dir *duc_dir_openat(duc_dir *dir, const char *name)
{
	if(strcmp(name, "..") == 0) {
		
		/* Special case: go up one directory */

		if(dir->dev_parent && dir->ino_parent) {
			duc_dir *pdir = duc_db_read_dir(dir->duc, dir->dev_parent, dir->ino_parent);
			if(pdir == NULL) return NULL;
			pdir->path = duc_strdup(dir->path);
			dirname(pdir->path);
			return pdir;
		}

	} else {

		/* Find given name in dir */

		size_t i;
		struct duc_dirent *e = dir->ent_list;
		for(i=0; i<dir->ent_count; i++) {
			if(strcmp(e->name, name) == 0) {
				return duc_dir_openent(dir, e);
			}
			e++;
		}
	}

	return NULL;
}


int duc_dir_limit(duc_dir *dir, size_t count)
{
	if(dir->ent_count <= count) return 0;

	off_t rest_size = 0;
	off_t rest_count = dir->ent_count - count;

	size_t i;
	struct duc_dirent *ent = dir->ent_list;
	for(i=0; i<dir->ent_count; i++) {
		if(i>=count-1) rest_size += ent->size;
		ent++;
	}

	dir->ent_list = realloc(dir->ent_list, sizeof(struct duc_dirent) * count);
	dir->ent_count = count;
	dir->ent_cur = 0;
	ent = &dir->ent_list[count-1];

	asprintf(&ent->name, "(%ld files)", rest_count);
	ent->mode = DUC_MODE_REST;
	ent->size = rest_size;
	ent->dev = 0;
	ent->ino = 0;

	return 0;
}


struct duc_dirent *duc_dir_find_child(duc_dir *dir, const char *name)
{
	size_t i;
	struct duc_dirent *ent = dir->ent_list;

	for(i=0; i<dir->ent_count; i++) {
		if(strcmp(name, ent->name) == 0) {
			return ent;
		}
		ent++;
	}
	
	dir->duc->err = DUC_E_PATH_NOT_FOUND;
	return NULL;
}



duc_dir *duc_dir_open(struct duc *duc, const char *path)
{
	/* Canonicalized path */

	char *path_canon = realpath(path, NULL);
	if(path_canon == NULL) {
		duc->err = DUC_E_PATH_NOT_FOUND;
		return NULL;
	}

	/* Find top path in database */

	int l = strlen(path_canon);
	dev_t dev = 0;
	ino_t ino = 0;
	while(l > 0) {
		struct duc_index_report *report;
		size_t report_len;
		report = db_get(duc->db, path_canon, l, &report_len);
		if(report && report_len == sizeof(*report)) {
			dev = report->dev;
			ino = report->ino;
			free(report);
			break;
		}
		l--;
		while(l > 1  && path_canon[l] != '/') l--;
	}

	if(l == 0) {
		duc_log(duc, LG_WRN, "Path %s not found in database\n", path_canon);
		duc->err = DUC_E_PATH_NOT_FOUND;
		free(path_canon);
		return NULL;
	}

	struct duc_dir *dir;

	dir = duc_db_read_dir(duc, dev, ino);

	if(dir == NULL) {
		duc->err = DUC_E_PATH_NOT_FOUND;
		free(path_canon);
		return NULL;
	}
	
	char rest[PATH_MAX];
	strncpy(rest, path_canon+l, sizeof rest);

	char *name = strtok(rest, "/");

	while(dir && name) {

		struct duc_dirent *ent = duc_dir_find_child(dir, name);

		struct duc_dir *dir_next = NULL;

		if(ent) {
			dir_next = duc_dir_openent(dir, ent);
		}

		duc_dir_close(dir);
		dir = dir_next;
		name = strtok(NULL, "/");
	}

	if(dir) {
		dir->path = strdup(path_canon);
	}

	return dir;
}


struct duc_dirent *duc_dir_read(duc_dir *dir)
{
	dir->duc->err = 0;
	if(dir->ent_cur < dir->ent_count) {
		struct duc_dirent *ent = &dir->ent_list[dir->ent_cur];
		dir->ent_cur ++;
		return ent;
	} else {
		return NULL;
	}
}


int duc_dir_rewind(duc_dir *dir)
{
	dir->ent_cur = 0;
	return 0;
}


int duc_dir_close(duc_dir *dir)
{
	if(dir->path) free(dir->path);
	int i;
	for(i=0; i<dir->ent_count; i++) {
		free(dir->ent_list[i].name);
	}
	free(dir->ent_list);
	free(dir);
	return 0;
}

/*
 * End
 */

