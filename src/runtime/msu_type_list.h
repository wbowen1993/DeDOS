/*
START OF LICENSE STUB
    DeDOS: Declarative Dispersion-Oriented Software
    Copyright (C) 2017 University of Pennsylvania, Georgetown University

    This program is free software: you can redistribute it and/or modify
    it under the terms of the GNU General Public License as published by
    the Free Software Foundation, either version 3 of the License, or
    (at your option) any later version.

    This program is distributed in the hope that it will be useful,
    but WITHOUT ANY WARRANTY; without even the implied warranty of
    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
    GNU General Public License for more details.

    You should have received a copy of the GNU General Public License
    along with this program.  If not, see <http://www.gnu.org/licenses/>.
END OF LICENSE STUB
*/
/**
 * @file msu_type_list.h
 * Defines the list of MSUs that can be instantiated on a runtime.
 *
 * Ifdef guards allow you to optionally compile different groups of MSUS.
 */

#ifndef MSU_TYPE_LIST_H_
#define MSU_TYPE_LIST_H_

// These MSUs are always included
#include "socket_msu.h"

// These ifdef guards below allow you to optionally compile different groups of MSUS
// Define the appropriate macro in the Makefile to enable compilation of the
// MSU group

#ifdef COMPILE_BAREMETAL_MSUS
#include "baremetal/baremetal_msu.h"
#include "baremetal/baremetal_socket_msu.h"
#define BAREMETAL_MSUS  \
        &BAREMETAL_MSU_TYPE, \
        &BAREMETAL_SOCK_MSU_TYPE,
#else
#define BAREMETAL_MSUS
#endif

/** Enables compilation of webserver MSUs */
#ifdef COMPILE_WEBSERVER_MSUS
#include "webserver/read_msu.h"
#include "webserver/http_msu.h"
#include "webserver/regex_msu.h"
#include "webserver/regex_routing_msu.h"
#include "webserver/cache_msu.h"
#include "webserver/fileio_msu.h"
#include "webserver/write_msu.h"

/** The MSUs that comprise the webserver application*/
#define WEBSERVER_MSUS \
        &WEBSERVER_READ_MSU_TYPE, \
        &WEBSERVER_HTTP_MSU_TYPE, \
        &WEBSERVER_REGEX_MSU_TYPE, \
        &WEBSERVER_REGEX_ROUTING_MSU_TYPE, \
        &WEBSERVER_CACHE_MSU_TYPE, \
        &WEBSERVER_FILEIO_MSU_TYPE, \
        &WEBSERVER_WRITE_MSU_TYPE,
#else

// If the webserver is not enabled, must define this macro to be empty
#define WEBSERVER_MSUS
#endif

/** Enables compilation of NDlog MSUs */
#ifdef COMPILE_NDLOG_MSUS
#include "ndlog/ndlog_msu_R1_eca.h"
#include "ndlog/ndlog_recv_msu.h"
#include "ndlog/ndlog_routing_msu.h"
/** The MSUs that comprise the NDlog application */
#define NDLOG_MSUS \
        &NDLOG_MSU_TYPE, \
        &NDLOG_RECV_MSU_TYPE, \
        &NDLOG_ROUTING_MSU_TYPE,
#else
#define NDLOG_MSUS
#endif

/** Enables compilation of picotcp MSUs */
#ifdef COMPILE_PICO_TCP_MSUS
#include "pico_tcp/msu_pico_tcp.h"
#include "pico_tcp/msu_tcp_handshake.h"
#include "pico_tcp/msu_app_tcp_echo.h"

/** The MSUs that comprise the pico_tcp application */
#define PICO_TCP_MSUS \
        &PICO_TCP_MSU_TYPE, \
        &TCP_HANDSHAKE_MSU_TYPE, \
        &MSU_APP_TCP_ECHO_TYPE,
#else
#define PICO_TCP_MSUS
#endif

/** The complete list of MSU types.
 * This is defined as a macro so that it can be overridden in testing scripts
 */
#ifndef MSU_TYPE_LIST
#define MSU_TYPE_LIST  \
    { \
        &SOCKET_MSU_TYPE, \
        BAREMETAL_MSUS \
        WEBSERVER_MSUS \
        NDLOG_MSUS \
        PICO_TCP_MSUS \
    }
#endif

#endif
