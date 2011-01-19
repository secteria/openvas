/* OpenVAS
* $Id$
* Description: Loads the preferences set in openvassd.conf into the memory.
*
* Authors: - Renaud Deraison <deraison@nessus.org> (Original pre-fork develoment)
*          - Tim Brown <mailto:timb@openvas.org> (Initial fork)
*          - Laban Mwangi <mailto:labanm@openvas.org> (Renaming work)
*          - Tarik El-Yassem <mailto:tarik@openvas.org> (Headers section)
*
* Copyright:
* Portions Copyright (C) 2006 Software in the Public Interest, Inc.
* Based on work Copyright (C) 1998 - 2006 Tenable Network Security, Inc.
*
* This program is free software; you can redistribute it and/or modify
* it under the terms of the GNU General Public License version 2,
* as published by the Free Software Foundation
*
* This program is distributed in the hope that it will be useful,
* but WITHOUT ANY WARRANTY; without even the implied warranty of
* MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
* GNU General Public License for more details.
*
* You should have received a copy of the GNU General Public License
* along with this program; if not, write to the Free Software
* Foundation, Inc., 51 Franklin St, Fifth Floor, Boston, MA 02110-1301 USA.
*/

/** @file
 * 'Server' Preference related functions (some of them scan-related).
 *
 * All the preference getter- functions for pseudo boolean values work in the
 * same fashion.
 * The static 'yes' value is initialized only the first time the function is
 * called. If then preferences != NULL, the arglist is queried, otherwise the
 * value keeps being or is resetted to -1.
 * On subsequent calls where preferences != NULL, the arglist does not have
 * to be queried anymore.
 * Resetting this "cache"s is possible by calling preferences_reset_cache.
 */

#include <unistd.h> /* for close() */
#include <stdio.h>  /* for printf() */
#include <string.h> /* for strstr() */
#include <errno.h>  /* for errno() */
#include <stdlib.h> /* for atoi() */
#include <fcntl.h>  /* for open() */

#include <openvas/hg/hosts_gatherer.h>
#include <openvas/misc/system.h>     /* for efree */

#include "glib.h"
#include "comm.h"
#include "preferences.h"
#include "log.h"
#include "utils.h"


#define inited(x) ((x) >= 0)

/**
 * @brief Initializes the preferences structure
 */
int
preferences_init (char *config_file, struct arglist **prefs)
{
  int result;
  *prefs = emalloc (sizeof (struct arglist));
  result = preferences_process (config_file, *prefs);
  return (result);
}


/**
 * @brief Creates a new preferences file.
 */
