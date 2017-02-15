/* OpenVAS
* $Id$
* Description: Manages the launching of plugins within processes.
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

#include <stdio.h>    /* for perror() */
#include <stdlib.h>   /* for atoi() */
#include <unistd.h>   /* for close() */
#include <sys/wait.h> /* for waitpid() */
#include <strings.h>  /* for bzero() */
#include <errno.h>    /* for errno() */
#include <sys/time.h> /* for gettimeofday() */
#include <string.h>

#include <gvm/base/prefs.h>          /* for prefs_get_bool() */
#include <gvm/util/nvticache.h>

#include <openvas/misc/network.h>    /* for internal_send */
#include <openvas/misc/nvt_categories.h>  /* for ACT_SCANNER */
#include <openvas/misc/internal_com.h>  /* for INTERNAL_COMM_MSG_TYPE_DATA */

#include "pluginload.h"
#include "utils.h"
#include "sighand.h"
#include "processes.h"
#include "pluginscheduler.h"
#include "plugs_req.h"

#undef G_LOG_DOMAIN
/**
 * @brief GLib log domain.
 */
#define G_LOG_DOMAIN "sd   main"

/**
 * @brief 'Hard' limit of the max. number of concurrent plugins per host.
 */
#define MAX_PROCESSES 32

#undef DEBUG_CONFLICTS

/**
 * @brief Structure to represent a process in the sense of a running NVT.
 */
struct running
{
  struct scheduler_plugin *plugin;
  struct timeval start;
  int pid;                   /**< Process ID. */
  int timeout;               /**< Timeout after which to kill process
                              * (NVT preference). If -1, never kill. it*/
  int upstream_soc;
  int internal_soc;          /**< 'Input' socket for this process */
  int alive;                 /**< 0 if dead. */
};

static void read_running_processes ();
static void update_running_processes ();

static struct running processes[MAX_PROCESSES];
static int num_running_processes;
static int max_running_processes;
static int old_max_running_processes;
static GSList *non_simult_ports = NULL;
const char *hostname = NULL;


/**
 *
 * @param p  Process index in processes array (function refers to processes[p]).
 *
 * @return -1 if plugin died
 */
static int
process_internal_msg (int p)
{
  int e = 0, bufsz = 0, type = 0;
  char *buffer = NULL;

  e = internal_recv (processes[p].internal_soc, &buffer, &bufsz, &type);
  if (e < 0)
    {
      g_debug ("Process %d (OID: %s) seems to have died too early",
                 processes[p].pid, processes[p].plugin->oid);
      processes[p].alive = 0;
      return -1;
    }

  if (type & INTERNAL_COMM_MSG_TYPE_DATA)
    {
      e = internal_send (processes[p].upstream_soc, buffer, type);
    }
  else if (type & INTERNAL_COMM_MSG_TYPE_CTRL)
    {
      if (type & INTERNAL_COMM_CTRL_FINISHED)
        {
          kill (processes[p].pid, SIGTERM);
          processes[p].alive = 0;
        }
    }
  else
    g_debug ("Received unknown message type %d", type);

  g_free (buffer);
  return e;
}


void
wait_for_children ()
{
  int i;

  for (i = 0; i < MAX_PROCESSES; i++)
    if (processes[i].pid != 0)
      {
        int ret;
        do
          {
            ret = waitpid (-1, NULL, WNOHANG);
          }
        while (ret < 0 && errno == EINTR);
      }
}

/**
 *
 */
static void
update_running_processes (void)
{
  int i;
  struct timeval now;
  int log_whole =  prefs_get_bool ("log_whole_attack");

  gettimeofday (&now, NULL);

  if (num_running_processes == 0)
    return;

  for (i = 0; i < MAX_PROCESSES; i++)
    {
      if (processes[i].pid > 0)
        {
          // If process dead or timed out
          if (processes[i].alive == 0
              || (processes[i].timeout > 0
                  && ((now.tv_sec - processes[i].start.tv_sec) >
                      processes[i].timeout)))
            {
              char *oid = processes[i].plugin->oid;

              if (processes[i].alive)
                {
                  gchar *msg;

                  if (log_whole)
                    g_message ("%s (pid %d) is slow to finish - killing it",
                               oid, processes[i].pid);

                  msg = g_strdup_printf
                         ("SERVER <|> ERRMSG <|> %s <|> general/tcp"
                          " <|> NVT timed out after %d seconds."
                          " <|> %s <|> SERVER\n",
                          hostname, processes[i].timeout, oid ?: "0");
                  internal_send (processes[i].upstream_soc,
                                 msg, INTERNAL_COMM_MSG_TYPE_DATA);
                  g_free (msg);

                  terminate_process (processes[i].pid);
                  processes[i].alive = 0;
                }
              else
                {
                  struct timeval old_now = now;
                  int e;
                  if (now.tv_usec < processes[i].start.tv_usec)
                    {
                      processes[i].start.tv_sec++;
                      now.tv_usec += 1000000;
                    }
                  if (log_whole)
                    {
                      char *name = nvticache_get_name (oid);
                      g_message
                        ("%s (%s) [%d] finished its job in %ld.%.3ld seconds",
                         name, oid, processes[i].pid,
                         (long) (now.tv_sec - processes[i].start.tv_sec),
                         (long) ((now.tv_usec -
                                  processes[i].start.tv_usec) / 1000));
                      g_free (name);
                    }
                  now = old_now;
                  do
                    {
                      e = waitpid (processes[i].pid, NULL, 0);
                    }
                  while (e < 0 && errno == EINTR);

                }
              num_running_processes--;
              processes[i].plugin->running_state = PLUGIN_STATUS_DONE;
              close (processes[i].internal_soc);
              bzero (&(processes[i]), sizeof (processes[i]));
            }
        }
    }
}

