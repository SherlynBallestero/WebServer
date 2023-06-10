#include "wslibs.h"

void doit(int fd, char *hostname);
void read_requesthdrs(rio_t *rp);
int parse_uri(char *uri, char *filename, char *cgiargs, int display_order, char *host_html);
int is_download_file_uri(char *uri);
void serve_static(int fd, char *filename, int filesize);
void get_filetype(char *filename, char *filetype);
void serve_dynamic(int fd, char *filename, char *cgiargs);
void clienterror(int fd, char *cause, char *errnum, char *shortmsg, char *longmsg);
int valid_uri(char *uri, int *is_download_file_uri, int *display_order);
int string_to_int_parse(char *p);
void update_current_dir(char *current_dir, char *hostname);
void merge_sort(int a, int b, char **names, char **types, char **times, char **readoks, char **sizes, int display_order);
void merge(int a, int b, int c, char **names, char **types, char **times, char **readoks, char **sizes, int display_order);
void erase_white_space_substrings(char *uri);

void sigchld_handler(int sig) /*Handler for SIGCHLD*/
{
    while (waitpid(-1, 0, WNOHANG) > 0);
    return;
}

char *root_dir;            //Root dir of the server. This is the position where the server will begin in every client
char current_dir[MAXLINE]; //Current directory of the server.

int main(int argc, char **argv)
{
    int listenfd, connfd;
    char hostname[MAXLINE], port[MAXLINE];
    socklen_t clientlen;
    struct sockaddr_storage clientaddr;

    /* Check command line args */
    if (argc != 3)
    {
        fprintf(stderr, "usage: %s <port> <root_dir>\n", argv[0]);
        exit(1);
    }

    /*Printing the port and the serving directory*/
    printf("Listening in port: %s\nServing Directory: %s\n", argv[1], argv[2]);
    root_dir = realpath(argv[2], NULL);
    strcpy(current_dir, root_dir);

    signal(SIGCHLD, sigchld_handler); //Installing the handler
    listenfd = Open_listenfd(argv[1]);
    while (1)
    {
        clientlen = sizeof(clientaddr);
        connfd = Accept(listenfd, (SA *)&clientaddr, &clientlen); //line:netp:tiny:accept
        Getnameinfo((SA *)&clientaddr, clientlen, hostname, MAXLINE,
                    port, MAXLINE, 0);
        printf("Accepted connection from (%s, %s)\n", hostname, port);
        if (fork() == 0)
        {
            Close(listenfd);        /* Child closes its listening socket */
            doit(connfd, hostname); //line:netp:tiny:doit /* Child services client */
            Close(connfd);          /* Child closes connection with client */
            exit(0);                /* Child exits */
        }
        //wait(NULL);
        Close(connfd); /* Parent closes connected socket (important!) */
    }

    return 0;
}

/*
 * doit - handle one HTTP request/response transaction
 */