int
preferences_new (char *name)
{
  FILE *fd;
  int f;

  if ((f = open (name, O_CREAT | O_RDWR | O_EXCL, 0660)) < 0)
    {
      perror ("preferences_new():open ");
      return (-1);
    }

  fd = fdopen (f, "w");

  fprintf (fd, "# Configuration file of the OpenVAS Security Scanner\n\n\n\n");
  fprintf (fd, "# Every line starting with a '#' is a comment\n\n");

  fprintf (fd, "[Misc]\n\n");

  fprintf (fd, "# Path to the security checks folder : \n");
  fprintf (fd, "plugins_folder = %s\n\n", OPENVAS_NVT_DIR);

  fprintf (fd, "# Path to OpenVAS caching folder: \n");
  fprintf (fd, "cache_folder = %s\n\n", OPENVAS_CACHE_DIR);

  fprintf (fd, "# Path to OpenVAS include directories: \n");
  fprintf (fd, "# (multiple entries are separated with colon ':')\n");
  // Default value is the same directory as the root for  the NVTs
  fprintf (fd, "include_folders = %s\n\n", OPENVAS_NVT_DIR);

  fprintf (fd, "# Maximum number of simultaneous hosts tested : \n");
  fprintf (fd, "max_hosts = 30\n\n");
  fprintf (fd,
           "# Maximum number of simultaneous checks against each host tested : \n");
  fprintf (fd, "max_checks = 10\n\n");
  fprintf (fd,
           "# Niceness. If set to 'yes', openvassd will renice itself to 10.\n");
  fprintf (fd, "be_nice = no\n\n");


  fprintf (fd, "# Log file (or 'syslog') : \n");
  fprintf (fd, "logfile = %s\n\n", OPENVASSD_MESSAGES);
  fprintf (fd,
           "# Shall we log every details of the attack ? (disk intensive)\n");
  fprintf (fd, "log_whole_attack = no\n\n");
  fprintf (fd,
           "# Log the name of the plugins that are loaded by the server ?\n");
  fprintf (fd, "log_plugins_name_at_load = no\n\n");
  fprintf (fd, "# Dump file for debugging output, use `-' for stdout\n");
  fprintf (fd, "dumpfile = %s\n\n", OPENVASSD_DEBUGMSG);
  fprintf (fd, "# Rules file : \n");
  fprintf (fd, "rules = %s\n\n", OPENVASSD_RULES);
  fprintf (fd, "# CGI paths to check for (cgi-bin:/cgi-aws:/ can do)\n");
  fprintf (fd, "cgi_path = /cgi-bin:/scripts\n\n");
  fprintf (fd, "# Range of the ports the port scanners will scan : \n");
  fprintf (fd, "# 'default' means that OpenVAS will scan ports found in its\n");
  fprintf (fd, "# services file.\n");
  fprintf (fd, "port_range = default\n\n");
  fprintf (fd, "# Optimize the test (recommended) : \n");
  fprintf (fd, "optimize_test = yes\n\n");

  fprintf (fd, "\n\n# Optimization : \n");
  fprintf (fd, "# Read timeout for the sockets of the tests : \n");
  fprintf (fd, "checks_read_timeout = 5\n");
  fprintf (fd,
           "# Ports against which two plugins should not be run simultaneously :\n");
  fprintf (fd, "# non_simult_ports = Services/www, 139, Services/finger\n");
  fprintf (fd, "non_simult_ports = 139, 445\n");
  fprintf (fd, "# Maximum lifetime of a plugin (in seconds) : \n");
  fprintf (fd, "plugins_timeout = %d\n", NVT_TIMEOUT);
  fprintf (fd, "\n\n# Safe checks rely on banner grabbing :\n");
  fprintf (fd, "safe_checks = yes\n");
  fprintf (fd,
           "\n\n# Automatically activate the plugins that are depended on\n");
  fprintf (fd, "auto_enable_dependencies = yes\n");
  fprintf (fd,
           "\n\n# Do not echo data from plugins which have been automatically enabled\n");
  fprintf (fd, "silent_dependencies = no\n");
  fprintf (fd,
           "\n\n# Designate hosts by MAC address, not IP address (useful for DHCP networks)\n");
  fprintf (fd, "use_mac_addr = no\n");

  fprintf (fd,
           "\n\n#--- Knowledge base saving (can be configured by the client) :\n");
  fprintf (fd, "# Save the knowledge base on disk : \n");
  fprintf (fd, "save_knowledge_base = no\n");
  fprintf (fd, "# Restore the KB for each test :\n");
  fprintf (fd, "kb_restore = no\n");
  fprintf (fd, "# Only test hosts whose KB we do not have :\n");
  fprintf (fd, "only_test_hosts_whose_kb_we_dont_have = no\n");
  fprintf (fd, "# Only test hosts whose KB we already have :\n");
  fprintf (fd, "only_test_hosts_whose_kb_we_have = no\n");
  fprintf (fd, "# KB test replay :\n");
  fprintf (fd, "kb_dont_replay_scanners = no\n");
  fprintf (fd, "kb_dont_replay_info_gathering = no\n");
  fprintf (fd, "kb_dont_replay_attacks = no\n");
  fprintf (fd, "kb_dont_replay_denials = no\n");
  fprintf (fd, "kb_max_age = 864000\n");
  fprintf (fd, "#--- end of the KB section\n\n");

  fprintf (fd, "\n\n");
  fprintf (fd,
           "# If this option is set, OpenVAS will not scan a network incrementally\n");
  fprintf (fd,
           "# (10.0.0.1, then 10.0.0.2, 10.0.0.3 and so on..) but will attempt to\n");
  fprintf (fd,
           "# slice the workload throughout the whole network (ie: it will scan\n");
  fprintf (fd,
           "# 10.0.0.1, then 10.0.0.127, then 10.0.0.2, then 10.0.0.128 and so on...\n");
  fprintf (fd, "slice_network_addresses = no\n\n");

  fprintf (fd,
           "# Should consider all the NASL scripts as being signed ? (unsafe if set to 'yes')\n");
  fprintf (fd, "nasl_no_signature_check = yes\n\n");
  fprintf (fd,
           "# If this option is set to yes, openvassd will attempt to drop its privileges\n");
  fprintf (fd,
           "# before launching NVTs.\n");
  fprintf (fd, "drop_privileges = no\n\n");
  fprintf (fd,
           "# Settings for vhost scanning.\n");
  fprintf (fd,
           "# You will most likely want to leave the settings empty here and set them through your client.\n");
  fprintf (fd, "vhosts =\n");
  fprintf (fd, "vhosts_ip =\n\n");
  fprintf (fd, "#end.\n");

  fclose (fd);
  close (f);
  return (0);
}


