#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <error.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <netdb.h> //getaddrinfo
#include <netinet/in.h>
#include <time.h>
#define MAXLEN 65535 //Buffer length will have to be 1MB+1, so this may change or be replaced
#define DEFAULTPORT 45667
#define HTMLLEN 138 //maybe? +2 if the leading \r\n counts...

/* Alright, this line will be more formal later, I guess. But for now:
  init:       Deepen
  dnslookup:  Deepen
  parse:      Craig
  main:       Mostly Deepen for now.
  gentime:    Craig
  generror:   Craig
  I(Craig) moved the things that are supposed to be functions into functions
  and renamed some variables such that I can easily distinguish them
  If the variable names:
  -servaddr for the address we bind our server too (was servsock)
  -serversock for the socket fd for the server's listening socket (was sersocket)
  -clientsock for the socket fd of the connected client (was clisock)
  -websock for the socket fd of the webserver we connect to for the client (was clientsock)
  don't work for someone we can come up with some different ones
  As things coalesce into this file, of course add more credits! 
  Ah, I use the second-line { rather than in-line {, maybe I shouldn't have changed that?
  Looks cleaner if it's consistent though...*/

int init(int *serversock, int port)
{
  *serversock = socket(AF_INET, SOCK_STREAM, 0);
  if (serversock < 0)
  {
    perror("Can't open socket!\n");
    return -1;
  }

  struct sockaddr_in servaddr;

  memset(&servaddr, 0,  sizeof(servaddr));
  servaddr.sin_family = AF_INET;
  servaddr.sin_port = htons(port);
  servaddr.sin_addr.s_addr = htonl(INADDR_ANY);
  if(bind(*serversock, (struct sockaddr *)&servaddr, sizeof(servaddr)) < 0)
  {
    perror("Bind error");
    return -1;
  }
}

int go()
{
  //move stuff from main to here later
}

int dnslookup(char* host, char* port, struct addrinfo *res)
{
    struct addrinfo hints;

    memset(&hints, 0, sizeof(struct addrinfo));
    hints.ai_flags = AI_ADDRCONFIG;       //To select only IPv4 Addresses
    hints.ai_family = AF_INET;
    hints.ai_socktype = SOCK_STREAM;
    hints.ai_protocol = 0;
    hints.ai_addr = NULL;
    hints.ai_next = NULL;
      

    return getaddrinfo (host, port, &hints, &res);
}

void cleanup(int serversock,int clientsock,int websock)
{
  close(serversock);
  close(clientsock);
  close(websock);
}

int parse(char* getreq, char* host, char* port, char* live)
{
  char s1[MAXLEN], s2[MAXLEN], curline[MAXLEN];
  short res=0, split=1;
  sscanf(getreq,"GET %s HTTP/1.%*c\r\n",s1);
  if(curline[0]!='/')//the host is part of the url
  {
    sscanf(s1,"%[^/]%*s",curline);
    res=sscanf(curline,"%[^:\r\n\0\f\t\v ]:%[0-9]",s1,s2);
    strcpy(host,s1);
    if(res==2)//we have a port
      strcpy(port,s2);
    else
      strcpy(port,"80");//default to port 80
  }
  res=0;
  while(getreq<getreq+MAXLEN)
  {
    sscanf(getreq,"%[^\r\n\0]\r\n",curline);
    if((res=sscanf(curline,"Host: %[^:\r\n\0\f\t\v ]:%[0-9]",s1,s2)))
    {
      strcpy(host,s1);
      if(res==2)//we have both a host and a port
        strcpy(port,s2);
      else
        strcpy(port,"80");//default to port 80
    }
    else if(sscanf(curline,"Connection: %s",s1))
      strcpy(live,s1);//return the keep-alive info
    else if(strcmp(getreq,"\r\n")==0)
    {
      split=0;//we've hit the end. if we don't get here, it's a split request.
      break;
    }
    getreq+=strlen(curline)+2;
  }
  return 1;//0 returned if normal, 1 if request split
}