void doit(int fd, char *hostname)
{
    int is_static, is_download_file_uri = 0, display_order = 0;
    struct stat sbuf;
    char buf[MAXLINE], method[MAXLINE], uri[MAXLINE], version[MAXLINE];
    char filename[MAXLINE], cgiargs[MAXLINE];
    rio_t rio;

    update_current_dir(current_dir, hostname); /*Here we update the current hostname of the client*/
    if (current_dir[strlen(current_dir) - 1] == '/')
        current_dir[strlen(current_dir) - 1] = '\0';

    /* Read request line and headers */
    Rio_readinitb(&rio, fd);
    if (!Rio_readlineb(&rio, buf, MAXLINE)) //line:netp:doit:readrequest
        return;
    printf("%s", buf);
    sscanf(buf, "%s %s %s", method, uri, version); //line:netp:doit:parserequest
    erase_white_space_substrings(uri);//Here we delete the %20 special substring for spaces in uris requests

    if (root_dir[0] == '.')
    {
        for (int i = strlen(uri); i >= 0; i--)
            uri[i + 1] = uri[i];
        uri[0] = '.';
    }
    printf("%s %s\n", uri, current_dir);

    if (strcasecmp(method, "GET"))
    { //line:netp:doit:beginrequesterr
        clienterror(fd, method, "501", "Not Implemented",
                    "RDWebServer does not implement this method");
        return;
    }                       //line:netp:doit:endrequesterr
    read_requesthdrs(&rio); //line:netp:doit:readrequesthdrs

    if (!valid_uri(uri, &is_download_file_uri, &display_order)) //If the uri is not valid(does not correspond to a direction obtained by clicking a directory or an archive in the current serving dir) then return a 404 error page
    {
        clienterror(fd, filename, "404", "Not found",
                    "RDWebServer couldn't find this file");
        return;
    }

    if (is_download_file_uri) /*If the uri points to a file, then send it to the client*/
    {
        int file_fd = open(uri, O_RDONLY, 0); //Opening the file descriptor of the file to be given to the client
        unsigned long file_bytes_size = 0;
        struct stat file_stats; //Getting the metadata from the file
        stat(uri, &file_stats);
        file_bytes_size = file_stats.st_size; //Getting the size of the file

        /* Send response headers to client */
        sprintf(buf, "HTTP/1.1 200 OK\r\n"); //line:netp:servestatic:beginserve
        Rio_writen(fd, buf, strlen(buf));
        sprintf(buf, "Server: RDWebServer\r\n");
        Rio_writen(fd, buf, strlen(buf));
        sprintf(buf, "Content-length: %d\r\n", file_bytes_size);
        Rio_writen(fd, buf, strlen(buf));
        sprintf(buf, "Content-Disposition: attachment\r\n\r\n"); //With this line of the response we indicate to te client that we will use
        Rio_writen(fd, buf, strlen(buf));                        //line:netp:servestatic:endserve

        sendfile(fd, file_fd, NULL, file_bytes_size);
        close(file_fd);
        is_download_file_uri = 0;
        return;
    }

    char host_html[MAXLINE] = "./";
    strcat(host_html, hostname);
    strcat(host_html, ".html"); //We set the name of the .html corresponding to the client hostname
    /* Parse URI from GET request */
    is_static = parse_uri(uri, filename, cgiargs, display_order, host_html); //line:netp:doit:staticcheck
    if (stat(host_html, &sbuf) < 0)
    { //line:netp:doit:beginnotfound
        clienterror(fd, filename, "404", "Not found",
                    "RDWebServer couldn't find this file");
        return;
    } //line:netp:doit:endnotfound

    if (is_static)
    { /* Serve static content */
        if (!(S_ISREG(sbuf.st_mode)) || !(S_IRUSR & sbuf.st_mode))
        { //line:netp:doit:readable
            clienterror(fd, filename, "403", "Forbidden",
                        "RDWebServer couldn't read the file");
            return;
        }
        serve_static(fd, host_html, sbuf.st_size); //line:netp:doit:servestatic
    }
    else
    { /* Serve dynamic content */
        if (!(S_ISREG(sbuf.st_mode)) || !(S_IXUSR & sbuf.st_mode))
        { //line:netp:doit:executable
            clienterror(fd, filename, "403", "Forbidden",
                        "RDWebServer couldn't run the CGI program");
            return;
        }
        serve_dynamic(fd, filename, cgiargs); //line:netp:doit:servedynamic
    }
}

/*
 * read_requesthdrs - read HTTP request headers
 */
void read_requesthdrs(rio_t *rp)
{
    char buf[MAXLINE];

    Rio_readlineb(rp, buf, MAXLINE);
    printf("%s", buf);
    while (strcmp(buf, "\r\n"))
    { //line:netp:readhdrs:checkterm
        Rio_readlineb(rp, buf, MAXLINE);
        printf("%s", buf);
    }
    return;
}

/*
 * parse_uri - parse URI into filename and CGI args
 *             return 0 if dynamic content, 1 if static
 */
