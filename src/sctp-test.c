
#if defined(__FreeBSD__) || defined(__APPLE__)
#define HAS_SOCKLEN 1
#endif

 #if defined(__APPLE__)
#if !defined(_POSIX_C_SOURCE)
#define _POSIX_C_SOURCE    200809L
#endif
#undef _DARWIN_C_SOURCE
#endif

#include <sys/socket.h>


#include <netdb.h>
#include <sys/poll.h>
#include <netinet/in.h>
#ifdef __APPLE__
#include <sctp/sctp.h>
#else
#include <netinet/sctp.h>
#endif
#include <string.h>
#include <stdio.h>
#include <errno.h>
#include <stdlib.h>
#include <arpa/inet.h>
#include <fcntl.h>
#include <err.h>
#include <unistd.h>
#include <termios.h>

int handleNotification(void *data, ssize_t data_size, struct sctp_sndrcvinfo *sinfo);
int isDataAvailable(int _socket, int timeoutInMs, int *hasData, int *hasHup);
int receiveAndProcessSCTP(int _socket);
int main(int argc, char *argv[]);
void setBlocking(int _socket, int set);
void enableSctpEvents(int _socket);
void setLingerTime(int _socket, int linger_time);
void setReuseAddr(int _socket);
void setReusePort(int _socket);
void setNodelay(int _socket);
void printStatus(int _socket);
void setIPDualStack(int _socket);


#define    NO_DATA_AVAILABLE        0
#define    DATA_AVAILABLE           1
#define    DATA_AVAILABLE_AND_HUP   2

