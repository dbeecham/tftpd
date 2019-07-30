#include <syslog.h>
#include <stdio.h>

#include "tftpd_tftp_parser.h"

%%{

    machine tftp;
    access parser->;

    action read_request_fin {
        printf("got read request\n");
        parser->read_request_cb(parser->user_data, parser->blksize);
    }


    action ack_init {
        parser->ack_num = 0;
    }

    action ack_fin {
        printf("got ack of pkt %d\n", parser->ack_num);
        parser->ack_cb(parser->user_data, parser->ack_num);
    }

    action blksize_init {
        parser->blksize = 0;
    }
    
    action blksize_copy {
        parser->blksize *= 10;
        parser->blksize += (*p - '0');
    }

    read_request =
        [A-Za-z0-9_\-]+ ${ printf("(%c)\n", *p); }
        0
        [A-Za-z0-9_\-]+ ${ printf("|%c|\n", *p); }
        0 
        'timeout'
        0
        any
        0
        'blksize'
        0
        any{4} >to(blksize_init) $(blksize_copy)
        0 @read_request_fin;

    ack =
        any @{ parser->ack_num = ((uint8_t)*p); }
        any @{ parser->ack_num = (parser->ack_num << 8) + ((uint8_t)*p); } @ack_fin;

    packet =
        0 @{ printf("[0x00]\n"); }
        ( 1 read_request
        | 4 ack
        );

    main := packet* $err{ fprintf(stderr, "parse failed, char is 0x%02x\n", *p); fgoto main; };

    write data;

}%%

int tftp_parser_init (
    struct tftp_parser_s * parser,
    int (*ack_cb)(void * user_data, int ack_num),
    int (*read_request_cb)(void * user_data, int blksize),
    void * user_data
)
{

    parser->sentinel = TFTP_PARSER_S_SENTINEL;
    parser->user_data = user_data;
    parser->ack_cb = ack_cb;
    parser->read_request_cb = read_request_cb;
    %% write init;
    return 0;

}

int tftp_parser_parse (
    struct tftp_parser_s * parser,
    char * buf,
    int buf_len
)
{

    syslog(LOG_DEBUG, "%s:%d:%s: hi!",
           __FILE__, __LINE__, __func__);

    if (TFTP_PARSER_S_SENTINEL != parser->sentinel) {
        syslog(LOG_ERR, "%s:%d:%s: sentinel is wrong!",
               __FILE__, __LINE__, __func__);
        return -1;
    }

    char * p = buf;
    char * pe = buf + buf_len;
    char * eof = 0;

    %% write exec;

    return 0;
}