int parse_uri(char *uri, char *filename, char *cgiargs, int display_order, char *host_html)
{
    char *ptr;

    // if (!strcmp(uri, "/") || !strcmp(uri, current_dir))
    if (!strcmp(uri, "/"))
    {
        char dir_cpy[MAXLINE];
        for (int i = 0; i < MAXLINE; i++)
            dir_cpy[i] = '\0';
        strcpy(dir_cpy, current_dir);
        open_read_dir(dir_cpy, display_order, host_html); //reading the serving directory and creating the home.html file
    }
    else
    {
        for (int i = 0; i < strlen(current_dir); i++)
            current_dir[i] = '\0';
        if (display_order)
        {
            int length_uri = strlen(uri);
            while (uri[length_uri - 1] != '/')
            {
                uri[length_uri - 1] = '\0';
                length_uri--;
            }
        }
        // if(uri[0]!='.')
        // {
        if (uri[strlen(uri) - 1] == '/')
            uri[strlen(uri) - 1] = '\0';
        // }
        strcpy(current_dir, uri);
        printf("%s\n", uri);
        printf("%s\n", current_dir);
        open_read_dir(uri, display_order, host_html);
    }

    if (!strstr(uri, "cgi-bin"))
    { /* Static content */                 //line:netp:parseuri:isstatic
        strcpy(cgiargs, "");               //line:netp:parseuri:clearcgi
        strcpy(filename, ".");             //line:netp:parseuri:beginconvert1
        strcat(filename, uri);             //line:netp:parseuri:endconvert1
        if (uri[strlen(uri) - 1] == '/')   //line:netp:parseuri:slashcheck
            strcat(filename, "home.html"); //line:netp:parseuri:appenddefault
        return 1;
    }
}

/*
 * serve_static - copy a file back to the client 
 */
void serve_static(int fd, char *filename, int filesize)
{
    int srcfd;
    char *srcp, filetype[MAXLINE], buf[MAXBUF];

    /* Send response headers to client */
    get_filetype(filename, filetype);    //line:netp:servestatic:getfiletype
    sprintf(buf, "HTTP/1.1 200 OK\r\n"); //line:netp:servestatic:beginserve
    Rio_writen(fd, buf, strlen(buf));
    sprintf(buf, "Server: RDWebServer\r\n");
    Rio_writen(fd, buf, strlen(buf));
    sprintf(buf, "Content-length: %d\r\n", filesize);
    Rio_writen(fd, buf, strlen(buf));
    sprintf(buf, "Content-type: %s\r\n\r\n", filetype);
    Rio_writen(fd, buf, strlen(buf)); //line:netp:servestatic:endserve

    /* Send response body to client */
    srcfd = Open(filename, O_RDONLY, 0);                        //line:netp:servestatic:open
    srcp = Mmap(0, filesize, PROT_READ, MAP_PRIVATE, srcfd, 0); //line:netp:servestatic:mmap
    Close(srcfd);                                               //line:netp:servestatic:close
    Rio_writen(fd, srcp, filesize);                             //line:netp:servestatic:write
    Munmap(srcp, filesize);                                     //line:netp:servestatic:munmap
}

/*
 * get_filetype - derive file type from file name
 */
void get_filetype(char *filename, char *filetype)
{
    if (strstr(filename, ".html"))
        strcpy(filetype, "text/html");
    else if (strstr(filename, ".gif"))
        strcpy(filetype, "image/gif");
    else if (strstr(filename, ".png"))
        strcpy(filetype, "image/png");
    else if (strstr(filename, ".jpg"))
        strcpy(filetype, "image/jpeg");
    else
        strcpy(filetype, "text/plain");
}

/*
 * serve_dynamic - run a CGI program on behalf of the client
 */
void serve_dynamic(int fd, char *filename, char *cgiargs)
{
    char buf[MAXLINE], *emptylist[] = {NULL};

    /* Return first part of HTTP response */
    sprintf(buf, "HTTP/1.1 200 OK\r\n");
    Rio_writen(fd, buf, strlen(buf));
    sprintf(buf, "Server: RDWebServer\r\n");
    Rio_writen(fd, buf, strlen(buf));

    if (Fork() == 0)
    { /* Child */ //line:netp:servedynamic:fork
        /* Real server would set all CGI vars here */
        setenv("QUERY_STRING", cgiargs, 1);                         //line:netp:servedynamic:setenv
        Dup2(fd, STDOUT_FILENO); /* Redirect stdout to client */    //line:netp:servedynamic:dup2
        Execve(filename, emptylist, environ); /* Run CGI program */ //line:netp:servedynamic:execve
    }
    Wait(NULL); /* Parent waits for and reaps child */ //line:netp:servedynamic:wait
}

/*
 * clienterror - returns an error message to the client
 */
