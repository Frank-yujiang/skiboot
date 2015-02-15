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

#define _GNU_SOURCE         /* for aspritnf */
#include <errno.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <fcntl.h>
#include <stdlib.h>
#include <stdio.h>
#include <unistd.h>
#include <stdbool.h>
#include <stdint.h>
#include <dirent.h>
#include <sys/param.h>
#include <string.h>
#include <sys/ioctl.h>
#include <linux/i2c.h>
#include <linux/i2c-dev.h>
#include <ccan/list/list.h>

#include "i2c.h"

struct i2c_bus {
	uint32_t		chip_id;
	uint8_t			engine;
	uint8_t			port;
	const char		*devpath;
	int			fd;
	struct list_node	link;
};

static struct list_head bus_list = LIST_HEAD_INIT(bus_list);

static int i2c_get_dev(uint32_t chip, uint8_t eng, uint8_t port, uint16_t dev)
{
	struct i2c_bus *b, *bus = NULL;

	list_for_each(&bus_list, b, link) {
		if (b->chip_id == chip && b->engine == eng && b->port == port) {
			bus = b;
			break;
		}
	}
	if (!bus) {
		printf("I2C: Bus %08x/%d/%d not found\n", chip, eng, port);
		return -1;
	}
	if (bus->fd < 0) {
		bus->fd = open(bus->devpath, O_RDWR);
		if (bus->fd < 0) {
			fprintf(stderr, "Failed to open %s: %s\n",
				bus->devpath, strerror(errno));
			return -1;
		}
	}

	/* XXX We could use the I2C_SLAVE ioctl to check if the device
	 * is currently in use by a kernel driver...
	 */

	return bus->fd;
}

int i2c_read(uint32_t chip_id, uint8_t engine, uint8_t port,
	     uint16_t device, uint32_t offset_size, uint32_t offset,
	     uint32_t length, void* data)
{
	struct i2c_rdwr_ioctl_data ioargs;
	struct i2c_msg	msgs[2];
	uint8_t obuf[4];
	int fd, i, midx = 0;

	if (offset_size > 4) {
		fprintf(stderr,"I2C: Invalid offset_size %d\n", offset_size);
		return -1;
	}
	fd = i2c_get_dev(chip_id, engine, port, device);
	if (fd == -1)
		return -1;

	/* If we have an offset, build a message for it */
	if (offset_size) {
		/* The offset has a variable size so let's handle this properly
		 * as it has to be laid out in memory MSB first
		 */
		for (i = 0; i < offset_size; i++)
			obuf[i] = offset >> (8 * (offset_size - i - 1));
		msgs[0].addr = device;
		msgs[0].flags = 0;
		msgs[0].buf = obuf;
		msgs[0].len = offset_size;
		midx = 1;
	}

	/* Build the message for the data portion */
	msgs[midx].addr = device;
	msgs[midx].flags = I2C_M_RD;
	msgs[midx].buf = data;
	msgs[midx].len = length;
	midx++;

	ioargs.msgs = msgs;
	ioargs.nmsgs = midx;
	if (ioctl(fd, I2C_RDWR, &ioargs) < 0) {
		fprintf(stderr, "I2C: Read error: %s\n", strerror(errno));
		return -1;
	}
	printf("I2C: Read from %08x:%d:%d@%02x+0x%x %d bytes ok\n",
	       chip_id, engine, port, device, offset_size ? offset : 0, length);

	return 0;
}

int i2c_write(uint32_t chip_id, uint8_t engine, uint8_t port,
	      uint16_t device, uint32_t offset_size, uint32_t offset,
	      uint32_t length, void* data)
{
	struct i2c_rdwr_ioctl_data ioargs;
	struct i2c_msg msg;
	int fd, size, i, rc;
	uint8_t *buf;

	if (offset_size > 4) {
		fprintf(stderr,"I2C: Invalid offset_size %d\n", offset_size);
		return -1;
	}
	fd = i2c_get_dev(chip_id, engine, port, device);
	if (fd == -1)
		return -1;

	/* Not all kernel driver versions support breaking up a write into
	 * two components (offset, data), so we coalesce them first and
	 * issue a single write. The offset is layed out in BE format.
	 */
	size = offset_size + length;
	buf = malloc(size);
	if (!buf) {
		fprintf(stderr, "I2C: Out of memory !\n");
		return -1;
	}

	/* The offset has a variable size so let's handle this properly
	 * as it has to be laid out in memory MSB first
	 */
	for (i = 0; i < offset_size; i++)
		buf[i] = offset >> (8 * (offset_size - i - 1));

	/* Copy the remaining data */
	memcpy(buf + offset_size, data, length);

	/* Build the message */
	msg.addr = device;
	msg.flags = 0;
	msg.buf = buf;
	msg.len = size;
	ioargs.msgs = &msg;
	ioargs.nmsgs = 1;
	rc = ioctl(fd, I2C_RDWR, &ioargs);
	free(buf);
	if (rc < 0) {
		fprintf(stderr, "I2C: Write error: %s\n", strerror(errno));
		return -1;
	}

	return 0;
}

static void i2c_add_bus(uint32_t chip, uint32_t engine, uint32_t port,
			const char *devname)
{
	struct i2c_bus *b = malloc(sizeof(struct i2c_bus));
	char *dn;

	if (asprintf(&dn, "/dev/%s", devname) < 0) {
		fprintf(stderr, "Error creating devpath for %s: %s\n",
			devname, strerror(errno));
		return;
	}

	memset(b, 0, sizeof(*b));
	b->chip_id = chip;
	b->engine = engine;
	b->port = port;
	b->devpath = dn;
	b->fd = -1;
	list_add(&bus_list, &b->link);
}

void i2c_init(void)
{
#define SYSFS	"/sys"	/* XXX Find it ? */
	DIR *devsdir;
	struct dirent *devent;
	char dpath[NAME_MAX];
	char busname[256];
	char *s;
	FILE *f;
	unsigned int chip, engine, port;

	/* Ensure i2c-dev is loaded (must be root ! might need to
	 * move that to some helper script or something ...)
	 */
	system("modprobe i2c-dev");

	/* Get directory of i2c char devs in sysfs */
	devsdir = opendir(SYSFS "/class/i2c-dev");
	if (!devsdir) {
		fprintf(stderr, "Error opening " SYSFS "/class/i2c-dev: %s\n",
			strerror(errno));
	}
	while ((devent = readdir(devsdir)) != NULL) {
		if (!strcmp(devent->d_name, "."))
			continue;
		if (!strcmp(devent->d_name, ".."))
			continue;

		/* Get bus name */
		sprintf(dpath, SYSFS "/class/i2c-dev/%s/name", devent->d_name);
		f = fopen(dpath, "r");
		if (!f) {
			fprintf(stderr, "Can't open %s: %s, skipping...\n",
				dpath, strerror(errno));
			continue;
		}
		s = fgets(busname, sizeof(busname), f);
		fclose(f);
		if (!s) {
			fprintf(stderr, "Failed to read %s, skipping...\n",
				dpath);
			continue;
		}

		/* Is this a P8 or Centaur i2c bus ? No -> move on */
		if (strncmp(s, "p8_", 3) == 0)
			sscanf(s, "p8_%x_e%dp%d", &chip, &engine, &port);
		else if (strncmp(s, "cen_", 4) == 0)
			sscanf(s, "cen_%x_e%dp%d", &chip, &engine, &port);
		else
			continue;

		printf("I2C: Found Chip: %08x engine %d port %d\n",
		       chip, engine, port);
		i2c_add_bus(chip, engine, port, devent->d_name);
	}
	closedir(devsdir);
}