int main(int argc, char *argv[])
{
    char                ip_buffer[256];
    int                 source_port  = 0;
    int                 destination_port = 0;
    struct sockaddr_in6 local_addr6;
    struct sockaddr_in6 remote_addr6;
    int                 _socket;
    int                 err;
    sctp_assoc_t        assoc;
    int                 avail = 0;
    struct termios      terminal_attributes;
    
    if(argc<5)
    {
        fprintf(stderr,"Usage: %s [sourceip] [sourceport]  [destinationip] [destinationport]\n",argv[0]);
        exit(EXIT_FAILURE);
    }
    
    source_port = atoi(argv[2]);
    destination_port = atoi(argv[4]);

    /******* parsing local IP *************/
    
    memset(&local_addr6,0x00,sizeof(local_addr6));
    local_addr6.sin6_family = AF_INET6;
#ifdef HAS_SOCKLEN
    local_addr6.sin6_len         = sizeof(struct sockaddr_in6);
#endif
    local_addr6.sin6_port = htons(source_port);
    if(1!=inet_pton(AF_INET6,argv[1], &local_addr6.sin6_addr))
    {
        memset(&ip_buffer,0x00,sizeof(ip_buffer));
        snprintf(ip_buffer, sizeof(ip_buffer)-1,"::ffff:%s",argv[1]);
        if(1!=inet_pton(AF_INET6,ip_buffer, &local_addr6.sin6_addr))
        {
            fprintf(stderr,"Error: can not interpret '%s' as ip address\n",argv[1]);
            exit(EXIT_FAILURE);
        }
    }
    
    /******* parsing remote IP *************/
    
    memset(&remote_addr6,0x00,sizeof(remote_addr6));
    remote_addr6.sin6_family = AF_INET6;
#ifdef HAS_SOCKLEN
    remote_addr6.sin6_len         = sizeof(struct sockaddr_in6);
#endif
    remote_addr6.sin6_port = htons(destination_port);
    if(1!=inet_pton(AF_INET6,argv[3], &remote_addr6.sin6_addr))
    {
        memset(&ip_buffer,0x00,sizeof(ip_buffer));
        snprintf(ip_buffer, sizeof(ip_buffer)-1,"::ffff:%s",argv[3]);
        if(1!=inet_pton(AF_INET6,ip_buffer, &remote_addr6.sin6_addr))
        {
            fprintf(stderr,"Error: can not interpret '%s' as ip address\n",argv[3]);
            exit(EXIT_FAILURE);
        }
    }
    
    /******* socket() *************/
    _socket = socket(AF_INET6,SOCK_STREAM,IPPROTO_SCTP);
    if(_socket==-1)
    {
        fprintf(stderr,"can not open socket: %d %s\n",errno,strerror(errno));
        exit(EXIT_FAILURE);
    }
    printf("socket() successful\n");
    
    
    printf("setting socket to non blocking\n");
    
    /******* setting optinos *************/
    setIPDualStack(_socket);
    setBlocking(_socket,0);
    enableSctpEvents(_socket);
    setLingerTime(_socket, 5);
    setReuseAddr(_socket);
    setReusePort(_socket);
    setNodelay(_socket);
    
    
    /******* bind() *************/
    
    err = bind(_socket, (const struct sockaddr *)&local_addr6, sizeof(local_addr6));
    if(err!=0)
    {
        fprintf(stderr,"can not bind: %d %s\n",errno,strerror(errno));
        exit(EXIT_FAILURE);
    }
    
    printf("bind() successful\n");
    
    /******* sctp_connectx() *************/
    
    err =  sctp_connectx(_socket,(struct sockaddr *)&remote_addr6,1,&assoc);
    if(err!=0)
    {
        if(errno==EINPROGRESS)
        {
            fprintf(stderr,"sctp_connectx returns (%d %s)\n",errno,strerror(errno));
        }
        else
        {
            fprintf(stderr,"sctp_connectx failed (%d %s)\n",errno,strerror(errno));
            exit(EXIT_FAILURE);
        }
    }
    
    printf("sctp_connectx() successful\n");
    
    avail = 0;
    err = 0;
    
    tcgetattr(STDIN_FILENO, &terminal_attributes);
    terminal_attributes.c_lflag = terminal_attributes.c_lflag & (~ICANON); /* we go to character by character mode */
    tcsetattr(STDIN_FILENO, TCSANOW, &terminal_attributes);

    int mustQuit;
    while(mustQuit==0)
    {
        int hasData = 0;
        int hasHup=0;
        int err = isDataAvailable(STDIN_FILENO,0,&hasData,&hasHup);
        if(hasData)
        {
            char buffer[2048];
            bzero(&buffer,sizeof(buffer));
            ssize_t read_bytes = read(STDIN_FILENO,&buffer,sizeof(buffer)-1);
            if(read_bytes==1)
            {
                if(buffer[0] == 0x1B)
                {
                    /* a single escape character */
                    printStatus(_socket);
                }
            }
            if(read_bytes > 0)
            {
                ssize_t written_bytes = write(_socket,&buffer,read_bytes);
                if(written_bytes > 0)
                {
                    fprintf(stderr,"{sent %d bytes}\n",(int)written_bytes);
                }
            }
        }

        hasData = 0;
        hasHup=0;
        err = isDataAvailable(STDIN_FILENO,2500,&hasData,&hasHup);
        if(hasHup)
        {
            mustQuit = 1;
        }
        if(hasData)
        {
            err = receiveAndProcessSCTP(_socket);
        }
        if(err)
        {
            fprintf(stderr,"{errno %d %s}\n",err,strerror(err));
        }
    }
}


int isDataAvailable(int _socket, int timeoutInMs, int *hasData, int *hasHup)
{
    int returnValue = 0;
    struct pollfd pollfds[1];
    int ret1;
    int ret2;
    int eno = 0;
    
    int events = POLLIN | POLLPRI | POLLERR | POLLHUP | POLLNVAL;
    
#ifdef POLLRDBAND
    events |= POLLRDBAND;
#endif
    
#ifdef POLLRDHUP
    events |= POLLRDHUP;
#endif
    
    memset(pollfds,0,sizeof(pollfds));
    pollfds[0].fd = _socket;
    pollfds[0].events = events;

    fprintf(stderr,"poll (timeout =%dms,socket=%d)\n",timeoutInMs,_socket);
    
    ret1 = poll(pollfds, 1, timeoutInMs);
    
    fprintf(stderr,"  returns %d (errno=%d:%s)\n",ret1,errno,strerror(errno));
    
    if (ret1 < 0)
    {
        eno = errno;
        if((eno==EINPROGRESS) || (eno == EINTR) || (eno==EAGAIN))
        {
            returnValue = 0;
        }
        else
        {
            returnValue = eno;
        }
    }
    else if (ret1 == 0)
    {
        returnValue = 0;
    }
    else /* ret1 > 0 */
    {
        /* we have some event to handle. */
        ret2 = pollfds[0].revents;
        fprintf(stderr,"pollfds[0].revents = %d",ret2);
        if(ret2 & POLLERR)
        {
            socklen_t len = sizeof(int);
            getsockopt(_socket, SOL_SOCKET, SO_ERROR, &returnValue, &len);
        }
        if(ret2 & POLLHUP)
        {
            *hasHup = 1;
        }
        
#ifdef POLLRDHUP
        if(ret2 & POLLRDHUP)
        {
            *hasHup = 1;
        }
#endif
        if(ret2 & POLLNVAL)
        {
            returnValue = EINVAL;
        }
#ifdef POLLRDBAND
        if(ret2 & POLLRDBAND)
        {
            *hasData = 1;
        }
#endif
        /* There is data to read.*/
        if(ret2 & (POLLIN | POLLPRI))
        {
            *hasData = 1;
        }
    }
    return returnValue;
}

