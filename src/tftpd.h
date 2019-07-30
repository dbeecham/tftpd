#pragma once

#include "tftpd_tftp_parser.h"

#define HOST "0.0.0.0"
#define PORT "69"

#define TFTPD_UDP_SOCKET_BUF_LEN 1024

#define EPOLL_NUM_EVENTS 8

#define TFTPD_S_SENTINEL 9090

struct tftpd_s {
    int sentinel;
    int epoll_fd;
    int udp_sock_fd;
    int blksize;
    int file_fd;
    int finished;
    struct tftp_parser_s tftp_parser;
};

struct tftpd_data_packet_s {
    uint16_t block_i;
    char data[];
};

struct tftpd_packet_s {
    uint16_t opcode;
    union {
        struct tftpd_data_packet_s data_packet;
        int fd;
    };
};