/**
 * @brief Copies the content of the prefs file to a special arglist.
 */
int
preferences_process (char *filename, struct arglist *prefs)
{
  FILE *fd;
  char buffer[1024];
  char *opt, *value;
  if (filename)
    {
      check_symlink (filename);
      if (!(fd = fopen (filename, "r")))
        {
          if (errno == EACCES)
            {
              printf ("The OpenVAS daemon doesn't have the right to read %s\n",
                      filename);
              exit (1);
            }

#ifdef DEBUG
          printf ("Couldn't find any prefs file... Creating a new one...\n");
#endif
          if (preferences_new (filename))
            {
              printf ("Error creating %s\n", filename);
              exit (1);
              arg_add_value (prefs, "plugins_folder", ARG_STRING,
                             strlen ("./plugins"), "./plugins");
              return (1);
            }
          else if (!(fd = fopen (filename, "r")))
            {
              perror ("preferences_process():open ");
              printf ("Could not open %s -- now quitting\n", filename);
              exit (2);
            }
        }

      while (!feof (fd) && fgets (buffer, sizeof (buffer) - 1, fd))
        {
          char *t;
          int len;

          buffer[sizeof (buffer) - 1] = '\0';
          len = strlen (buffer);

          if (buffer[len - 1] == '\n')
            {
              buffer[len - 1] = 0;
              len--;
            }

          if (buffer[0] == '#')
            continue;
          opt = buffer;
          t = strchr (buffer, '=');
          if (t == NULL)
            continue;
          else
            {
              t[0] = 0;
              t += sizeof (char);
              while (t[0] == ' ')
                t += sizeof (char);
              len = strlen (opt);
              while (opt[len - 1] == ' ')
                {
                  opt[len - 1] = '\0';
                  len--;
                }

              len = strlen (t);
              while (t[len - 1] == ' ')
                {
                  t[len - 1] = '\0';
                  len--;
                }

              value = emalloc (len + 1);
              strncpy (value, t, len);
              arg_add_value (prefs, opt, ARG_STRING, strlen (value), value);
#ifdef DEBUGMORE
              printf ("%s = %s\n", opt, value);
#endif
            }
        }
      fclose (fd);
      return (0);
    }
  else
    return (1);
}


int
preferences_get_host_expansion (struct arglist *preferences)
{
  char *pref;
  static int ret = -1;


  if (!preferences)
    {
      ret = -1;
      return -1;
    }

  if (ret >= 0)
    return ret;

  ret = 0;
  pref = arg_get_value (preferences, "host_expansion");
  if (!pref)
    ret = HG_SUBNET;
  else
    {
      if (strstr (pref, "dns"))
        ret = ret | HG_DNS_AXFR;
      if (strstr (pref, "nfs"))
        ret = ret | HG_NFS;
      if (strstr (pref, "ip"))
        ret = ret | HG_SUBNET;
    }

  pref = arg_get_value (preferences, "reverse_lookup");
  if (pref && strstr (pref, "yes"))
    ret = ret | HG_REVLOOKUP;
  return ret;
}

int
preferences_get_slice_network_addresses (struct arglist *preferences)
{
  char *str;

  if (preferences == NULL)
    return 0;

  str = arg_get_value (preferences, "slice_network_addresses");
  if (str == NULL)
    return 0;

  return strcmp (str, "yes") == 0;
}


int
preferences_get_checks_read_timeout (struct arglist *preferences)
{
  char *pref;
  static int ret = -1;

  if (!preferences)
    {
      ret = -1;
      return -1;
    }


  if (ret >= 0)
    return ret;

  pref = arg_get_value (preferences, "checks_read_timeout");
  if (pref)
    {
      ret = atoi (pref);
      if (!ret)
        ret = 15;
    }
  else
    ret = 15;
  return ret;
}