#define    SCTP_RXBUF 10240

int receiveAndProcessSCTP(int _socket)
{
    char                    buffer[SCTP_RXBUF+1];
    int                     flags=0;
    struct sockaddr         source_address;
    struct sctp_sndrcvinfo  sinfo;
    socklen_t               fromlen;
    ssize_t                 bytes_read = 0;
    
    flags = 0;
    fromlen = sizeof(source_address);
    memset(&source_address,0,sizeof(source_address));
    memset(&sinfo,0,sizeof(sinfo));
    memset(&buffer[0],0xFA,sizeof(buffer));
    
    //    fprintf(stderr,"RXT: calling sctp_recvmsg(fd=%d)",link->fd);
    //    debug("sctp",0,"RXT: calling sctp_recvmsg. link=%08lX",(unsigned long)link);
    bytes_read = sctp_recvmsg (_socket, buffer, SCTP_RXBUF, &source_address,&fromlen,&sinfo,&flags);
    //    debug("sctp",0,"RXT: returned from sctp_recvmsg. link=%08lX",(unsigned long)link);
    //    fprintf(stderr,"RXT: sctp_recvmsg: bytes read =%ld, errno=%d\n",(long)bytes_read,(int)errno);
    
    fprintf(stderr,"sctp_recvmsg returns bytes_read=%d\n",(int)bytes_read);
    
    if(bytes_read == 0)
    {
        if(errno==ECONNRESET)
        {
            fprintf(stderr,"ECONNRESET\n");
            return -1;
        }
    }
    if(bytes_read <= 0)
    {
        /* we are having a non blocking read here */
        fprintf(stderr,"errno=%d %s",errno,strerror(errno));
        return -1;
    }
    
    fprintf(stderr,"[%d]",(int)bytes_read);
    if(flags & MSG_NOTIFICATION)
    {
        fprintf(stderr,"{NOTIFICATION}");
        handleNotification(buffer,bytes_read,&sinfo);
    }
    else
    {
        uint16_t streamId = sinfo.sinfo_stream;
        uint32_t protocolId = ntohl(sinfo.sinfo_ppid);
        fprintf(stderr,"{DATA,stream=%d,protocol=%d}",streamId,protocolId);
    }
    return 1;
}

