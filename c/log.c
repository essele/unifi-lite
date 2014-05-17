/*------------------------------------------------------------------------------
 *  This file is part of UniFi-lite.
 *  Copyright (C) 2014 Lee Essen <lee.essen@nowonline.co.uk>
 *
 *  This program is free software: you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation, either version 3 of the License, or
 *  (at your option) any later version.
 *
 *  This program is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *  GNU General Public License for more details.
 *
 *  You should have received a copy of the GNU General Public License
 *  along with this program.  If not, see <http://www.gnu.org/licenses/>.
 *------------------------------------------------------------------------------
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/select.h>
#include <unistd.h>
#include <errno.h>
#include <fcntl.h>
#include <stdint.h>
#include <ctype.h>
#include <errno.h>
#include <stdarg.h>

#include <sys/types.h>
#include <time.h>

#include "log.h"

/*==============================================================================
 * Globals (local to this module)
 *==============================================================================
 */
#define LOG_FILE        "./x.log"               // for logging issues

static FILE				*logf;					// fh for log file
static int				log_level = LOG_INFO;	// default logging level

/*==============================================================================
 * Initialise the log file
 *==============================================================================
 */
void init_log() {
    if(!(logf = fopen(LOG_FILE, "a"))) {
        fprintf(stderr, "WARNING: unable to open log file (%s): %s\n", LOG_FILE, strerror(errno));
    }
    wlog(LOG_WARN, "starting.");
}

/*==============================================================================
 * Write stuff into our log file, we have a level and only write stuff that is
 * less than or equal the level
 *==============================================================================
 */
char	*log_levels[5] = { "FATAL:", "ERROR:", "WARN:", "INFO:", "DEBUG:" };
void wlog(int level, char *fmt, ...) {
	va_list 	ap;
	char		tstr[30];
	time_t		t;
	struct tm	*tmp;

	// Sanity on the log_level...	
	if(level > log_level) return;
	if(level < 0 || level > 4) level = LOG_ERROR;

	// Inclue the time format...
	t = time(NULL);
	tmp = localtime(&t);
	if(tmp == NULL) {
		sprintf(tstr, "--unknown--");
	} else {
		strftime(tstr, sizeof(tstr), "%b %d %T", tmp);
	}

	// Output...
	va_start(ap, fmt);
	fprintf(logf, "%s  %-6.6s ", tstr, log_levels[level]);
	vfprintf(logf, fmt, ap);
	fprintf(logf, "\n");
	fflush(logf);
}
 