int
preferences_log_whole_attack (struct arglist *preferences)
{
  char *value;
  static int yes = -1;

  if (!preferences)
    {
      yes = -1;
      return -1;
    }


  if (yes >= 0)
    return yes;

  value = arg_get_value (preferences, "log_whole_attack");
  if (value && strcmp (value, "yes"))
    {
      yes = 0;
    }
  else
    yes = 1;

  return yes;
}

int
preferences_optimize_test (struct arglist *preferences)
{
  static int yes = -1;
  char *optimize_asc;

  if (!preferences)
    {
      yes = -1;
      return -1;
    }


  if (yes >= 0)
    return yes;

  optimize_asc = arg_get_value (preferences, "optimize_test");
  if (optimize_asc && !strcmp (optimize_asc, "no"))
    yes = 0;
  else
    yes = 1;

  return yes;
}




int
preferences_log_plugins_at_load (struct arglist *preferences)
{
  static int yes = -1;
  char *pref;

  if (!preferences)
    {
      yes = -1;
      return -1;
    }


  if (yes >= 0)
    return yes;

  pref = arg_get_value (preferences, "log_plugins_name_at_load");
  if (pref && !strcmp (pref, "yes"))
    yes = 1;
  else
    yes = 0;

  return yes;
}

int
preferences_ntp_show_end (struct arglist *preferences)
{
  static int yes = -1;
  char *pref;

  if (!preferences)
    {
      yes = -1;
      return -1;
    }


  if (yes >= 0)
    return yes;

  pref = arg_get_value (preferences, "ntp_opt_show_end");
  if (pref && !strcmp (pref, "yes"))
    yes = 1;
  else
    yes = 0;

  return yes;
}


int
preferences_plugins_timeout (struct arglist *preferences)
{
  static int to = -1;
  char *pref;

  if (!preferences)
    {
      to = -1;
      return -1;
    }


  if (to >= 0)
    return to;

  pref = arg_get_value (preferences, "plugins_timeout");
  if (pref)
    {
      to = atoi (pref);
      if (to == 0)
        to = NVT_TIMEOUT;
    }
  else
    to = NVT_TIMEOUT;

  return to;
}


/**
 * @brief Returns the timeout defined by the client or 0 if none was set.
 *
 * @param preferences Preferences arglist.
 * @param oid         OID of NVT to ask timeout value of.
 *
 * @return 0 if no timeout for the NVT oid was found, timeout in seconds
 *         otherwise.
 */
int
preferences_plugin_timeout (struct arglist *preferences, char *oid)
{
  int ret = 0;
  char *pref_name = g_strdup_printf ("timeout.%s", oid);

  if (arg_get_type (preferences, pref_name) == ARG_STRING)
    {
      int to = atoi (arg_get_value (preferences, pref_name));
      if (to)
        ret = to;
    }

  efree (&pref_name);
  return ret;
}

int
preferences_benice (struct arglist *preferences)
{
  char *pref;
  static int yes = -1;

  if (preferences == NULL)
    {
      return yes;
    }


  if (yes >= 0)
    return yes;

  pref = arg_get_value (preferences, "be_nice");
  if (pref && !strcmp (pref, "yes"))
    yes = 1;
  else
    yes = 0;

  return yes;
}


/**
 * @brief Returns the privilege setting defined by the client or the scanner
 * preference if none was set.
 *
 * @param preferences Preferences arglist.
 * @param oid         OID of NVT to ask privilege setting of. (unused)
 *
 * @return 1 if privileges should be dropped for this NVT, 0 if not.
 */
int
preferences_drop_privileges (struct arglist *preferences, char *oid)
{
  char *pref;
  int ret = 0;

  if (preferences == NULL)
      return ret;

  if (arg_get_type (preferences, "drop_privileges") == ARG_STRING)
    {
      if (strcmp (arg_get_value (preferences, "drop_privileges"), "yes") == 0)
        ret = 1;
    }

  pref = arg_get_value (preferences, "drop_privileges");
  return ret;
}


int
preferences_save_session (struct arglist *preferences)
{
  static int yes = -1;
  char *pref;

  if (!preferences)
    {
      yes = -1;
      return -1;
    }


  if (yes >= 0)
    return yes;

  pref = arg_get_value (preferences, "save_session");
  if (pref && !strcmp (pref, "yes"))
    yes = 1;
  else
    yes = 0;

  return yes;
}