int handleNotification(void *data, ssize_t data_size, struct sctp_sndrcvinfo *sinfo)
{
    const union sctp_notification *snp;
    
    char addrbuf[INET6_ADDRSTRLEN];
    const char *ap;
    struct sockaddr_in *sin;
    struct sockaddr_in6 *sin6;
    
    snp = data;
    ssize_t len = data_size;
    
    switch(snp->sn_header.sn_type)
    {
        case SCTP_ASSOC_CHANGE:
            fprintf(stderr,"SCTP_ASSOC_CHANGE\n");
            if(len < sizeof (struct sctp_assoc_change))
            {
                fprintf(stderr," Size Mismatch in SCTP_ASSOC_CHANGE\n");
                return -1;
            }
            fprintf(stderr,"  sac_type: %d\n",             (int)snp->sn_assoc_change.sac_type);
            fprintf(stderr,"  sac_flags: %d\n",            (int)snp->sn_assoc_change.sac_flags);
            fprintf(stderr,"  sac_length: %d\n",           (int)snp->sn_assoc_change.sac_length);
            fprintf(stderr,"  sac_state: %d\n",            (int)snp->sn_assoc_change.sac_state);
            fprintf(stderr,"  sac_error: %d\n",            (int)snp->sn_assoc_change.sac_error);
            fprintf(stderr,"  sac_outbound_streams: %d\n", (int)snp->sn_assoc_change.sac_outbound_streams);
            fprintf(stderr,"  sac_inbound_streams: %d\n",  (int)snp->sn_assoc_change.sac_inbound_streams);
            fprintf(stderr,"  sac_assoc_id: %d\n",         (int)snp->sn_assoc_change.sac_assoc_id);
            
            if((snp->sn_assoc_change.sac_state==SCTP_COMM_UP) && (snp->sn_assoc_change.sac_error== 0))
            {
                fprintf(stderr,"*** SCTP_COMM_UP ***\n");
                return 0;
            }
            else if(snp->sn_assoc_change.sac_state==SCTP_COMM_LOST)
            {
                fprintf(stderr,"*** SCTP_COMM_LOST ***\n");
                return -1;
            }
            else if (snp->sn_assoc_change.sac_state == SCTP_CANT_STR_ASSOC)
            {
                fprintf(stderr,"*** SCTP_CANT_STR_ASSOC ***\n");
                return -1;
            }
            else if(snp->sn_assoc_change.sac_error!=0)
            {
                fprintf(stderr,"*** SCTP_COMM_ERROR(%d) ***\n",snp->sn_assoc_change.sac_error);
                return -1;
            }
            break;
            
        case SCTP_PEER_ADDR_CHANGE:
            fprintf(stderr,"SCTP_PEER_ADDR_CHANGE\n");
            if(len < sizeof (struct sctp_paddr_change))
            {
                fprintf(stderr," Size Mismatch in SCTP_PEER_ADDR_CHANGE\n");
                return -1;
            }
            
            fprintf(stderr,"  spc_type: %d\n",    (int)snp->sn_paddr_change.spc_type);
            fprintf(stderr,"  spc_flags: %d\n",   (int)snp->sn_paddr_change.spc_flags);
            fprintf(stderr,"  spc_length: %d\n",  (int)snp->sn_paddr_change.spc_length);
            
            if (snp->sn_paddr_change.spc_aaddr.ss_family == AF_INET)
            {
                //struct sockaddr_in *sin;
                sin = (struct sockaddr_in *)&snp->sn_paddr_change.spc_aaddr;
                ap = inet_ntop(AF_INET, &sin->sin_addr, addrbuf, INET6_ADDRSTRLEN);
                fprintf(stderr,"  spc_aaddr: ipv4:%s", ap);
            }
            if (snp->sn_paddr_change.spc_aaddr.ss_family == AF_INET6)
            {
                sin6 = (struct sockaddr_in6 *)&snp->sn_paddr_change.spc_aaddr;
                ap = inet_ntop(AF_INET6, &sin6->sin6_addr, addrbuf, INET6_ADDRSTRLEN);
                fprintf(stderr,"  spc_aaddr: ipv6:%s", ap);
            }
            
            
            fprintf(stderr,"  spc_state: %d\n",   (int)snp->sn_paddr_change.spc_state);
            fprintf(stderr,"  spc_error: %d\n",   (int)snp->sn_paddr_change.spc_error);
            if (snp->sn_paddr_change.spc_aaddr.ss_family == AF_INET)
            {
                fprintf(stderr,"**** SCTP_PEER_ADDR_CHANGE: ipv4:%s\n",ap);
            }
            else
            {
                fprintf(stderr,"**** SCTP_PEER_ADDR_CHANGE: ipv6:%s\n",ap);
            }
            break;
            
        case SCTP_REMOTE_ERROR:
            fprintf(stderr,"SCTP_REMOTE_ERROR\n");
            if(len < sizeof (struct sctp_remote_error))
            {
                fprintf(stderr," Size Mismatch in SCTP_REMOTE_ERROR\n");
                return -1;
            }
            fprintf(stderr,"  sre_type: %d\n",             (int)snp->sn_remote_error.sre_type);
            fprintf(stderr,"  sre_flags: %d\n",            (int)snp->sn_remote_error.sre_flags);
            fprintf(stderr,"  sre_length: %d\n",           (int)snp->sn_remote_error.sre_length);
            fprintf(stderr,"  sre_length: %d\n",           (int)snp->sn_remote_error.sre_error);
            fprintf(stderr,"  sre_assoc_id: %d\n",         (int)snp->sn_remote_error.sre_assoc_id);
            fprintf(stderr,"  sre_data: %02X %02X %02X %02x\n",
                    (int)snp->sn_remote_error.sre_data[0],
                    (int)snp->sn_remote_error.sre_data[1],
                    (int)snp->sn_remote_error.sre_data[2],
                    (int)snp->sn_remote_error.sre_data[3]);
            break;
        case SCTP_SEND_FAILED:
            fprintf(stderr,"SCTP_SEND_FAILED\n");
            
            
            if(len < sizeof (struct sctp_send_failed))
            {
                fprintf(stderr," Size Mismatch in SCTP_SEND_FAILED\n");
                return -1;
            }
            fprintf(stderr,"  ssf_type: %d\n",                (int)snp->sn_send_failed.ssf_type);
            fprintf(stderr,"  ssf_flags: %d\n",               (int)snp->sn_send_failed.ssf_flags);
            fprintf(stderr,"  ssf_length: %d\n",              (int)snp->sn_send_failed.ssf_length);
            fprintf(stderr,"  ssf_error: %d\n",               (int)snp->sn_send_failed.ssf_error);
            fprintf(stderr,"  ssf_assoc_id: %d\n",            (int)snp->sn_send_failed.ssf_assoc_id);
            fprintf(stderr,"  ssf_info.sinfo_stream: %d\n",   (int)snp->sn_send_failed.ssf_info.sinfo_stream);
            fprintf(stderr,"  ssf_info.sinfo_ssn: %d\n",      (int)snp->sn_send_failed.ssf_info.sinfo_ssn);
            fprintf(stderr,"  ssf_info.sinfo_flags: %d\n",    (int)snp->sn_send_failed.ssf_info.sinfo_flags);
            fprintf(stderr,"  ssf_info.sinfo_stream: %d\n",   (int)snp->sn_send_failed.ssf_info.sinfo_stream);
            fprintf(stderr,"  ssf_info.sinfo_context: %d\n",  (int)snp->sn_send_failed.ssf_info.sinfo_context);
            fprintf(stderr,"  ssf_info.sinfo_timetolive: %d\n",(int)snp->sn_send_failed.ssf_info.sinfo_timetolive);
            fprintf(stderr,"  ssf_info.sinfo_tsn: %d\n",      (int)snp->sn_send_failed.ssf_info.sinfo_tsn);
            fprintf(stderr,"  ssf_info.sinfo_cumtsn: %d\n",   (int)snp->sn_send_failed.ssf_info.sinfo_cumtsn);
            fprintf(stderr,"  ssf_info.sinfo_assoc_id: %d\n", (int)snp->sn_send_failed.ssf_info.sinfo_assoc_id);
            fprintf(stderr,"  ssf_assoc_id: %d\n",    (int)snp->sn_send_failed.ssf_assoc_id);
            fprintf(stderr,"SCTP sendfailed: len=%du err=%d\n", snp->sn_send_failed.ssf_length,snp->sn_send_failed.ssf_error);
            return -1;
            break;
        case SCTP_SHUTDOWN_EVENT:
            fprintf(stderr,"SCTP_SHUTDOWN_EVENT\n");
            
            if(len < sizeof (struct sctp_shutdown_event))
            {
                fprintf(stderr," Size Mismatch in SCTP_SHUTDOWN_EVENT\n");
                return -1;
            }
            
            fprintf(stderr,"  sse_type: %d\n",     (int)snp->sn_shutdown_event.sse_type);
            fprintf(stderr,"  sse_flags: %d\n",    (int)snp->sn_shutdown_event.sse_flags);
            fprintf(stderr,"  sse_length: %d\n",   (int)snp->sn_shutdown_event.sse_length);
            fprintf(stderr,"  sse_assoc_id: %d\n", (int)snp->sn_shutdown_event.sse_assoc_id);
            return -1;
            break;
            
#ifdef    SCTP_ADAPTATION_INDICATION
        case SCTP_ADAPTATION_INDICATION:
            fprintf(stderr,"SCTP_ADAPTATION_INDICATION\n");
            if(len < sizeof(struct sctp_adaptation_event))
            {
                fprintf(stderr," Size Mismatch in SCTP_ADAPTATION_INDICATION\n");
                return -1;
            }
            fprintf(stderr,"  sai_type: %d\n",           (int)snp->sn_adaptation_event.sai_type);
            fprintf(stderr,"  sai_flags: %d\n",          (int)snp->sn_adaptation_event.sai_flags);
            fprintf(stderr,"  sai_length: %d\n",         (int)snp->sn_adaptation_event.sai_length);
            fprintf(stderr,"  sai_adaptation_ind: %d\n", (int)snp->sn_adaptation_event.sai_adaptation_ind);
            fprintf(stderr,"  sai_assoc_id: %d\n",       (int)snp->sn_adaptation_event.sai_assoc_id);
            break;
#endif
            
        case SCTP_PARTIAL_DELIVERY_EVENT:
            fprintf(stderr,"SCTP_PARTIAL_DELIVERY_EVENT\n");
            if(len < sizeof(struct sctp_pdapi_event))
            {
                fprintf(stderr," Size Mismatch in SCTP_PARTIAL_DELIVERY_EVENT\n");
                return -1;
            }
            
            fprintf(stderr,"  pdapi_type: %d\n",           (int)snp->sn_pdapi_event.pdapi_type);
            fprintf(stderr,"  pdapi_flags: %d\n",          (int)snp->sn_pdapi_event.pdapi_flags);
            fprintf(stderr,"  pdapi_length: %d\n",         (int)snp->sn_pdapi_event.pdapi_length);
            fprintf(stderr,"  pdapi_indication: %d\n",     (int)snp->sn_pdapi_event.pdapi_indication);
#ifndef LINUX
            fprintf(stderr,"  pdapi_stream: %d\n",         (int)snp->sn_pdapi_event.pdapi_stream);
            fprintf(stderr,"  pdapi_seq: %d\n",            (int)snp->sn_pdapi_event.pdapi_seq);
#endif
            fprintf(stderr,"  pdapi_assoc_id: %d\n",       (int)snp->sn_pdapi_event.pdapi_assoc_id);
            break;
            
#ifdef SCTP_AUTHENTICATION_EVENT
            
        case SCTP_AUTHENTICATION_EVENT:
            
            fprintf(stderr,"SCTP_AUTHENTICATION_EVENT\n");
            if(len < sizeof(struct sctp_authkey_event))
            {
                fprintf(stderr," Size Mismatch in SCTP_AUTHENTICATION_EVENT\n");
                return -1;
            }
            
            fprintf(stderr,"  auth_type: %d\n",           (int)snp->sn_auth_event.auth_type);
            fprintf(stderr,"  auth_flags: %d\n",          (int)snp->sn_auth_event.auth_flags);
            fprintf(stderr,"  auth_length: %d\n",         (int)snp->sn_auth_event.auth_length);
            fprintf(stderr,"  auth_keynumber: %d\n",      (int)snp->sn_auth_event.auth_keynumber);
            fprintf(stderr,"  auth_altkeynumber: %d\n",   (int)snp->sn_auth_event.auth_altkeynumber);
            fprintf(stderr,"  auth_indication: %d\n",     (int)snp->sn_auth_event.auth_indication);
            fprintf(stderr,"  auth_assoc_id: %d\n",       (int)snp->sn_auth_event.auth_assoc_id);
            break;
#endif
            
            
#ifdef SCTP_STREAM_RESET_EVENT
        case SCTP_STREAM_RESET_EVENT:
            
            
            fprintf(stderr,"SCTP_STREAM_RESET_EVENT\n");
            if(len < sizeof(struct sctp_stream_reset_event))
            {
                fprintf(stderr," Size Mismatch in SCTP_STREAM_RESET_EVENT\n");
                return -1;
            }
            fprintf(stderr,"  strreset_type: %d\n",     (int)snp->sn_strreset_event.strreset_type);
            fprintf(stderr,"  strreset_flags: %d\n",    (int)snp->sn_strreset_event.strreset_flags);
            fprintf(stderr,"  strreset_length: %d\n",   (int)snp->sn_strreset_event.strreset_length);
            fprintf(stderr,"  strreset_assoc_id: %d\n", (int)snp->sn_strreset_event.strreset_assoc_id);
            break;
            
#endif
            
            
#ifdef SCTP_SENDER_DRY_EVENT
        case SCTP_SENDER_DRY_EVENT:
            
            fprintf(stderr,"SCTP_SENDER_DRY_EVENT\n");
            if(len < sizeof(struct sctp_sender_dry_event))
            {
                fprintf(stderr," Size Mismatch in SCTP_SENDER_DRY_EVENT\n");
                return -1;
            }
            
            fprintf(stderr,"  sender_dry_type: %d\n",     (int)snp->sn_sender_dry_event.sender_dry_type);
            fprintf(stderr,"  sender_dry_flags: %d\n",    (int)snp->sn_sender_dry_event.sender_dry_flags);
            fprintf(stderr,"  sender_dry_length: %d\n",   (int)snp->sn_sender_dry_event.sender_dry_length);
            fprintf(stderr,"  sender_dry_assoc_id: %d\n", (int)snp->sn_sender_dry_event.sender_dry_assoc_id);
            break;
#endif
        default:
            fprintf(stderr,"  SCTP unknown event type: %hu", snp->sn_header.sn_type);
            fprintf(stderr,"   RX-STREAM: %d\n",sinfo->sinfo_stream);
            fprintf(stderr,"   RX-PROTO: %d\n", ntohl(sinfo->sinfo_ppid));
            break;
    }
    return 0;
}


