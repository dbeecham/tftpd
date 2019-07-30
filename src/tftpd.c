#define _DEFAULT_SOURCE

#include <syslog.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <netdb.h>
#include <unistd.h>
#include <errno.h>
#include <string.h>
#include <stdio.h>
#include <sys/epoll.h>
#include <unistd.h>
#include <fcntl.h>

#include "tftpd.h"
#include "tftpd_tftp_parser.h"

static int tftpd_epoll_event_udp_sock_fd (
    struct tftpd_s * tftpd,
    struct epoll_event * event
)
{

    int ret = 0;
    int bytes_read = 0;
    int bytes_written = 0;
    struct sockaddr_storage their_addr;
    socklen_t their_addr_len = sizeof(their_addr);

    syslog(LOG_DEBUG, "%s:%d:%s: hi!",
           __FILE__, __LINE__, __func__);

    if (TFTPD_S_SENTINEL != tftpd->sentinel) {
        syslog(LOG_ERR, "%s:%d:%s: sentinel is wrong!",
               __FILE__, __LINE__, __func__);
        return -1;
    }

    char buf[TFTPD_UDP_SOCKET_BUF_LEN];
    bytes_read = recvfrom(
        event->data.fd,
        buf,
        TFTPD_UDP_SOCKET_BUF_LEN,
        0,
        (struct sockaddr *)&their_addr,
        &their_addr_len
    );
    if (bytes_read < 0) {
        syslog(LOG_ERR, "%s:%d:%s: recvfrom: %s",
               __FILE__, __LINE__, __func__, strerror(errno));
        return -1;
    }
    if (0 == bytes_read) {
        syslog(LOG_WARNING, "%s:%d:%s: read 0 bytes",
               __FILE__, __LINE__, __func__);
        return -1;
    }

    tftpd->tftp_parser.their_addr = their_addr;
    tftpd->tftp_parser.their_addr_len = their_addr_len;
    ret = tftp_parser_parse(&tftpd->tftp_parser, buf, bytes_read);
    if (ret < 0) {
        syslog(LOG_ERR, "%s:%d:%s: tftpd_tftp_parser_parser returned %d",
               __FILE__, __LINE__, __func__, ret);
        return -1;
    }


    // Re-arm EPOLLONESHOT file descriptor in epoll
    ret = epoll_ctl(
        tftpd->epoll_fd,
        EPOLL_CTL_MOD,
        event->data.fd,
        &(struct epoll_event){
            .events = EPOLLIN | EPOLLERR | EPOLLHUP | EPOLLONESHOT,
            .data = {
                .fd = event->data.fd
            }
        }
    );
    if (-1 == ret) {
        syslog(LOG_ERR, "%s:%d: epoll_ctl: %s", __func__, __LINE__, strerror(errno));
        return -1;
    }

    return 0;
}

static int tftpd_epoll_event_dispatch (
    struct tftpd_s * tftpd,
    struct epoll_event * event
)
{
    if (event->data.fd == tftpd->udp_sock_fd)
        return tftpd_epoll_event_udp_sock_fd(tftpd, event);

    syslog(LOG_WARNING, "%s:%d:%s: No match on epoll event.",
           __FILE__, __LINE__, __func__);
    return 0;
}


static int tftpd_epoll_handle_events (
    struct tftpd_s * tftpd,
    struct epoll_event epoll_events[EPOLL_NUM_EVENTS],
    int ep_num_events
)
{
    int ret = 0;
    for (int i = 0; i < ep_num_events; i++) {
        ret = tftpd_epoll_event_dispatch(tftpd, &epoll_events[i]);
        if (0 != ret) {
            syslog(LOG_ERR, "%s:%d:%s: tftpd_epoll_event_dispatch returned %d",
                   __FILE__, __LINE__, __func__, ret);
            return ret;
        }
    }
}