int gentime(char* timestring)//creates an http format date line
{
  time_t ctime;
  ctime = time(NULL);
  strftime(timestring,MAXLEN,"%a, %d %b %Y %H:%M:%S GMT",gmtime(&ctime));//day, date, month, year, time
  return 0;
}

int generror(int code,char* response)//create http responses to various errors
{
  char etitle[1024], emessage[MAXLEN], timestring[MAXLEN];
  gentime(timestring);
  if(code==400)//bad request error
  {
    strcpy(etitle,"Bad Request");
    strcpy(emessage,"Your browser sent a request that this server could not understand.");
  }
  else if(code==403)//forbidden error (we blocked a website)
  {
    strcpy(etitle,"Forbidden");//I think?
    strcpy(emessage,"This site is forbidden by the proxy server.");//will look up/ask tanu+surya if they looked it up. If there is a standard message at all.
  }
  else if(code==431)//We have a request larger than 1 MB
  {
    strcpy(etitle,"Request Header Fields Too Large");
    strcpy(emessage,"This proxy server only supports requests of up to 1 MB");
  }
  else
    return -1;//don't want to be doing weird things with uninitialized strings, though this *should* never be called without a valid code
  sprintf(response, "HTTP/1.1 %i %s\r\n"
            "Date: %s\r\n"
            "Server: CS656proxyG2\r\n"
            "Content-length: %i\r\n"
            "Connection: close\r\n"
            "Content-Type: text/html; charset=iso-8859-1\r\n"
            "\r\n"
            "<!DOCTYPE HTML PUBLIC \"-//IETF//DTD HTML 2.0//EN\">\n" //51
            "<html><head>\n" //14
            "<title>%i %s</title>\n" //20 (all response codes are 3 characters, so the %i will always be length 3)
            "</head><body>\n" //14
            "<h1>%s</h1>\n" //10
            "<p>%s<br />\n" //10
            "</p>\n" //5
            "</body></html>",code,etitle,timestring,2*strlen(etitle)+strlen(emessage)+HTMLLEN,code,etitle,etitle,emessage); //14
  return 0;
}

int main (int argc, char **argv)
{
  short inport;
  int serversock,clientsock,websock;
  char port[MAXLEN], host[MAXLEN], alive[MAXLEN];

  if(argc>=2)
    inport = atoi(argv[1]);
  else
    inport = DEFAULTPORT;

  if(init(&serversock, inport)==-1)
    return -1;//we couldn't initialize, port problems?
  listen (serversock, 30);
  //while(1)//This should loop, but I'll leave it open for now to test if my shifting things around broke anything
  //{//should probably declare some of the things declared inside the loop outside. overhead...
    clientsock = accept(serversock, (struct sockaddr *)NULL, NULL);
    if (clientsock < 0)
      perror("Accept error");

    char browser_input[MAXLEN];
    int red = read(clientsock, &browser_input, sizeof(browser_input));
    //split request handling should go here
    parse(browser_input,host,port,alive);
    if(red < 0)
      perror("Didn't receive");

    struct addrinfo *res, *rp;
    dnslookup(host,port,res);
  	
    for (rp = res; rp != NULL; rp = rp->ai_next) 
    {
      websock = socket(rp->ai_family, rp->ai_socktype, rp->ai_protocol);
        if (websock == -1)
          continue;
          
        if (connect(websock, rp->ai_addr, rp->ai_addrlen) != -1)
           break;
    }
    if (rp == NULL) 
    {               //No address
      fprintf(stderr, "Could not connect\n");
      cleanup(serversock,clientsock,websock);
      exit(1);
  	}
    int wr = write(websock, &browser_input, sizeof(browser_input));
    //Writing it to the web server of the host

    char br_response[65532];
    int rd = read(websock, &br_response, sizeof(br_response));
    //printf("Response: %s\n", br_response);
    //Parse response for content-length/chunk info here, use that to loop appropriately
    write(clientsock, &br_response, sizeof(br_response));
  //}
  freeaddrinfo(res);           //Free up the structure
  cleanup(serversock,clientsock,websock);
  exit(0);
}