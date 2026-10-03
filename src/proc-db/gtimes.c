/*                         G T I M E S . C
 * BRL-CAD
 *
 * Copyright (c) 2016-2026 United States Government as represented by
 * the U.S. Army Research Laboratory.
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public License
 * version 2.1 as published by the Free Software Foundation.
 *
 * This library is distributed in the hope that it will be useful, but
 * WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with this file; see the file named COPYING for more
 * information.
 */

#include "common.h"

#include <stdio.h>
#include <string.h>
#include <sys/stat.h>

#include "bu.h"
#include "ged.h"

int
main(int ac, char *av[])
{
  int64_t begin = bu_gettime();
  int64_t timer;
  double seconds[128] = {0};
  int c;
  int i;
  int iterations = 1;
  const int SKIP = 1000;
  const char *USAGE = "Usage: %s [-i iterations] file.g\n";
  const char *progname = (ac > 0 && av && av[0]) ? av[0] : "gtimes";

  struct db_i *dbip;

  if (ac > 0 && av && av[0])
    bu_setprogname(av[0]);

  if (ac < 2) {
    bu_log(USAGE, progname);
    return 1;
  }

  while ((c = bu_getopt(ac, av, "h?i:")) != -1) {
    switch (c) {
      case 'h':
      case '?':
        bu_log(USAGE, progname);
        return 0;
      case 'i':
        if (bu_sscanf(bu_optarg, "%d", &iterations) != 1 || iterations < 1)
          iterations = 1;
        break;
      default:
        bu_log("ERROR: unexpected [%c] option\n", c);
        bu_log(USAGE, progname);
        return 1;
    }
  }

  if (bu_optind >= ac || !av[bu_optind]) {
    bu_log(USAGE, progname);
    return 1;
  }
  av += bu_optind;

  if (!bu_file_exists(av[0], NULL)) {
    bu_log("ERROR: %s does not exist\n", av[0]);
    return 2;
  }

  if (iterations >= SKIP)
      bu_log("NOTE: only printing times every %d iterations\n\n", SKIP);

  for (i=0; i < iterations; i++) {

    timer = bu_gettime();
    dbip = db_open(av[0], DB_OPEN_READWRITE);
    if (!dbip) {
      bu_log("ERROR: %s could not be opened\n", av[0]);
      return 2;
    }
    seconds[1] += (bu_gettime() - timer) / 1000000.0;
    if (iterations < SKIP || (i % SKIP == 0))
	bu_log("[%2d] db_open: %02fs\n", i, (bu_gettime() - timer) / 1000000.0);

    timer = bu_gettime();
    (void)db_dirbuild(dbip);
    seconds[2] += (bu_gettime() - timer) / 1000000.0;
    if (iterations < SKIP || (i % SKIP == 0))
	bu_log("[%2d] db_dirbuild: %02fs\n", i, (bu_gettime() - timer) / 1000000.0);

    timer = bu_gettime();
    db_update_nref(dbip);
    seconds[3] += (bu_gettime() - timer) / 1000000.0;
    if (iterations < SKIP || (i % SKIP == 0))
	bu_log("[%2d] db_update_nref: %02fs\n", i, (bu_gettime() - timer) / 1000000.0);

    timer = bu_gettime();
    {
      const struct rt_wdb *wdbp = wdb_dbopen(dbip, RT_WDB_TYPE_DB_DISK);
      if (wdbp) {
	struct ged ged;
	ged_init(&ged);
	ged.dbip = wdbp->dbip;

	const char *tops_av[1] = {"tops"};
	(void)ged_exec_tops(&ged, 1, (const char **)tops_av);
	ged_free(&ged);
      }
    }
    seconds[4] += (bu_gettime() - timer) / 1000000.0;
    if (iterations < SKIP || (i % SKIP == 0))
	bu_log("[%2d] tops: %02fs\n", i, (bu_gettime() - timer) / 1000000.0);

    timer = bu_gettime();
    db_close(dbip);
    seconds[5] += (bu_gettime() - timer) / 1000000.0;
    if (iterations < SKIP || (i % SKIP == 0))
	bu_log("[%2d] db_close: %02fs\n", i, (bu_gettime() - timer) / 1000000.0);
  }

  seconds[0] += (bu_gettime() - begin) / 1000000.0;

  bu_log("\n");
  bu_log("db_open: %02fs\n", seconds[1]);
  bu_log("db_dirbuild: %02fs\n", seconds[2]);
  bu_log("db_update_nref: %02fs\n", seconds[3]);
  bu_log("ged_tops: %02fs\n", seconds[4]);
  bu_log("db_close: %02fs\n", seconds[5]);
  bu_log("\nELAPSED: %02fs\n", seconds[0]);

  return 0;
}


/*
 * Local Variables:
 * mode: C
 * tab-width: 8
 * indent-tabs-mode: t
 * c-file-style: "stroustrup"
 * End:
 * ex: shiftwidth=4 tabstop=8
 */