int tftpd_ack_cb (
    void * user_data,
    int ack_num
)
{
    struct tftpd_s * tftpd = user_data;
    int ret = 0;
    int bytes_written = 0;
    int bytes_read = 0;
    int next_pkt_num = ack_num + 1;
    char buf[65535] = {

        // these two bytes indicate "type of package". 3 is a data packet.
        0, 3,

        // these two bytes represent which packet we're sending. First line
        // extracts the uppermost 8 bits, the second line extracts the
        // lowermost 8 bits.
        (next_pkt_num >> 8) & 255,
        next_pkt_num & 255

    };

    if (NULL == user_data) {
        syslog(LOG_ERR, "%s:%d:%s: user_data is NULL!",
               __FILE__, __LINE__, __func__);
        return -1;
    }
    if (TFTPD_S_SENTINEL != tftpd->sentinel) {
        syslog(LOG_ERR, "%s:%d:%s: sentinel is wrong!",
               __FILE__, __LINE__, __func__);
        return -1;
    }

    if (1 == tftpd->finished) {
        return 0;
    }

    // Seek in the file
    ret = lseek(tftpd->file_fd, ack_num * tftpd->blksize, SEEK_SET);
    if (ret < 0) {
        syslog(LOG_ERR, "%s:%d:%s: lseek: %s",
               __FILE__, __LINE__, __func__, strerror(errno));
        return -1;
    }

    char file_buf[65536];
    bytes_read = read(tftpd->file_fd, file_buf, tftpd->blksize);
    memcpy(buf + 4, file_buf, bytes_read);

    if (bytes_read < 512) {
        tftpd->finished = 1;
    }

    bytes_written = sendto(
        tftpd->udp_sock_fd,
        buf, 
        bytes_read + 4,
        0,
        (struct sockaddr *)&tftpd->tftp_parser.their_addr,
        tftpd->tftp_parser.their_addr_len
    );
    printf("%d: wrote %d bytes\n", __LINE__, bytes_written);
    if (bytes_written < 0) {
        syslog(LOG_ERR, "%s:%d:%s: sendto: %s",
               __FILE__, __LINE__, __func__, strerror(errno));
        return -1;
    }

    return 0;
}


int tftpd_read_request_cb (
    void * user_data,
    int blksize
)
{
    struct tftpd_s * tftpd = user_data;
    int bytes_written = 0;
    int ret = 0;

    if (NULL == user_data) {
        syslog(LOG_ERR, "%s:%d:%s: user_data is NULL!",
               __FILE__, __LINE__, __func__);
        return -1;
    }
    if (TFTPD_S_SENTINEL != tftpd->sentinel) {
        syslog(LOG_ERR, "%s:%d:%s: sentinel is wrong!",
               __FILE__, __LINE__, __func__);
        return -1;
    }

    // Store this bit of info for later. This is not an optimal place to store
    // this info, but this server is too simple right now and does not handle
    // multiple connections.
    tftpd->blksize = blksize;
    tftpd->finished = 0;

    // Construct a buf with an 'option acknowledgement' package in it.
    char buf[32] = {
        0, 6, 'b', 'l', 'o', 'c', 'k', 's', 'i', 'z', 'e', 0
    };
    bytes_written = snprintf(buf + 12, 32-12, "%d", blksize);

    // Send the option ack to the tftp client.
    bytes_written = sendto(
        tftpd->udp_sock_fd,
        buf,
        12 + bytes_written + 1,
        0,
        (struct sockaddr *)&tftpd->tftp_parser.their_addr,
        tftpd->tftp_parser.their_addr_len
    );
    if (bytes_written < 0) {
        syslog(LOG_ERR, "%s:%d:%s: sendto: %s",
               __FILE__, __LINE__, __func__, strerror(errno));
        return -1;
    }

    return 0;
}