void setBlocking(int _socket, int set)
{
    int err = 0;
    int flags = fcntl(_socket, F_GETFL, 0);
    if(set)
    {
        err = fcntl(_socket, F_SETFL, flags  & ~O_NONBLOCK);
    }
    else
    {
        err = fcntl(_socket, F_SETFL, flags  | O_NONBLOCK);
    }
    if(err==0)
    {
        fprintf(stderr,"socket set to %sblocking mode successful\n",(set ? "" : "non "));
    }
    else
    {
        fprintf(stderr,"changing to %sblocking mode on socket failed %d %s\n",(set ? "" : "non "), errno,strerror(errno));
    }
    
}

void enableSctpEvents(int _socket)
{
    struct sctp_event_subscribe event;
    int err;
    
    /**********************/
    /* ENABLING EVENTS    */
    /**********************/
    
    bzero((void *)&event, sizeof(struct sctp_event_subscribe));
    event.sctp_data_io_event            = 1;
    event.sctp_association_event        = 1;
    event.sctp_address_event            = 1;
    event.sctp_send_failure_event        = 1;
    event.sctp_peer_error_event            = 1;
    event.sctp_shutdown_event            = 1;
    event.sctp_partial_delivery_event    = 1;
    event.sctp_adaptation_layer_event    = 1;
    event.sctp_authentication_event        = 1;
#ifndef LINUX
    event.sctp_stream_reset_events        = 1;
#endif
    
    err = setsockopt(_socket, IPPROTO_SCTP, SCTP_EVENTS, &event, sizeof(event));
    if(err !=0)
    {
        fprintf(stderr,"setsockopt(IPPROTO_SCTP,SCTP_EVENTS,) failed %d %s\n",errno,strerror(errno));
    }
    else
    {
        fprintf(stderr,"setsockopt(IPPROTO_SCTP,SCTP_EVENTS) successful\n");
    }
}

