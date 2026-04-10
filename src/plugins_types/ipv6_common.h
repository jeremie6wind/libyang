/*
 * Copyright 2026 6WIND S.A.
 */

#ifdef _WIN32
# include <winsock2.h>
# include <ws2tcpip.h>
#else
#  include <arpa/inet.h>
#  if defined (__FreeBSD__) || defined (__NetBSD__) || defined (__OpenBSD__)
#    include <netinet/in.h>
#    include <sys/socket.h>
#  endif
#endif

#include "libyang.h"

/**
 * @brief convert dual format ipv6 address inside to compressed ipv6 address
 */
LY_ERR ipv6address_ip2str(struct in6_addr *addr, ly_bool dual_stack,
        char **str_addr, int str_addr_len);