int tftpd_init (
    struct tftpd_s * tftpd
)
{

    int ret = 0;
    struct addrinfo *servinfo, *p;

    tftpd->sentinel = TFTPD_S_SENTINEL;

    // Initialize the tftp parser
    ret = tftp_parser_init(&tftpd->tftp_parser, tftpd_ack_cb, tftpd_read_request_cb, tftpd);
    if (ret < 0) {
        syslog(LOG_ERR, "%s:%d:%s: tftp_parser_init returned %d",
               __FILE__, __LINE__, __func__, ret);
        return -1;
    }

    // Create the epoll instance
    tftpd->epoll_fd = epoll_create1(EPOLL_CLOEXEC);
    if (-1 == tftpd->epoll_fd) {
        syslog(LOG_ERR, "%s:%d:%s: epoll_create1: %s", 
               __FILE__, __LINE__, __func__, strerror(errno));
        return -1;
    }


    // Get server info
    struct addrinfo hints = {
        .ai_family = AF_UNSPEC,
        .ai_socktype = SOCK_DGRAM
    };
    ret = getaddrinfo(HOST, PORT, &hints, &servinfo);
    if (ret < 0) {
        syslog(LOG_ERR, "%s:%d:%s: getaddrinfo: %s",
               __FILE__, __LINE__, __func__, gai_strerror(ret));
        return -1;
    }

    for (p = servinfo; p != NULL; p = p->ai_next) {
        tftpd->udp_sock_fd = socket(p->ai_family, p->ai_socktype, p->ai_protocol);
        if (tftpd->udp_sock_fd < 0) {
            syslog(LOG_WARNING, "%s:%d:%s: socket: %s",
                   __FILE__, __LINE__, __func__, strerror(errno));
            continue;
        }

        ret = setsockopt(tftpd->udp_sock_fd, SOL_SOCKET, SO_REUSEADDR, &(int){1}, sizeof(int));
        if (ret < 0) {
            syslog(LOG_WARNING, "%s:%d:%s: setsockopt: %s",
                   __FILE__, __LINE__, __func__, strerror(errno));
        }

        ret = bind(tftpd->udp_sock_fd, p->ai_addr, p->ai_addrlen);
        if (ret < 0) {
            syslog(LOG_WARNING, "%s:%d:%s: bind: %s",
                   __FILE__, __LINE__, __func__, strerror(errno));
            close(tftpd->udp_sock_fd);
            continue;
        }
        break;
    }
    freeaddrinfo(servinfo);

    if (NULL == p) {
        syslog(LOG_ERR, "%s:%d:%s: bind failed",
               __FILE__, __LINE__, __func__);
        return -1;
    }


    // add datagram socket to epoll
    ret = epoll_ctl(
        tftpd->epoll_fd,
        EPOLL_CTL_ADD,
        tftpd->udp_sock_fd,
        &(struct epoll_event){
            .events = EPOLLIN | EPOLLERR | EPOLLHUP | EPOLLONESHOT,
            .data = {
                .fd = tftpd->udp_sock_fd
            }
        }
    );
    if (-1 == ret) {
        syslog(LOG_ERR, "%s:%d:%s: epoll_ctl: %s", 
               __FILE__, __LINE__, __func__, strerror(errno));
        return -1;
    }

    return 0;
}


int tftpd_epoll_loop (
    struct tftpd_s * tftpd
)
{
    int ret = 0;

    syslog(LOG_DEBUG, "%s:%d:%s: hi!",
           __FILE__, __LINE__, __func__);

    if (TFTPD_S_SENTINEL != tftpd->sentinel) {
        syslog(LOG_ERR, "%s:%d:%s: sentinel is wrong!",
               __FILE__, __LINE__, __func__);
        return -1;
    }

    int ep_num_events = 0;
    struct epoll_event events[EPOLL_NUM_EVENTS];
    for (ep_num_events = epoll_wait(tftpd->epoll_fd, events, EPOLL_NUM_EVENTS, -1);
         ep_num_events > 0 || (-1 == ep_num_events && EINTR == errno);
         ep_num_events = epoll_wait(tftpd->epoll_fd, events, EPOLL_NUM_EVENTS, -1))
    {
        ret = tftpd_epoll_handle_events(tftpd, events, ep_num_events);
        if (ret < 0) {
            syslog(LOG_ERR, "%s:%d:%s: tftpd_epoll_handle_events returned %d",
                    __FILE__, __LINE__, __func__, ret);
            return -1;
        }
    }
    if (-1 == ep_num_events) {
        syslog(LOG_ERR, "%s:%d:%s: epoll_wait: %s", 
               __FILE__, __LINE__, __func__, strerror(errno));
        return -1;
    }

    return 0;
}


int main (
    int argc,
    char * argv[]
)
{

    int ret = 0;
    struct tftpd_s tftpd = {0};

    openlog("tftpd", LOG_CONS | LOG_PID, LOG_USER);
    syslog(LOG_INFO, "%s:%d:%s: hi!",
           __FILE__, __LINE__, __func__);


    // Make sure a file has been given to us in args
    if (argc != 2) {
        syslog(LOG_ERR, "%s:%d:%s: argc is not 2",
               __FILE__, __LINE__, __func__);
        return -1;
    }

    ret = tftpd_init(&tftpd);
    if (ret < 0) {
        syslog(LOG_ERR, "%s:%d:%s: tftpd_init returned %d",
               __FILE__, __LINE__, __func__, ret);
        return -1;
    }

    // Try to open the tftp file
    tftpd.file_fd = open(argv[1], O_RDONLY);
    if (tftpd.file_fd < 0) {
        syslog(LOG_ERR, "%s:%d:%s: open: %s",
               __FILE__, __LINE__, __func__, strerror(errno));
        return -1;
    }

    ret = tftpd_epoll_loop(&tftpd);
    if (ret < 0) {
        syslog(LOG_ERR, "%s:%d:%s: tftpd_epoll_loop returned %d",
               __FILE__, __LINE__, __func__, ret);
        return -1;
    }


    return 0;
}
