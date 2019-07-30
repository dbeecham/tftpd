#pragma once

#include <stdint.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <netdb.h>

#define TFTP_PARSER_S_SENTINEL 9091

struct tftp_parser_s {
    int sentinel;
    union {
        uint16_t ack_num;
        uint8_t ack_num_d[2];
    };
    int cs;
    uint16_t blksize;
    void * user_data;
    int (*ack_cb)(void * user_data, int ack_num);
    int (*read_request_cb)(void * user_data, int blksize);
    struct sockaddr_storage their_addr;
    socklen_t their_addr_len;
};

int tftp_parser_init (
    struct tftp_parser_s * parser,
    int (*ack_cb)(void * user_data, int ack_num),
    int (*read_request_cb)(void * user_data, int blksize),
    void * user_data
);

int tftp_parser_parse (
    struct tftp_parser_s * parser,
    char * buf,
    int buf_len
);