static int
common (GSList *list1, GSList *list2)
{
  if (!list1 || !list2)
    return 0;

  while (list1)
    {
      GSList *tmp = list2;
      while (tmp)
        {
          if (!strcmp (list1->data, tmp->data))
            return 1;
          tmp = tmp->next;
        }
      list1 = list1->next;
    }
  return 0;
}

static GSList *
required_ports_in_list (const char *oid, GSList *list)
{
  GSList *common_ports = NULL;
  char **array, *ports;
  int i;

  if (!oid || !list)
    return 0;
  ports = nvticache_get_required_ports (oid);
  if (!ports)
    return 0;
  array = g_strsplit (ports, ", ", 0);
  g_free (ports);
  if (!array)
    return 0;

  for (i = 0; array[i]; i++)
    {
      GSList *tmp = list;
      while (tmp)
        {
          if (!strcmp (tmp->data, array[i]))
            common_ports = g_slist_prepend (common_ports, g_strdup (tmp->data));
          tmp = tmp->next;
        }
    }

  g_strfreev (array);
  return common_ports;
}

static void
wait_if_simult_ports (int pid, const char *oid, const char *next_oid)
{
  GSList *common_ports1 = NULL, *common_ports2 = NULL;

  common_ports1 = required_ports_in_list (oid, non_simult_ports);
  if (common_ports1)
    common_ports2 = required_ports_in_list (next_oid, non_simult_ports);
  if (common_ports1 && common_ports2 && common (common_ports1, common_ports2))
    {
#ifdef DEBUG_CONFLICT
      g_debug ("Waiting has been initiated...\n");
      g_debug ("Ports in common - waiting...");
#endif
      while (process_alive (pid))
        {
          read_running_processes ();
          update_running_processes ();
          wait_for_children ();
        }
#ifdef DEBUG_CONFLICT
       g_debug ("End of the wait - was that long ?\n");
#endif
    }
  g_slist_free_full (common_ports1, g_free);
  g_slist_free_full (common_ports2, g_free);
}

/**
 * If another NVT with same port requirements is running, wait.
 *
 * @return -1 if MAX_PROCESSES are running, the index of the first free "slot"
 *          in the processes array otherwise.
 */
static int
next_free_process (struct scheduler_plugin *upcoming)
{
  int r;

  wait_for_children ();
  for (r = 0; r < MAX_PROCESSES; r++)
    {
      if (processes[r].pid > 0)
        wait_if_simult_ports (processes[r].pid, processes[r].plugin->oid,
                              upcoming->oid);
    }
  for (r = 0; r < MAX_PROCESSES; r++)
    if (processes[r].pid <= 0)
      return r;
  return -1;
}

/**
 *
 */
static void
read_running_processes (void)
{
  int i;
  int flag = 0;
  struct timeval tv;
  fd_set rd;
  int max = 0;
  int e;

  if (num_running_processes == 0)
    return;

  FD_ZERO (&rd);
  for (i = 0; i < MAX_PROCESSES; i++)
    {
      if (processes[i].pid > 0)
        {
          FD_SET (processes[i].internal_soc, &rd);
          if (processes[i].internal_soc > max)
            max = processes[i].internal_soc;
        }
    }

  do
    {
      tv.tv_sec = 0;
      tv.tv_usec = 500000;
      e = select (max + 1, &rd, NULL, NULL, &tv);
    }
  while (e < 0 && errno == EINTR);

  if (e == 0)
    return;

  for (i = 0; i < MAX_PROCESSES; i++)
    {
      if (processes[i].pid > 0)
        {
          flag++;
          if (FD_ISSET (processes[i].internal_soc, &rd) != 0)
            {
              int result = process_internal_msg (i);
              if (result)
                {
#ifdef DEBUG
                  g_debug ("process_internal_msg for %s returned %d",
                             processes[i].plugin->oid, result);
#endif
                }
            }
        }
    }

  if (flag == 0 && num_running_processes != 0)
    num_running_processes = 0;
}