void clienterror(int fd, char *cause, char *errnum,
                 char *shortmsg, char *longmsg)
{
    char buf[MAXLINE];

    /* Print the HTTP response headers */
    sprintf(buf, "HTTP/1.1 %s %s\r\n", errnum, shortmsg);
    Rio_writen(fd, buf, strlen(buf));
    sprintf(buf, "Content-type: text/html\r\n\r\n");
    Rio_writen(fd, buf, strlen(buf));

    /* Print the HTTP response body */
    sprintf(buf, "<html><title>RDWebServer Error</title>");
    Rio_writen(fd, buf, strlen(buf));
    sprintf(buf, "<body bgcolor="
                 "ffffff"
                 ">\r\n");
    Rio_writen(fd, buf, strlen(buf));
    sprintf(buf, "%s: %s\r\n", errnum, shortmsg);
    Rio_writen(fd, buf, strlen(buf));
    sprintf(buf, "<p>%s: %s\r\n", longmsg, cause);
    Rio_writen(fd, buf, strlen(buf));
    sprintf(buf, "<hr><em>The RDWebServer</em>\r\n");
    Rio_writen(fd, buf, strlen(buf));
}

/*Function for opening a directory, reads its contents and generate an .html archive with the contents and its metadata*/
void open_read_dir(char *pathname, int display_order, char *host_html) //dir_data is a matrix that contains in each row a name of a file and metadata that belongs to it
{
    DIR *streamp;
    struct dirent *dep;
    char type[MAXLINE], readok[MAXLINE], d_name[MAXLINE], size[MAXLINE], time[MAXLINE];
    char *names_c[MAXFILES], *types_c[MAXFILES], *sizes_c[MAXFILES], *times_c[MAXFILES], *readoks_c[MAXFILES]; //Lists of metadatas from each content in the directory pointed by pathname

    if (pathname[strlen(pathname) - 1] != '/' || strlen(pathname) == 1)
        strcat(pathname, "/");

    int home_fd = open(host_html, O_WRONLY | O_TRUNC); //We open the home.html file where the main page is placed

    if (home_fd < 0) //If the home.html does not exist the we create it
    {
        close(home_fd);
        home_fd = open(host_html, O_CREAT | O_TRUNC);
        close(home_fd);
        home_fd = open(host_html, O_WRONLY | O_TRUNC);
    }

    /*Writting the header of home.html file*/
    write(home_fd, "<!DOCTYPE html> ", strlen("<!DOCTYPE html> "));
    write(home_fd, "<head><title>Directorio ", strlen("<head><title>Directorio "));
    write(home_fd, pathname, strlen(pathname));
    write(home_fd, "</title></head>\n", strlen("</title></head>\n"));
    write(home_fd, "<body> ", strlen("<body> "));
    write(home_fd, "<table> <tr> <th>Name</th> <th>Type</th> <th>Size</th> <th>Last Access</th> <th>Readeable</th> </tr>\n", strlen("<table> <tr> <th>Name</th> <th>Type</th> <th>Size</th> <th>Last Access</th> <th>Readeable</th> </tr>\n"));

    streamp = opendir(pathname); //We open the directory with the pathname location
    int index_files = 0;
    while ((dep = readdir(streamp)) != NULL && index_files < MAXFILES) //We read each content in the directory and get its metadata
    {
        struct stat stats;
        for (int i = 0; i < MAXLINE; i++)
        {
            type[i] = '\0';
            readok[i] = '\0';
            d_name[i] = '\0';
            size[i] = '\0';
            time[i] = '\0';
        }

        strcpy(d_name, dep->d_name); //Setting the name of the archive/folder
        char pathncpy[MAXLINE];
        strcpy(pathncpy, pathname);
        stat(strcat(pathncpy, d_name), &stats);
        if (S_ISREG(stats.st_mode)) /* Determine file type */
            strcpy(type, "regular");
        else if (S_ISDIR(stats.st_mode))
            strcpy(type, "directory");
        else
            strcpy(type, "other");
        if ((stats.st_mode & S_IRUSR)) /* Check read access */
            strcpy(readok, "yes");
        else
            strcpy(readok, "no");
        sprintf(size, "%lu", stats.st_size);
        strftime(time, sizeof(time), "%D:%H:%M:%S", localtime(&stats.st_atime));

        /*After getting the metadata from a file, then we store it in the relatives lists*/
        names_c[index_files] = (char *)malloc(strlen(d_name) + 1);
        types_c[index_files] = (char *)malloc(strlen(type) + 1);
        times_c[index_files] = (char *)malloc(strlen(time) + 1);
        readoks_c[index_files] = (char *)malloc(strlen(readok) + 1);
        sizes_c[index_files] = (char *)malloc(strlen(size) + 1);

        strcpy(names_c[index_files], d_name);
        strcpy(types_c[index_files], type);
        strcpy(times_c[index_files], time);
        strcpy(readoks_c[index_files], readok);
        if (!strcmp(types_c[index_files], "directory"))
            strcpy(sizes_c[index_files], "0");
        else
            strcpy(sizes_c[index_files], size);
        index_files++;
    }

    /*Now we use a Merge Sort for sorting the files, according to display_order value. 1-ByName, 2-BySize*/
    if (display_order)
        merge_sort(0, index_files - 1, names_c, types_c, times_c, readoks_c, sizes_c, display_order);

    /*Here we write the html code related with the table of contents from the directory*/
    for (int i = 0; i < index_files; i++)
    {
        char pathncpy[MAXLINE];
        strcpy(pathncpy, pathname);
        if (strcmp(names_c[i], "."))
        {
            // strcat(pathncpy, "/");
            strcat(pathncpy, names_c[i]);
        }
        /*Writing the row in the table with the current content metadata*/
        write(home_fd, "<tr>", strlen("<tr>"));
        write(home_fd, "<td> ", strlen("<td>"));
        write(home_fd, "<a href=\"", strlen("<a href=\""));
        if (!strcmp(names_c[i], "."))
            write(home_fd, pathncpy, strlen(pathncpy) - 1);
        else
            write(home_fd, pathncpy, strlen(pathncpy));
        // if(!strcmp(names_c[i],".") || !strcmp(names_c[i],".."))
        // {
        //     write(home_fd,names_c[i],strlen(names_c[i]));
        //     write(home_fd,"/",1);
        // }
        // else
        // {
        //     write(home_fd,"./",2);
        //     write(home_fd,names_c[i],strlen(names_c[i]));
        // }
        write(home_fd, "\">", strlen("\">"));

        write(home_fd, names_c[i], strlen(names_c[i]));

        write(home_fd, "</a> ", strlen("</a> "));
        write(home_fd, " </td>\n", strlen("</td>\n"));

        write(home_fd, "<td> ", strlen("<td>"));
        write(home_fd, types_c[i], strlen(types_c[i]));
        write(home_fd, " </td>\n", strlen("</td>\n"));

        write(home_fd, "<td> ", strlen("<td>"));
        if (!strcmp(types_c[i], "directory"))
            write(home_fd, "0", strlen("0"));
        else
            write(home_fd, sizes_c[i], strlen(sizes_c[i]));
        write(home_fd, " </td>\n", strlen("</td>\n"));

        write(home_fd, "<td> ", strlen("<td>"));
        write(home_fd, times_c[i], strlen(times_c[i]));
        write(home_fd, " </td>\n", strlen("</td>\n"));

        write(home_fd, "<td> ", strlen("<td>"));
        write(home_fd, readoks_c[i], strlen(readoks_c[i]));
        write(home_fd, " </td>\n", strlen("</td>\n"));

        write(home_fd, "</tr>\n", strlen("</tr>\n"));
    }

    /*Writing the down side of the main page*/
    write(home_fd, "</table>", strlen("</table>"));
    write(home_fd, "<n>ORDERING FUNCTIONS:</n>", strlen("<n>ORDERING FUNCTIONS:</n>"));

    write(home_fd, "<a href=\"", strlen("<a href=\""));
    write(home_fd, pathname, strlen(pathname));
    write(home_fd, "order?name\">", strlen("order?name\">"));
    write(home_fd, "Order By Name", strlen("Order By Name"));
    write(home_fd, "</a> ", strlen("</a> "));

    write(home_fd, "<a href=\"", strlen("<a href=\""));
    write(home_fd, pathname, strlen(pathname));
    write(home_fd, "order?size\">", strlen("order?size\">"));
    write(home_fd, "Order By Size", strlen("Order By Size"));
    write(home_fd, "</a> ", strlen("</a> "));

    write(home_fd, "</body>\n </html>\n", strlen("</body>\n </html>\n"));

    for (int i = 0; i < index_files; i++)
    {
        free(names_c[i]);
        free(types_c[i]);
        free(sizes_c[i]);
        free(readoks_c[i]);
        free(times_c[i]);
    }
    closedir(streamp);
    close(home_fd);
}

