/*
 * FSM: check for reachability of a given host
 *
 * Copyright (C) 2010-2012 Olaf Kirch <okir@suse.de>
 */
#ifdef HAVE_CONFIG_H
#include "config.h"
#endif


#include <wicked/netinfo.h>
#include <wicked/logging.h>
#include <wicked/wicked.h>
#include <wicked/xml.h>
#include <wicked/socket.h>
#include <wicked/dbus.h>
#include <wicked/objectmodel.h>
#include <wicked/dbus-errors.h>
#include <wicked/modem.h>
#include <wicked/xpath.h>

#include "wicked-client.h"
#include "fsm.h"

/*
 * Data needed for this check
 */
typedef struct ni_reachability_check {
	char *			hostname;
	int			family;

	ni_bool_t		address_valid;
	ni_sockaddr_t		address;
} ni_reachability_check_t;


static ni_bool_t
ni_fsm_require_check_reachable(ni_objectmodel_fsm_t *fsm, ni_ifworker_t *w, ni_fsm_require_t *req)
{
	ni_reachability_check_t *check = req->user_data;

	/* Do not check too often. If the dhcp or routing info didn't change,
	 * there is no point wasting time on another lookup. */
	if (req->event_seq == fsm->last_event_seq[NI_EVENT_ADDRESS_ACQUIRED]) {
		ni_debug_application("check reachability: %s SKIP", check->hostname);
		return FALSE;
	}
	/* Force another lookup if the resolver was updated in the meantime */
	if (req->event_seq < fsm->last_event_seq[NI_EVENT_RESOLVER_UPDATED])
		check->address_valid = FALSE;
	req->event_seq = fsm->event_seq;

	if (!check->address_valid
	 && ni_resolve_hostname_timed(check->hostname, check->family, &check->address, 1) <= 0) {
		ni_debug_application("check reachability: %s not resolvable", check->hostname);
		return FALSE;
	}
	check->address_valid = TRUE;

	if (ni_host_is_reachable(check->hostname, &check->address) <= 0) {
		ni_debug_application("check reachability: %s not reachable at %s",
				check->hostname, ni_address_print(&check->address));
		return FALSE;
	}

	ni_debug_application("check reachability: %s OK", check->hostname);
	return TRUE;
}

static void
ni_ifworker_reachability_check_destroy(ni_fsm_require_t *req)
{
	ni_reachability_check_t *check = req->user_data;

	if (check != NULL) {
		ni_string_free(&check->hostname);
		free(check);
	}

	req->user_data = NULL;
}

ni_fsm_require_t *
ni_ifworker_reachability_check_new(xml_node_t *node)
{
	ni_reachability_check_t *check;
	const char *hostname, *attr;
	int afhint = AF_UNSPEC;
	ni_fsm_require_t *req;

	if (!(hostname = node->cdata))
		return NULL;

	if ((attr = xml_node_get_attr(node, "address-family")) != NULL) {
		if ((afhint = ni_addrfamily_name_to_type(attr)) < 0) {
			ni_error("%s: bad address-family attribute \"%s\"",
					xml_node_location(node), attr);
			return NULL;
		}
	}


	check = calloc(1, sizeof(*check));
	ni_string_dup(&check->hostname, hostname);
	check->family = afhint;

	req = ni_fsm_require_new(ni_fsm_require_check_reachable, ni_ifworker_reachability_check_destroy);
	req->user_data = check;

	return req;
}