void
pluginlaunch_init (const char *host)
{
  int i;

  char **split = g_strsplit (prefs_get ("non_simult_ports"), ", ", 0);
  for (i = 0; split[i]; i++)
    non_simult_ports = g_slist_prepend (non_simult_ports, g_strdup (split[i]));
  g_strfreev (split);
  max_running_processes = get_max_checks_number ();
  old_max_running_processes = max_running_processes;
  hostname = host;

  if (max_running_processes >= MAX_PROCESSES)
    {
      g_debug
        ("max_checks (%d) > MAX_PROCESSES (%d) - modify openvas-scanner/openvassd/pluginlaunch.c",
         max_running_processes, MAX_PROCESSES);
      max_running_processes = MAX_PROCESSES - 1;
    }


  num_running_processes = 0;
  bzero (&(processes), sizeof (processes));
}

void
pluginlaunch_disable_parrallel_checks (void)
{
  max_running_processes = 1;
}

void
pluginlaunch_enable_parrallel_checks (void)
{
  max_running_processes = old_max_running_processes;
}


void
pluginlaunch_stop (int soft_stop)
{
  int i;

  if (soft_stop)
    {
      read_running_processes ();

      for (i = 0; i < MAX_PROCESSES; i++)
        {
          if (processes[i].pid > 0)
            kill (processes[i].pid, SIGTERM);
        }
      usleep (20000);
    }

  for (i = 0; i < MAX_PROCESSES; i++)
    {
      if (processes[i].pid > 0)
        {
          kill (processes[i].pid, SIGKILL);
          num_running_processes--;
          processes[i].plugin->running_state = PLUGIN_STATUS_DONE;
          close (processes[i].internal_soc);
          bzero (&(processes[i]), sizeof (struct running));
        }
    }
}


/**
 * @return PID of process that is connected to the plugin as returned by plugin
 *         classes pl_launch function (<=0 means there was a problem).
 */
int
plugin_launch (struct arglist *globals, struct scheduler_plugin *plugin,
               struct host_info *hostinfo, kb_t kb, char *name)
{
  int p;
  int dsoc[2];

  /* Wait for a free slot while reading the input from the plugins  */
  while (num_running_processes >= max_running_processes)
    {
      read_running_processes ();
      update_running_processes ();
    }

  p = next_free_process (plugin);
  processes[p].plugin = plugin;
  processes[p].timeout = prefs_nvt_timeout (plugin->oid);
  if (processes[p].timeout == 0)
    processes[p].timeout = nvticache_get_timeout (plugin->oid);

  if (processes[p].timeout == 0)
    {
      int category = nvticache_get_category (plugin->oid);
      if (category == ACT_SCANNER)
        processes[p].timeout = atoi (prefs_get ("scanner_plugins_timeout")
                                     ?: "-1");
      else
        processes[p].timeout = atoi (prefs_get ("plugins_timeout") ?: "-1");
    }

  if (socketpair (AF_UNIX, SOCK_STREAM, 0, dsoc) < 0)
    {
      perror ("pluginlaunch.c:plugin_launch:socketpair(1) ");
    }
  gettimeofday (&(processes[p].start), NULL);

  processes[p].upstream_soc = arg_get_value_int (globals, "global_socket");
  processes[p].internal_soc = dsoc[0];

  processes[p].pid =
    nasl_plugin_launch (globals, hostinfo, kb, name, plugin->oid, dsoc[1]);

  processes[p].alive = 1;
  close (dsoc[1]);
  if (processes[p].pid > 0)
    num_running_processes++;
  else
    processes[p].plugin->running_state = PLUGIN_STATUS_UNRUN;

  return processes[p].pid;
}


/**
 * @brief Waits and 'pushes' processes until num_running_processes is 0.
 */
void
pluginlaunch_wait (void)
{
  do
    {
      wait_for_children ();
      read_running_processes ();
      update_running_processes ();
    }
  while (num_running_processes != 0);
}

/**
 * @brief Cleanup file descriptors used by the processes array. To be called by
 *        the child process running the plugin.
 */
void
pluginlaunch_child_cleanup (void)
{
  int i;
  for (i = 0; i < MAX_PROCESSES; i++)
    if (processes[i].internal_soc)
      close (processes[i].internal_soc);
}

/**
 * @brief Waits and 'pushes' processes until the number of running processes has
 *        changed.
 */
void
pluginlaunch_wait_for_free_process (void)
{
  int num = num_running_processes;
  do
    {
      wait_for_children ();
      read_running_processes ();
      update_running_processes ();
    }
  while (num_running_processes == num);
}
