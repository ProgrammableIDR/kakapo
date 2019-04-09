
/* kakapo-session - a BGP traffic source and sink */

#include <stdio.h>
#include <errno.h>
#include <sys/socket.h>
#include <arpa/inet.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <netinet/in.h>
#include <netinet/tcp.h>
#include <sys/sendfile.h>
#include <fcntl.h>
#include <assert.h>
#include <sys/time.h>
#include <pthread.h>
#include <linux/sockios.h>
#include <net/if.h>
#include <sys/ioctl.h>

#include "libutil.h"
#include "session.h"
#include "kakapo.h"
#include "stats.h"

#define MAXPENDING 5    // Max connection requests
#define BUFFSIZE 0x10000

void *session(void *x){
// from here on down all variables are function, and thus thread, local.
struct sessiondata *sd = (struct sessiondata *) x;
slp_t slp;

uint32_t  localip,peerip;

int i,msgtype;
struct sockbuf sb;
struct timeval t_active, t_idle;
int active = 0;
int sock = sd->sock;
char *tid;
int tmp=asprintf(&tid,"%d-%d: ",pid,sd->tidx);

void getsockaddresses () {
  struct sockaddr_in sockaddr;
  int socklen;

  memset(&sockaddr, 0, SOCKADDRSZ); socklen = SOCKADDRSZ; ((0 == getsockname(sd->sock,&sockaddr,&socklen) && (socklen==SOCKADDRSZ)) || die ("Failed to find local address"));
  localip = sockaddr.sin_addr.s_addr;
  //fprintf(stderr, "%d: local address %s\n",pid, inet_ntoa(sockaddr.sin_addr));

  memset(&sockaddr, 0, SOCKADDRSZ); socklen = SOCKADDRSZ; ((0 == getpeername(sd->sock,&sockaddr,&socklen) && (socklen==SOCKADDRSZ)) || die ("Failed to find peer address"));
  peerip = sockaddr.sin_addr.s_addr;
  //fprintf(stderr, "%d: peer address %s\n",pid, inet_ntoa(sockaddr.sin_addr));

  //fprintf(stderr, "%d: connection info: %s/%s\n",pid, fromHostAddress(localip),fromHostAddress(peerip));
  fprintf(stderr, "%s: connection info: %s/",tid, fromHostAddress(localip));
  fprintf(stderr, "%s\n",fromHostAddress(peerip));
};

unsigned char keepalive [19]={ 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0, 19, 4 };
unsigned char marker [16]={ 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff };
const char *hexmarker = "FFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFF";

char *bgpopen(int as, int holdtime, int routerid, char *hexoptions) {
    // //fprintf(stderr,"===%s,%04x\n", fromHostAddress(routerid), routerid);
    // //fprintf(stderr,"===%s,%04x\n", fromHostAddress(localip), localip);
    if (NULL==hexoptions) { // then we should build our own AS4 capability using the provided AS number
        hexoptions = concat ("02064104",hex32(as));
    };
 
    char * hexmessage = concat (hex8(4), hex16(as), hex16(holdtime), hex32(routerid), hex8(strlen(hexoptions)/2), hexoptions, NULL);
    int messagelength = strlen(hexmessage) / 2 + 19;
    // return concat (hexmarker,hex16(messagelength),hex8(1),hexmessage,NULL);
    char* ret = concat (hexmarker,hex16(messagelength),hex8(1),hexmessage,NULL);
    // //fprintf(stderr,"===%s,%s==%s===\n", fromHostAddress(routerid), hex32(routerid), ret);
    return ret;
};

int isMarker (const unsigned char *buf) {
   return ( 0 == memcmp(buf,marker,16));
}


void setactive () {
    active = 1;
    gettimeofday(&t_active, NULL);
};

void setidle () {
    active = 0;
    gettimeofday(&t_idle, NULL);
};

char * showtype (unsigned char msgtype) {
   switch(msgtype) {
      case 1 : return "OPEN";
          break;
      case 2 : return "UPDATE";
          break;
      case 3 : return "NOTIFICATION";
          break;
      case 4 : return "KEEPALIVE";
          break;
      default : return "UNKNOWN";
    }
}

void doopen(char *msg, int length) {
   unsigned char version = * (unsigned char*) msg;
   if (version != 4) {
      fprintf(stderr, "%s: unexpected version in BGP Open %d\n",tid,version);
   }
   uint16_t as       = ntohs ( * (uint16_t*) (msg+1));
   uint16_t holdtime = ntohs ( * (uint16_t*) (msg+3));
   struct in_addr routerid = (struct in_addr) {* (uint32_t*) (msg+5)};
   unsigned char opl = * (unsigned char*) (msg+9);
   unsigned char *hex = toHex (msg+10,opl) ;
   fprintf(stderr, "%s: BGP Open: as =  %d, routerid = %s , holdtime = %d, opt params = %s\n",tid,as,inet_ntoa(routerid),holdtime,hex);
   free(hex);
};

void printPrefix(char *pfx, int length) {
    uint32_t addr = ntohl(* ((uint32_t*) pfx));
    uint32_t mask = (0xffffffff >> (32-length)) << (32-length);
    uint32_t maskedaddr = htonl( addr & mask);
    struct in_addr inaddr = (struct in_addr) {maskedaddr};
    fprintf(stderr,"%s/%d\n",inet_ntoa(inaddr),length);
};

//simpleParseNLRI
int spnlri (char *nlri, int length) {
    int count = 0;
    int offset = 0;
    for (;offset<length;) {
        count++;
        if (nlri[offset] == 0)
            offset += 1;
        else if (nlri[offset] < 9)
            offset += 2;
        else if (nlri[offset] < 17)
            offset += 3;
        else if (nlri[offset] < 25)
            offset += 4;
        else if (nlri[offset] < 33)
            offset += 5;
        else {
            unsigned char *hex = toHex (nlri,length) ;
            fprintf(stderr, "**** %d %d %d %d %s \n",nlri[offset],offset,count,length,hex);
            assert(0);
        }
        if ((1 == VERBOSE) && (offset<length))
            printPrefix(nlri+offset+1,nlri[offset]);
    }

    // debug only
    if (offset != length) {
        unsigned char *hex = toHex (nlri,length) ;
        fprintf(stderr, "**** %d %d %d %d %s \n",nlri[offset],offset,count,length,hex);
    }
    assert (offset==length);
    return count;
}

void doupdate(char *msg, int length) {
   uint16_t wrl = ntohs ( * (uint16_t*) msg);
   assert (wrl < length-1);
   char *withdrawn = msg+2;
   uint16_t tpal = ntohs ( * (uint16_t*) (msg+wrl+2));
   char *pa = msg+wrl+4;
   assert (wrl + tpal < length-3);
   char *nlri = msg+wrl+tpal+4;
   uint16_t nlril = length - wrl - tpal - 4;
   int wc,uc;
   if ( wrl > 0 )
        wc = spnlri(withdrawn,wrl);
   else
        wc = 0;
   if ( nlril > 0 )
        uc = spnlri(nlri,nlril);
   else
        uc = 0;
   if (1 == VERBOSE)
       fprintf(stderr, "%s: BGP Update: withdrawn count =  %d, path attributes length = %d , NLRI count = %d\n",tid,wc,tpal,uc);
   updatelogrecord (slp, uc, wc);
};

void donotification(char *msg, int length) {
   unsigned char ec  = * (unsigned char*) (msg+0);
   unsigned char esc = * (unsigned char*) (msg+1);
   fprintf(stderr, "%s: BGP Notification: error code =  %d, error subcode = %d\n",tid,ec,esc);
};

int getBGPMessage (struct sockbuf *sb) {
   char *header;
   char *payload;
   int received;
   uint16_t pl;
   unsigned char msgtype;

   header = bufferedRead(sb,19);
   if (header == (char *) -1 ) {
      fprintf(stderr, "%s: end of stream\n",tid);
      return -1;
   } else if (0 == header ) {
      // zero is simply a timeout: -1 is eof
      if (active) {
          setidle();
      };
      return 0;
   } else if (!isMarker(header)) {
      die("Failed to find BGP marker in msg header from peer");
            return -1;
   } else {
      if (0==active) {
          setactive();
      };
      pl = ( ntohs ( * (uint16_t*) (header+16))) - 19;
      msgtype = * (unsigned char *) (header+18);
      if (0 < pl) {
         payload=bufferedRead(sb,pl);
         if (0 == payload) {
            fprintf(stderr, "%s: unexpected end of stream after header received\n",tid);
            return 0;
         }
     } else
         payload = 0;
   }
//   msgcount++;
   if (1 == VERBOSE) {
      unsigned char *hex = toHex (payload,pl) ;
      fprintf(stderr, "%s: BGP msg type %s length %d received [%s]\n",tid, showtype(msgtype), pl , hex);
      free(hex);
   }

   switch (msgtype) {
      case 1:doopen(payload,pl);
             break;
      case 2:doupdate(payload,pl);
             break;
      case 3:donotification(payload,pl);
             break;
      case 4: // keepalive, no analysis required
             break;
   };
   return msgtype;
}

void report (int expected, int got) {

   if (1 == VERBOSE) {
      if (expected == got) {
         fprintf(stderr, "%s: session: OK, got %s\n",tid,showtype(expected));
      } else {
         fprintf(stderr, "%s: session: expected %s, got %s (%d)\n",tid,showtype(expected),showtype(got),got);
      }
   } else {
      if (expected != got)
         fprintf(stderr, "%s: session: expected %s, got %s (%d)\n",tid,showtype(expected),showtype(got),got);
   }
}

void *sendthread (void *fd) {
  struct timeval t0, t1, td;

   int sendupdates (int seq) {
      struct timeval t0, t1 , td;
      if (VERBOSE)
          gettimeofday(&t0, NULL);

      // lseek(*(int *)fd,0,0);
      // (0 < sendfile(sock, *(int *)fd, 0, 0x7ffff000)) || die("Failed to send updates to peer");
      if (0 == (seq % 2))
          sendbs(sock,update ( nlris(toHostAddress("10.0.0.0"),30,4),
                               empty,
                               iBGPpath (toHostAddress("192.168.1.1"), (uint32_t []) {1,2,3,0})));
      else
          sendbs(sock,update ( empty, nlris(toHostAddress("10.0.0.0"),30,4), empty));

      if (VERBOSE) {
         gettimeofday(&t1, NULL);
         timeval_subtract(&td,&t1,&t0);
         fprintf(stderr, "%s: session: sendfile complete in %s\n",tid,timeval_to_str(&td));
      };
      return 0; // ask to be restarted...
   };

  if (0 == SLEEP) {
     if (VERBOSE)
        fprintf(stderr,"sendupdates singleshot mode\n");
     sendupdates(0);
  } else {
     if (VERBOSE)
        fprintf(stderr,"sendupdates looping at %f\n", SLEEP / 1000.0);
     timedloopms ( SLEEP ,sendupdates);
  };
};

long int threadmain() {

  int fd1,fd2;
  getsockaddresses();
  if ((fd1 = open(sd->fn1,O_RDONLY)) < 0) {
    die("Failed to open BGP Open message file");
  }

  if ((fd2 = open(sd->fn2,O_RDONLY)) < 0) {
    die("Failed to open BGP Update message file");
  }


  setsockopt( sock, IPPROTO_TCP, TCP_NODELAY, (void *)&i, sizeof(i));
  lseek(fd1,0,0);
  lseek(fd2,0,0);
  bufferInit(&sb,sock,BUFFSIZE,TIMEOUT);

  // (0 < sendfile(sock, fd1, 0, 0x7ffff000)) || die("Failed to send fd1 to peer");

  //char * m = bgpopen(65001,180,htonl(inet_addr("192.168.122.123")),"020641040000fde8");
  char * m = bgpopen(sd->as,180,htonl(localip),NULL); // let the code build the optional parameter :: capability
  int ml = fromHex(m);
  (0 < send(sock, m, ml, 0)) || die("Failed to send synthetic open to peer");

  do
      msgtype=getBGPMessage (&sb); // this is expected to be an Open
  while (msgtype==0);

  report(1,msgtype);
  if (1 != msgtype)
    goto exit;

  (0 < send(sock, keepalive, 19, 0)) || die("Failed to send keepalive to peer");

  do
      msgtype=getBGPMessage (&sb); // this is expected to be a Keepalive
  while (msgtype==0);

  report(4,msgtype);
  if (4 != msgtype)
    goto exit;

  pthread_t thrd;
  pthread_create(&thrd, NULL, sendthread, &fd2);

  setidle();
  slp = initlogrecord(sd->tidx,tid);  // implies that the rate display is based at first recv request call rather than return......
                    // for more precision consider moving to either getBGPMessage
                    // would be too late otherwise anywhere in here, as getBGPMessage will call updatelog

  while (1) {
        msgtype = getBGPMessage (&sb); // keepalive or updates from now on
        switch (msgtype) {
        case 2: // Update
            break;
        case 4: // Keepalive
            (0 < send(sock, keepalive, 19, 0)) || die("Failed to send keepalive to peer");
            break;
        case 0: // this is an idle recv timeout event
            break;
        case 3: // Notification
            fprintf(stderr, "%s: session: got Notification\n",tid);
            goto exit;
        default:
            if (msgtype<0){ // all non message events except recv timeout
                fprintf(stderr, "%s: session: end of stream\n",tid);
            } else { // unexpected BGP message - unless BGP++ it must be an Open....
                report(2,msgtype);
            }
            goto exit;
        }
  };
exit:
  closelogrecord(slp,sd->tidx);
  pthread_cancel(thrd);
  close(sock);
  close(fd1);
  close(fd2);
  fprintf(stderr, "%s: session exit\n",tid);
  free(sd);
} // end of threadmain

  // effective start of 'main, i.e. function 'session'
  // code and variables are defined within 'session' to ensure that the variables are thread local,
  // and to allow inner functions access to those local variables

  return (int*)threadmain();
}
