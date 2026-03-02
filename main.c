#include <stdio.h>      // printf
#include <netinet/in.h> // sockaddr_in, INADDR_ANY
#include <sys/socket.h> // socket, bind, listen, accept, AF_INET, SOCK_STREAM
#include <sys/types.h>  // htonl, htons, socklen_t
#include <unistd.h>     // read, write, close
#include <string.h>
#include <stdlib.h>
#include <stdint.h>
#include <regex.h>
#include <stdbool.h>

#define LOGd(expr) printf(#expr " = %d\n", (expr))

#define MAX_PATH_SIZE 256
#define MAX_LINE_SIZE 256

/* #define HOSTNAME "localhost" */
const char* HOSTNAME = "localhost";

int sock_fd;
int con_fd;

void remove_newline(char* str) {
    int l = strlen(str);
    if (str[l-1]=='\n')
        str[l-1]=0;
    if (str[l-2]=='\r')
        str[l-2]=0;
}

void send_text(char* line) {
    dprintf(con_fd,"i%s\t\terror.host\t1\r\n",line);
}
void send_link(char* line, char* cur_path) {
    char* str = strdup(line);
    // format: @<`host` `mode``path`>`text`
    // `host` = hostname or . for the current host
    // `mode` = / or +, / being absolute and + being relative (add to the path)
    // `path` = the path to set or add
    // `link text` = any text to be displayed
    // NOTE: no space between mode and path

    int idx = 0;
    const int len = strlen(str); // save this for later

    if (str[idx] != '@') {
        // TODO: error
        printf("missing @\n");
        return;
    }
    idx++;

    if (str[idx] != '<') {
        // TODO: error
        printf("missing <\n");
        return;
    }
    idx++;

    // parse host
    int host_start = idx; // save start location
    while (idx<len && str[idx] != ' ') // skip non spaces
        idx++;
    str[idx] = 0; // we're on a space, set it to null so that (str+host_start) is the host string
    idx++;


    // parse mode
    bool is_rel = str[idx] == '+';
    if (!is_rel && str[idx] != '/') { // if `mode` isnt + or / then...
        // TODO: error
        printf("bad mode\n");
        return;
    }
    idx++;

    int path_start = idx;
    while (idx<len && str[idx] != '>') // skip non `>`s
        idx++;
    str[idx] = 0;
    int path_len = idx - path_start;
    idx++;

    int text_start = idx;

    // parsing done!

    // now make the new path
    char* path = malloc((is_rel ? (strlen(cur_path)+1+path_len) : (path_len)) + 1);
    path[0]=0;
    if (is_rel)
        strcat(path, cur_path);
    strcat(path,"/");
    strcat(path,str+path_start);

    // make text and host strings bc it looks nice
    char* text = str+text_start;
    char* host = str+host_start;

    /* printf("is_rel=%d\n",is_rel); */
    /* printf("host=%s\n",str+host_start); */
    /* printf("path=%s\n",str+path_start); */
    /* printf("text=%s\n",str+text_start); */

    /* printf("1%s\t%s\t%s\t70\n", text, path, host[0]=='.'?HOSTNAME:host); */
    dprintf(con_fd, "1%s\t%s\t%s\t70\r\n", text, path, host[0]=='.'?HOSTNAME:host);
}

