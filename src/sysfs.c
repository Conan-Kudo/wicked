/*
 * Routines for reading from sysfs files
 *
 * Copyright (C) 2009-2010 Olaf Kirch <okir@suse.de>
 */
#include <stdlib.h>
#include <string.h>
#include <stdarg.h>
#include <stdio.h>
#include <limits.h>

#include <wicked/netinfo.h>
#include <wicked/logging.h>
#include "sysfs.h"

#define _PATH_SYS_CLASS_NET	"/sys/class/net"

static const char *	__ni_sysfs_netif_attrpath(const char *ifname, const char *attr);
static const char *	__ni_sysfs_netif_get_attr(const char *ifname, const char *attr);
static int		__ni_sysfs_printf(const char *, const char *, ...);
static int		__ni_sysfs_read_list(const char *, ni_string_array_t *);
static int		__ni_sysfs_read_string(const char *, char **);

int
ni_sysfs_netif_get_int(const char *ifname, const char *attr_name, int *result)
{
	const char *attr;

	attr = __ni_sysfs_netif_get_attr(ifname, attr_name);
	if (!attr)
		return -1;

	*result = strtol(attr, NULL, 0);
	return 0;
}

int
ni_sysfs_netif_get_string(const char *ifname, const char *attr_name, char **result)
{
	const char *attr;

	attr = __ni_sysfs_netif_get_attr(ifname, attr_name);
	if (!attr)
		return -1;

	ni_string_dup(result, attr);
	return 0;
}

static const char *
__ni_sysfs_netif_get_attr(const char *ifname, const char *attr_name)
{
	static char buffer[256];
	const char *filename;
	char *result = NULL;
	FILE *fp;

	filename = __ni_sysfs_netif_attrpath(ifname, attr_name);
	if (!(fp = fopen(filename, "r")))
		return NULL;

	if (fgets(buffer, sizeof(buffer), fp) != NULL) {
		buffer[strcspn(buffer, "\n")] = '\0';
		result = buffer;
	}
	fclose(fp);
	return result;
}

static const char *
__ni_sysfs_netif_attrpath(const char *ifname, const char *attr_name)
{
	static char pathbuf[PATH_MAX];

	snprintf(pathbuf, sizeof(pathbuf), "%s/%s/%s",
			_PATH_SYS_CLASS_NET, ifname, attr_name);
	return pathbuf;
}

/*
 * Bonding support
 */
int
ni_sysfs_bonding_available(void)
{
	return ni_file_exists("/sys/class/net/bonding_masters");
}

int
ni_sysfs_bonding_get_masters(ni_string_array_t *list)
{
	return __ni_sysfs_read_list("/sys/class/net/bonding_masters", list);
}

int
ni_sysfs_bonding_add_master(const char *ifname)
{
	return __ni_sysfs_printf("/sys/class/net/bonding_masters", "+%s\n", ifname);
}

int
ni_sysfs_bonding_is_master(const char *ifname)
{
	return ni_file_exists(__ni_sysfs_netif_attrpath(ifname, "bonding"));
}

int
ni_sysfs_bonding_delete_master(const char *ifname)
{
	return __ni_sysfs_printf("/sys/class/net/bonding_masters", "-%s\n", ifname);
}

int
ni_sysfs_bonding_get_slaves(const char *master, ni_string_array_t *list)
{
	return __ni_sysfs_read_list(__ni_sysfs_netif_attrpath(master, "bonding/slaves"), list);
}

int
ni_sysfs_bonding_add_slave(const char *master, const char *slave)
{
	return __ni_sysfs_printf(__ni_sysfs_netif_attrpath(master, "bonding/slaves"), "+%s", slave);
}

int
ni_sysfs_bonding_delete_slave(const char *master, const char *slave)
{
	return __ni_sysfs_printf(__ni_sysfs_netif_attrpath(master, "bonding/slaves"), "-%s", slave);
}

int
ni_sysfs_bonding_get_arp_targets(const char *master, ni_string_array_t *result)
{
	return __ni_sysfs_read_list(__ni_sysfs_netif_attrpath(master, "bonding/arp_ip_target"), result);
}

int
ni_sysfs_bonding_add_arp_target(const char *master, const char *ipaddress)
{
	return __ni_sysfs_printf(__ni_sysfs_netif_attrpath(master, "bonding/arp_ip_target"), "+%s\n", ipaddress);
}

int
ni_sysfs_bonding_delete_arp_target(const char *master, const char *ipaddress)
{
	return __ni_sysfs_printf(__ni_sysfs_netif_attrpath(master, "bonding/arp_ip_target"), "-%s\n", ipaddress);
}