int
preferences_save_empty_sessions (struct arglist *preferences)
{
  static int yes = -1;
  char *pref;

  if (!preferences)
    {
      yes = -1;
      return -1;
    }


  if (yes >= 0)
    return yes;

  pref = arg_get_value (preferences, "save_empty_sessions");
  if (pref && !strcmp (pref, "yes"))
    yes = 1;
  else
    yes = 0;

  return yes == 1;
}


int
preferences_autoload_dependencies (struct arglist *preferences)
{
  static int yes = -1;
  char *pref;

  if (!preferences)
    {
      yes = -1;
      return -1;
    }


  if (yes >= 0)
    return yes;

  pref = arg_get_value (preferences, "auto_enable_dependencies");
  if (pref && !strcmp (pref, "yes"))
    yes = 1;
  else
    yes = 0;

  return yes;
}



int
preferences_safe_checks_enabled (struct arglist *preferences)
{
  static int yes = -1;
  char *value;

  if (!preferences)
    {
      yes = -1;
      return -1;
    }


  if (yes >= 0)
    return yes;
  value = arg_get_value (preferences, "safe_checks");
  if (value && !strcmp (value, "yes"))
    yes = 1;
  else
    yes = 0;

  return yes;
}


int
preferences_use_mac_addr (struct arglist *preferences)
{
  static int yes = -1;
  char *value;

  if (!preferences)
    {
      yes = -1;
      return -1;
    }


  if (yes >= 0)
    return yes;

  value = arg_get_value (preferences, "use_mac_addr");
  if (value && !strcmp (value, "yes"))
    yes = 1;
  else
    yes = 0;

  return yes;
}

int
preferences_nasl_no_signature_check (struct arglist *preferences)
{
  static int yes = -1;
  char *pref;

  if (!preferences)
    {
      yes = -1;
      return -1;
    }


  if (yes >= 0)
    return yes;


  pref = arg_get_value (preferences, "nasl_no_signature_check");
  if (pref && !strcmp (pref, "yes"))
    yes = 1;
  else
    yes = 0;

  return yes;
}

int
preferences_report_killed_plugins (struct arglist *preferences)
{
  static int yes = -1;
  char *pref;

  if (!preferences)
    {
      yes = -1;
      return -1;
    }


  if (yes >= 0)
    return yes;

  pref = arg_get_value (preferences, "report_killed_plugins");
  if ((!pref) || strcmp (pref, "yes"))
    yes = 0;
  else
    yes = 1;

  return yes;
}

int
preferences_silent_dependencies (struct arglist *preferences)
{
  static int yes = -1;
  char *pref;

  if (!preferences)
    {
      yes = -1;
      return -1;
    }


  if (yes >= 0)
    return yes;


  pref = arg_get_value (preferences, "silent_dependencies");
  if (pref && !strcmp (pref, "yes"))
    yes = 1;
  else
    yes = 0;

  return yes;
}

int
preferences_network_scan (struct arglist *preferences)
{
  static int yes = -1;
  char *pref;

  if (!preferences)
    {
      yes = -1;
      return -1;
    }


  if (yes >= 0)
    return yes;


  pref = arg_get_value (preferences, "network_scan");
  if (pref && !strcmp (pref, "yes"))
    yes = 1;
  else
    yes = 0;

  return yes;
}

/**
 * @return NULL if pref is set to "no", preference value otherwise.
 */
char *
preferences_get_string (struct arglist *preferences, char *name)
{
  char *pref = arg_get_value (preferences, name);

  if (pref && pref[0] != '\0' && strcmp (pref, "no"))
    return pref;
  else
    return NULL;
}


/**
 * @brief Resets the preference caches.
 *
 * Subsequent calls to the pseudo-boolean preference getters like
 * preferences_silent_dependencies will query a given arglist once and refill
 * the caches.
 */
void
preferences_reset_cache ()
{
  preferences_get_host_expansion (NULL);
  preferences_get_checks_read_timeout (NULL);
  preferences_log_whole_attack (NULL);
  preferences_report_killed_plugins (NULL);
  preferences_optimize_test (NULL);
  preferences_ntp_show_end (NULL);
  preferences_log_plugins_at_load (NULL);
  preferences_plugins_timeout (NULL);
  preferences_benice (NULL);
  preferences_autoload_dependencies (NULL);
  preferences_safe_checks_enabled (NULL);
  preferences_use_mac_addr (NULL);
  preferences_save_session (NULL);
  preferences_save_empty_sessions (NULL);
  preferences_silent_dependencies (NULL);
}