void setLingerTime(int _socket, int linger_time)
{
    int err;
    
    struct    linger xlinger;
    bzero(&xlinger,sizeof(xlinger));
    if(linger_time == 0)
    {
        xlinger.l_onoff = 1;
    }
    else
    {
        xlinger.l_onoff = 1;
    }
    xlinger.l_linger = linger_time;
    err = setsockopt(_socket, SOL_SOCKET, SO_LINGER,  &xlinger,sizeof(xlinger));
    if(err !=0)
    {
        fprintf(stderr,"setsockopt(SOL_SOCKET,SO_LINGER,%d) failed %d %s\n",linger_time,errno,strerror(errno));
    }
    else
    {
        fprintf(stderr,"setsockopt(SOL_SOCKET,SO_LINGER,%d) successful\n",linger_time);
    }
}

void setReuseAddr(int _socket)
{
    int flags = 1;
    int err = setsockopt(_socket, SOL_SOCKET, SO_REUSEADDR, (char *)&flags, sizeof(flags));
    if(err !=0)
    {
        fprintf(stderr,"setsockopt(,SOL_SOCKET,SO_REUSEADDR,1) failed %d %s\n",errno,strerror(errno));
    }
    else
    {
        fprintf(stderr,"setsockopt(SOL_SOCKET,SO_REUSEADDR,1) successful\n");
    }
}