/*Function for verifiyng if an uri from a GET method is valid and the value is_download_file_uri is 1 if it is downloadable, 0 otherwise*/
int valid_uri(char *uri, int *is_download_file_uri, int *display_order)
{
    if (strlen(uri) < strlen(root_dir))
    {
        strcpy(uri, root_dir);
        return 1;
    }

    if (uri == NULL || !strcmp(uri, ""))
        return 0;

    if (!strcmp(uri, "/"))
        return 1;

    if (uri[strlen(uri) - 1] == '/')
        uri[strlen(uri) - 1] = '\0';

    char content_name[MAXLINE];
    for (int i = 0; i < MAXLINE; i++)
        content_name[i] = '\0';
    int last_slash = -1;
    for (int i = 0; i < strlen(uri); i++)
        if (uri[i] == '/')
            last_slash = i;
    int index = 0;
    for (int i = last_slash + 1; i < strlen(uri); i++)
    {
        content_name[index] = uri[i];
        index++;
    }

    if (!strcmp(content_name, "order?name")) //If the uri corresponds to an ordering function by name return 1
        return *display_order = 1;
    if (!strcmp(content_name, "order?size")) //If the uri corresponds to an ordering function by size return 2
        return *display_order = 2;
    if (!strcmp(content_name, "order?date")) //If the uri corresponds to an ordering function by date return 3
        return *display_order = 3;

    DIR *streamp;
    struct dirent *dep;
    
    if (last_slash != -1)
        uri[last_slash] = '\0';
    strcpy(current_dir, uri);
    if (last_slash != -1)
        uri[last_slash] = '/';

    streamp = opendir(current_dir);                  //We open the directory with the pathname location
    if(streamp==NULL)
        return 0;
    while ((dep = readdir(streamp)) != NULL) //We read each content in the directory and get its metadata
    {
        if (!strcmp(dep->d_name, content_name))
        {
            if (4 != dep->d_type) //If it is not a directory
            {
                *is_download_file_uri = 1;
            }
            return 1;
        }
    }

    closedir(streamp);
    return 0;
}