int
ni_sysfs_bonding_get_attr(const char *ifname, const char *attr_name, char **result)
{
	static char pathbuf[PATH_MAX];

	snprintf(pathbuf, sizeof(pathbuf), "%s/%s/bonding/%s", _PATH_SYS_CLASS_NET, ifname, attr_name);
	return __ni_sysfs_read_string(pathbuf, result);
}

int
ni_sysfs_bonding_set_attr(const char *ifname, const char *attr_name, const char *attr_value)
{
	static char pathbuf[PATH_MAX];

	snprintf(pathbuf, sizeof(pathbuf), "%s/%s/bonding/%s", _PATH_SYS_CLASS_NET, ifname, attr_name);
	return __ni_sysfs_printf(pathbuf, "%s", attr_value);
}

int
ni_sysfs_bonding_set_list_attr(const char *ifname, const char *attr_name, const ni_string_array_t *list)
{
	static char pathbuf[PATH_MAX];
	ni_string_array_t current, delete, add, unchanged;
	unsigned int i;
	int rv = -1;

	snprintf(pathbuf, sizeof(pathbuf), "%s/%s/bonding/%s", _PATH_SYS_CLASS_NET, ifname, attr_name);

	ni_string_array_init(&current);
	if (__ni_sysfs_read_list(pathbuf, &current) < 0)
		return -1;

	ni_string_array_init(&delete);
	ni_string_array_init(&add);
	ni_string_array_init(&unchanged);

	ni_string_array_comm(&current, list,
			&delete,	/* unique to 1st array */
			&add,		/* unique to 2nd array */
			&unchanged);	/* common to both */

	if (add.count == 0 && delete.count == 0) {
		ni_debug_ifconfig("%s: attr list %s unchanged", ifname, attr_name);
		rv = 0;
		goto done;
	}

	if (ni_debug & NI_TRACE_IFCONFIG) {
		ni_trace("%s: updating attr list %s", ifname, attr_name);
		for (i = 0; i < delete.count; ++i)
			ni_trace("    remove %s", delete.data[i]);
		for (i = 0; i < add.count; ++i)
			ni_trace("    add %s", add.data[i]);
		for (i = 0; i < unchanged.count; ++i)
			ni_trace("    leave %s", add.data[i]);
	}

	for (i = 0; i < delete.count; ++i) {
		if (__ni_sysfs_printf(pathbuf, "-%s\n", delete.data[i]) < 0) {
			ni_error("%s: could not remove %s %s",
					ifname, attr_name,
					delete.data[i]);
			goto done;
		}
	}

	for (i = 0; i < add.count; ++i) {
		if (__ni_sysfs_printf(pathbuf, "+%s\n", add.data[i]) < 0) {
			ni_error("%s: could not add %s %s",
					ifname, attr_name,
					add.data[i]);
			goto done;
		}
	}

	rv = 0;

done:
	ni_string_array_init(&current);
	ni_string_array_init(&delete);
	ni_string_array_init(&add);
	ni_string_array_init(&unchanged);
	return rv;
}

/*
 * Print a value to a sysfs file
 */
static int
__ni_sysfs_printf(const char *pathname, const char *fmt, ...)
{
	va_list ap;
	FILE *fp;

	if ((fp = fopen(pathname, "w")) == NULL) {
		ni_error("unable to open %s: %m", pathname);
		return -1;
	}

	va_start(ap, fmt);
	vfprintf(fp, fmt, ap);
	va_end(ap);

	if (fclose(fp) < 0) {
		ni_error("error writing to %s: %m", pathname);
		return -1;
	}

	return 0;
}

/*
 * Read a list of values from a sysfs file
 */
static int
__ni_sysfs_read_list(const char *pathname, ni_string_array_t *result)
{
	char buffer[256];
	FILE *fp;

	if ((fp = fopen(pathname, "r")) == NULL) {
		ni_error("unable to open %s: %m", pathname);
		return -1;
	}

	while (fgets(buffer, sizeof(buffer), fp) != NULL) {
		char *s;

		for (s = strtok(buffer, " \t\n"); s; s = strtok(NULL, " \t\n"))
			ni_string_array_append(result, s);
	}
	fclose(fp);
	return 0;
}

static int
__ni_sysfs_read_string(const char *pathname, char **result)
{
	char buffer[256];
	FILE *fp;

	if (!(fp = fopen(pathname, "r")))
		return -1;

	ni_string_free(result);

	if (fgets(buffer, sizeof(buffer), fp) != NULL) {
		buffer[strcspn(buffer, "\n")] = '\0';
		ni_string_dup(result, buffer);
	}
	fclose(fp);
	return 0;
}
