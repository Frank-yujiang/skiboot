/* Copyright 2013-2015 IBM Corp.
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 * 	http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or
 * implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

#include <stdio.h>
#include <stdlib.h>

#include <libflash/libffs.h>

extern int pnor_init(const char *mtd_path, struct ffs_handle **ffs);
extern void dump_parts(struct ffs_handle *ffs);


int main(int argc, char **argv)
{

	struct ffs_handle *ffs;

	if (argc != 2) {
		printf("usage: %s [pnor file]\n", argv[0]);
		exit(EXIT_FAILURE);
	}

	pnor_init(argv[1], &ffs);
	dump_parts(ffs);
	ffs_close(ffs);

	return 0;
}
