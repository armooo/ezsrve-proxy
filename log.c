/*
    Copyright Jason Michalski

    This file is part ofi ezsrve_proxy.

    ezsrve_proxy is free software: you can redistribute it and/or modify
    it under the terms of the GNU General Public License as published by
    the Free Software Foundation, either version 3 of the License, or
    (at your option) any later version.

    ezsrve_proxy is distributed in the hope that it will be useful,
    but WITHOUT ANY WARRANTY; without even the implied warranty of
    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
    GNU General Public License for more details.

    You should have received a copy of the GNU General Public License
    along with ezsrve_proxy.  If not, see <http://www.gnu.org/licenses/>.
*/

#include <syslog.h>
#include <stdarg.h>
#include <stdio.h>

extern int daemonize;

void init_log() {
    openlog("ezsrve_proxy", 0, LOG_DAEMON);
}

void write_log(const char* format, ...){
    va_list va;
    va_start (va, format);
    if ( !daemonize ) {
        vprintf(format, va);
    }
    va_end(va);

    va_start (va, format);
    vsyslog(LOG_NOTICE, format, va);
    va_end(va);
}