void setReusePort(int _socket)
{
#if defined(SCTP_REUSE_PORT)
    
    int flags = 1;
    int err = setsockopt(_socket, IPPROTO_SCTP, SCTP_REUSE_PORT, (char *)&flags, sizeof(flags));
    if(err !=0)
    {
        fprintf(stderr,"setsockopt(,IPPROTO_SCTP,SCTP_REUSE_PORT,1) failed %d %s\n",errno,strerror(errno));
    }
    else
    {
        fprintf(stderr,"setsockopt(IPPROTO_SCTP,SCTP_REUSE_PORT,1) successful\n");
    }
#else
    fprintf(stderr,"setsockopt(IPPROTO_SCTP,SCTP_REUSE_PORT,1) not supported on platform\n");

#endif
}

void setNodelay(int _socket)
{
    int flags = 1;
    int err = setsockopt(_socket, IPPROTO_SCTP, SCTP_NODELAY, (char *)&flags, sizeof(flags));
    if(err !=0)
    {
        fprintf(stderr,"setsockopt(IPPROTO_SCTP,SCTP_NODELAY,1) failed %d %s\n",errno,strerror(errno));
    }
    else
    {
        fprintf(stderr,"setsockopt(IPPROTO_SCTP,SCTP_NODELAY,1) successful\n");
    }
}