/*A function for converting a string in an int*/
int string_to_int_parse(char *p)
{
    int ans = 0;
    int pos = 0;
    while (p[pos] != '\0')
    {
        ans = (ans * 10) + p[pos] - '0';
        pos++;
    }

    return ans;
}

/*Merge Sort function*/
void merge_sort(int a, int b, char **names, char **types, char **times, char **readoks, char **sizes, int display_order)
{
    if (a == b) //One element
    {
        return;
    }

    int m = (a + b) / 2;
    merge_sort(a, m, names, types, times, readoks, sizes, display_order);
    merge_sort(m + 1, b, names, types, times, readoks, sizes, display_order);
    merge(a, m, b, names, types, times, readoks, sizes, display_order);
}

/*Merge function*/
void merge(int a, int b, int c, char **names, char **types, char **times, char **readoks, char **sizes, int display_order)
{
    int length_mix = c - a + 1, index = 0;
    char *names_mix[length_mix], *types_mix[length_mix], *times_mix[length_mix], *readoks_mix[length_mix], *sizes_mix[length_mix];
    int pivot1 = a, pivot2 = b + 1;
    while (pivot1 <= b && pivot2 <= c)
    {
        if ((display_order == 1 && compare_strings_alphabetical(names[pivot1], names[pivot2]) <= 0) || (display_order == 2 && (string_to_int_parse(sizes[pivot1]) <= string_to_int_parse(sizes[pivot2])))) //Swapping the contents
        {
            names_mix[index] = *(&names[pivot1]);
            types_mix[index] = *(&types[pivot1]);
            times_mix[index] = *(&times[pivot1]);
            sizes_mix[index] = *(&sizes[pivot1]);
            readoks_mix[index] = *(&readoks[pivot1]);

            pivot1++;
        }
        else
        {
            names_mix[index] = *(&names[pivot2]);
            types_mix[index] = *(&types[pivot2]);
            times_mix[index] = *(&times[pivot2]);
            sizes_mix[index] = *(&sizes[pivot2]);
            readoks_mix[index] = *(&readoks[pivot2]);

            pivot2++;
        }

        index++;
    }

    if (pivot1 <= b)
        for (int i = pivot1; i <= b; i++, pivot1++, index++)
        {
            names_mix[index] = *(&names[pivot1]);
            types_mix[index] = *(&types[pivot1]);
            times_mix[index] = *(&times[pivot1]);
            sizes_mix[index] = *(&sizes[pivot1]);
            readoks_mix[index] = *(&readoks[pivot1]);
        }

    else if (pivot2 <= c)
        for (int i = pivot2; i <= c; i++, pivot2++, index++)
        {
            names_mix[index] = *(&names[pivot2]);
            types_mix[index] = *(&types[pivot2]);
            times_mix[index] = *(&times[pivot2]);
            sizes_mix[index] = *(&sizes[pivot2]);
            readoks_mix[index] = *(&readoks[pivot2]);
        }

    for (int i = a; i <= c; i++)
    {
        names[i] = *(&names_mix[i - a]);
        types[i] = *(&types_mix[i - a]);
        times[i] = *(&times_mix[i - a]);
        sizes[i] = *(&sizes_mix[i - a]);
        readoks[i] = *(&readoks_mix[i - a]);
    }
}