char path_prefix[] = "content";
void do_request(char* path) {
    /* char* fname = malloc(strlen(path)+strlen(path_prefix)+1); */
    char fname[MAX_PATH_SIZE+100];
    fname[0]=0;
    strcat(fname,path_prefix);
    strcat(fname,path);

    if (strlen(path)==0)
        strcat(fname,"/index");

    /* printf("fname='%s'\n",fname); */

    if (strstr("..",fname) != NULL) {
        /* fname = "notfound"; */
        printf("sneaky guy...\n");
        strcpy(fname,"notfound");
    }

    FILE* f = fopen(fname,"r");

    if (f == NULL) {
        f = fopen("notfound","r");

        if (f == NULL) {
            dprintf(con_fd,"3server is broken, oops\t\terror.host\t1\r\n.\r\n");
            return;
        }
    }

    /* printf("file opened\n"); */

    char line[MAX_LINE_SIZE];

    while (fgets(line, sizeof(line), f)) {
        remove_newline(line);
        if (line[0]=='@') {
            /* printf("link\n"); */
            send_link(line,path);
        } else if (line[0]=='!') {
            dprintf(con_fd,"%s\r\n",line+1);
        } else {
            /* printf("text\n"); */
            send_text(line);
        }
    }
    /* printf("done\n"); */
    dprintf(con_fd,".\r\n");
}

void onexit() {
    close(con_fd);
    close(sock_fd);
}

int main(int argc, char** argv) {

    atexit(onexit);
    struct sockaddr_in server_sockaddr_in; // Initialize the details of the server socket
    server_sockaddr_in.sin_family = AF_INET; // Define socket family AF_INET = internetwork: UDP, TCP, etc.

    // https://linux.die.net/man/3/htonl
    // The htonl() function converts the unsigned integer hostlong from host byte order to network byte order.
    server_sockaddr_in.sin_addr.s_addr = htonl(INADDR_ANY);

    int port = 7070;
    for (int i=1;i<argc;i++) {
        if (!strcmp(argv[i],"-port"))
            port = atoi(argv[++i]);
        else if (!strcmp(argv[i],"-host"))
            HOSTNAME = argv[++i];
        else
            printf("idk what this arg means: '%s'\n",argv[i]);
    }
    // The htons() function converts the unsigned short integer hostshort from host byte order to network byte order.
    server_sockaddr_in.sin_port = htons(port);

    // https://man7.org/linux/man-pages/man2/socket.2.html
    // creates an endpoint for communication and returns a file descriptor that refers to that endpoint
    // SOCK_STREAM defines that this should communicate over TCP
    sock_fd = socket(AF_INET, SOCK_STREAM, 0);

    // https://man7.org/linux/man-pages/man2/bind.2.html
    // bind() assigns the address specified by server_sockaddr_in to the socket sock_fd
    int a = bind(sock_fd, (struct sockaddr *)&server_sockaddr_in, sizeof(server_sockaddr_in));
    if (a == -1) {
        perror("bind()");
        exit(1);
    }

    // https://man7.org/linux/man-pages/man2/listen.2.html
    // listen() marks the socket referred to by sockfd as a passive socket
    // the second parameter (5) defines the maximum length to which the queue of pending connections for sock_fd may grow
    listen(sock_fd, 5);
    printf("listening on %d\n",port);

    struct sockaddr_in client_sockaddr_in;
    socklen_t len = sizeof(client_sockaddr_in);

    while (1) {
        // https://man7.org/linux/man-pages/man2/accept4.2.html
        // accept() extracts the first connection request on the queue of pending connections for the listening socket
        // The address info from the client will be stored in client_sockaddr_in
        
        printf("waiting for accept()\n");
        con_fd = accept(sock_fd, (struct sockaddr *)&client_sockaddr_in, &len);
        /* printf("got fd %d\n",con_fd); */
        if (con_fd == -1) {
            perror("accept()");
            exit(1);
        }

        char path_buf[MAX_PATH_SIZE] = {};

        // https://man7.org/linux/man-pages/man2/read.2.html
        // read() attempts to read up to MAX_PATH_SIZE bytes from file descriptor con_fd into path_buf
        read(con_fd, path_buf, sizeof(path_buf));
        remove_newline(path_buf);
        printf("request: '%s'\n", path_buf);

        do_request(path_buf);

        // https://man7.org/linux/man-pages/man2/close.2.html
        // close() closes a file descriptor, so that it no longer refers to any file and may be reused
        close(con_fd);
    }

    close(sock_fd);

    return 0;
}