void setIPDualStack(int _socket)
{
    int flag = 0;
    int err = setsockopt(_socket, IPPROTO_IPV6, IPV6_V6ONLY, (char *)&flag, sizeof(flag));
    if(err !=0)
    {
        fprintf(stderr,"setsockopt(IPPROTO_IPV6,IPV6_V6ONLY,0) failed %d %s\n",errno,strerror(errno));
    }
    else
    {
        fprintf(stderr,"setsockopt(IPPROTO_IPV6,IPV6_V6ONLY,0) successful\n");
    }
}

void printStatus(int _socket)
{
    struct sctp_status status;
    socklen_t opt_len;
    
    memset((void *)&status, 0, sizeof(struct sctp_status));
    opt_len = (socklen_t)sizeof(struct sctp_status);
    if (getsockopt(_socket, IPPROTO_SCTP, SCTP_STATUS, &status, &opt_len) < 0)
    {
        perror("getsockopt");
    }
    fprintf(stderr,"----SCTP-STATUS-----------------------------------\n");
    fprintf(stderr,"status.sstat_assoc_id = %lu\n",(unsigned long)status.sstat_assoc_id);
    fprintf(stderr,"status.sstat_state = %lu\n", (unsigned long)status.sstat_state);
    fprintf(stderr,"status.sstat_rwnd = %lu\n", (unsigned long)status.sstat_rwnd);
    fprintf(stderr,"status.sstat_unackdata = %lu\n", (unsigned long)status.sstat_unackdata);
    fprintf(stderr,"status.sstat_penddata = %lu\n", (unsigned long)status.sstat_penddata);
    fprintf(stderr,"status.sstat_instrms = %lu\n", (unsigned long)status.sstat_instrms);
    fprintf(stderr,"status.sstat_outstrms = %lu\n", (unsigned long)status.sstat_outstrms);
    fprintf(stderr,"status.sstat_fragmentation_point = %lu\n", (unsigned long)status.sstat_fragmentation_point);
    fprintf(stderr,"--------------------------------------------------\n");


}