/*Auxiliar function for comparing two strings*/
int compare_strings_alphabetical(char *str1, char *str2)
{
    char s1[strlen(str1)];
    char s2[strlen(str2)];

    strcpy(s1, str1);
    strcpy(s2, str2);

    for (int i = 0; i < strlen(s1); i++)
        s1[i] = (char)tolower(s1[i]);
    for (int i = 0; i < strlen(s2); i++)
        s2[i] = (char)tolower(s2[i]);

    int resp = strcmp(s1, s2);
    return resp;
}

/*Function for updating the current directory pathname of a client called hostname that is being served by a child process from the server*/
void update_current_dir(char *current_dir, char *hostname)
{
    //First we initialize a fd to a .html file corresponding to the hostname response page
    char host_cpy[MAXLINE];
    for (int i = 0; i < MAXLINE; i++)
        host_cpy[i] = '\0';
    host_cpy[0] = '.';
    host_cpy[1] = '/';
    strcat(host_cpy, hostname);
    strcat(host_cpy, ".html");
    int hostname_fd = open(host_cpy, O_RDONLY, 0);
    if (hostname_fd < 0) //If the file does not exist, then do not update the current_dir
    {
        close(hostname_fd);
        return;
    }

    //Otherwise we extract the current_dir from the .html that contains the last page requested by the user
    char c[2]; //An auxiliar char variable
    c[1] = '\0';
    int reading_index = 0;
    int reading_init_size = strlen("<!DOCTYPE html> ") + strlen("<head><title>Directorio ");
    while (reading_index < reading_init_size) //We first read the initial chars that we do not need from the file.
    {
        read(hostname_fd, &c, 1);
        reading_index++;
    }

    char new_current_dir[MAXLINE];
    for (int i = 0; i < MAXLINE; i++)
        new_current_dir[i] = '\0';
    while (1) //Now we get the current dir from the .html file
    {
        read(hostname_fd, &c, 1);
        if (!strcmp(c, "<")) //We ended reading the current dir
            break;
        strcat(new_current_dir, &c); //We add a new char to the new current dir
    }
    for (int i = 0; i < strlen(current_dir) + 1; i++)
        current_dir[i] = '\0';

    strcpy(current_dir, new_current_dir); //Finally, we set the current dir to its actual value
    close(hostname_fd);
    return;
}

/*Function to ignore the whitespace substrings in a uri from a GET method*/
void erase_white_space_substrings(char *uri)
{
    int whitespace_subs = 0;

    if (strlen(uri) >= 3)
        for (int i = 0; i < strlen(uri) - 2; i++)
        {
            if (uri[i] == '%' && uri[i + 1] == '2' && uri[i + 2] == '0')
            {
                uri[i] = uri[i + 1] = uri[i + 2] = ' ';
                whitespace_subs = 1;
            }
        }

    if (!whitespace_subs)
        return;

    char uri_cpy[MAXLINE];
    for(int i = 0; i < MAXLINE; i++)
        uri_cpy[i] = '\0';

    for (int i = 0; i < strlen(uri);)
    {
        if (uri[i] == ' ')
        {
            strcat(uri_cpy, " ");
            i = i + 3;
        }
        else
        {
            char c[2] = {'\0', '\0'};
            c[0] = uri[i];
            strcat(uri_cpy, c);
            i++;
        }
    }

    for (int i = 0; i < strlen(uri); i++)
        uri[i] = '\0';

    strcat(uri, uri_cpy);
    printf("%s %s\n", uri, uri_cpy);
}