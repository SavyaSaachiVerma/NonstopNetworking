/**
 * Networking
 * CS 241 - Fall 2017
 */
#include "common.h"
#include "format.h"
#include <ctype.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <netdb.h>
#include <sys/stat.h>
#include <stdint.h>
#include <errno.h>
#include <sys/socket.h>
#include <fcntl.h>

char **parse_args(int argc, char **argv);
verb check_args(char **args);
char* request(char** args);

char* requestList(void );
char* requestPut(char* remote, char* local);
char* requestGet(char* remote);
char* requestDelete(char* remote);
ssize_t write_all_to_socket(int socket, const char *buffer, size_t count);
ssize_t read_all_from_socket(int socket, char *buffer, size_t count); 

void parsingResponse(int serverSocket, char* local);

int requestSent=0;
int flag= 0;
FILE* localF;
size_t bytesremaining=0;
size_t reqSize=0;

int main(int argc, char **argv) {
    // Good luck!
	char** args=parse_args(argc, argv);
	check_args(args); 	

	char* host=args[0];
	char* port=args[1];
//	char* command=args[2];
	char* remote=args[3];
	char* local=args[4];

	char* req=request(args);	

	struct addrinfo hints, *result;

	memset(&hints, 0, sizeof(struct addrinfo));

	hints.ai_family = AF_INET;
	hints.ai_socktype = SOCK_STREAM;

	int s = getaddrinfo(host, port, &hints, &result);
	if(s != 0) {
		fprintf(stderr, "Getaddrinfo : %s \n", gai_strerror(s));
		exit(1);
	}

	int serverSocket = socket(result->ai_family, result->ai_socktype, 0);
	if(serverSocket == -1) {
		perror("Socket error");
		exit(1);
	}

	int connect_ok = connect(serverSocket, result->ai_addr, result->ai_addrlen);
	if(connect_ok != 0) {
		perror("Connect error");
		exit(1);
	}
	
	if(requestSent == 4 ){										//REQUEST = PUT
		write_all_to_socket(serverSocket, req, reqSize);

		while(bytesremaining > 0){
			char* req=requestPut(remote, local);
			write_all_to_socket(serverSocket, req, reqSize);
			//printf("%zu\n",reqSize);
                        free(req);
			bytesremaining-=reqSize;
		}

		shutdown(serverSocket,SHUT_WR);
		fclose(localF);
	}
	else{
		write_all_to_socket(serverSocket, req, reqSize);
		shutdown(serverSocket,SHUT_WR);
		free(req);
	}
	parsingResponse(serverSocket,args[4]);	
	
//	free(req);
	free(args);
	freeaddrinfo(result);
//	char buf[1024];
//	read_all_from_socket(serverSocket, buf, 1024);
//	printf("%s\n",buf); 
}

void parsingResponse(int serverSocket, char* local){
	char buf[1];
	read_all_from_socket(serverSocket, buf, 1);

	char* response=malloc(10);
//	strcpy(response,"");
	*response=buf[0];
//	*(response+1)='\0';
	size_t size=0;
	char* binData;

	if(*buf=='O'){
	 	read_all_from_socket(serverSocket, buf, 1);
		*(response+1)=buf[0];
		*(response+2)='\0';
		read_all_from_socket(serverSocket, buf, 1); 					//for \n
		//strcat(respons,buf);

		if( requestSent==1 || requestSent==2){						//request was GET or LIST
			char buf2[8];
			read_all_from_socket(serverSocket, buf2, 8);				//next 8 bytes size in case of GET and LIST
		
			size=*((size_t*)buf2);
			binData=malloc(size+10);	
//			printf("%zu\n",size);
//			size=20;
			ssize_t bytesread1=read_all_from_socket(serverSocket, binData, size+10);
				if(bytesread1 < (ssize_t)size){
					print_too_little_data();
					free(binData);
					free(response);				
					exit(1);			
				}			
			
//			char buffy[10];
//                        ssize_t bytesread2=read_all_from_socket(serverSocket, buffy, 10);
                        	if(bytesread1 >(ssize_t)size){
                                	print_received_too_much_data();
                                	free(binData);
                                	free(response);
                                	exit(1);
                        	}				
	
			if(requestSent==1){							//LIST
				write( 1, binData, size);		
							
			}
			else{				
				int fildes = open(local ,O_CREAT | O_TRUNC | O_RDWR, S_IRWXU | S_IRWXG | S_IRWXO);
				write(fildes, binData, size);
				//printf("%s\n",response);
				close(fildes);	
			}
		
			free(binData);	
		}
		else{
			//	printf("%s\n",response);			
			print_success();
		}
	
	}	
	else if(*buf == 'E'){
		char* errmsg=malloc(1024); 
		char* errmsg1=errmsg;
		for(int i=0;i<5;i++){
			read_all_from_socket(serverSocket, buf, 1);			
		}
				
/*		strcpy(errmsg,"");
		while(1){									//reading response
			read_all_from_socket(serverSocket, buf, 1);
			
			if(*buf != '\n')
				strcat(response,buf);
			else 
				break;
		}
*/		
		while(1){									//reading err msg
			read_all_from_socket(serverSocket, buf, 1);
			
			if(*buf != '\n'){
				*errmsg=buf[0];
				++errmsg;
				//strcat(errmsg,buf);
			}
			else 
				break;
		}
		
		*errmsg='\0';
		print_error_message(errmsg1);
	}
	else{
		print_invalid_response();
	
	}

//	free(binData);	
	free(response); //
}


char* request(char** args){
	
	char* command=args[2];
	char* request=NULL;

    if (strcmp(command, "LIST") == 0) {
        request=requestList();
	requestSent=1;
    }

    if (strcmp(command, "GET") == 0) {
        if (args[3] != NULL && args[4] != NULL) {
	    char* remote=args[3];
            request=requestGet( remote);
	    requestSent=2;	
        }
    }

    if (strcmp(command, "DELETE") == 0) {
        if (args[3] != NULL) {
	    char* remote=args[3];
            request=requestDelete( remote);
	    requestSent=3;
        }
    }

    if (strcmp(command, "PUT") == 0) {
        if (args[3] == NULL || args[4] == NULL) {
            print_client_help();
            exit(1);
        }
	    char* remote=args[3];		//
	    char* local=args[4];		//
            request=requestPut(remote, local);
	    requestSent=4;
    }

	return request;
}


char*  requestList(){

	char* request=malloc(1024);
	strcpy(request,"LIST\n");
	reqSize=strlen(request) +1;

	return request;
}

char* requestGet( char* remote){

	char* request=malloc(1024);
	strcpy(request,"GET ");
	strcat(request,remote);
	strcat(request,"\n");
	
	reqSize=strlen(request) +1;

	return request;
}

char*  requestDelete( char* remote){

	char* request=malloc(1024);
	strcpy(request,"DELETE ");
	strcat(request,remote);
	strcat(request,"\n");

	reqSize= strlen(request) +1;

	return request;
}

char* requestPut(char* remote, char* local){
	
	if( access( local, F_OK ) != -1 ) {
    		
		char* request=malloc(1024);
		struct stat fileInfo;//=malloc(sizeof(stat));;
                size_t sizeFile;

		if(flag == 0){	
			flag =1;
		//	struct stat fileInfo;//=malloc(sizeof(stat));;
		//	size_t sizeFile;

			strcpy(request,"PUT ");                                                                         // file exists
                	strcat(request, remote);
                	strcat(request,"\n");

			int size=strlen(request) ;

			if( stat(local, &fileInfo) == -1){
				perror("stat: ");
				exit(1);
			}
			else{
				sizeFile=(size_t)(fileInfo.st_size);
				//printf("%zu\n",sizeFile);
			}
	
			char* trav=request;

			while(*trav){
				++trav;
			}
		
			memcpy(trav,(char*)&sizeFile, 8);
 
			reqSize=size+8;
			bytesremaining= sizeFile;

			
			localF=fopen(local,"r");
			return request;
			
		}
		else{	
//			FILE* localFile=fopen(local,"r");
			int fd= fileno(localF);
			if( stat(local, &fileInfo) == -1){
                                perror("stat: ");
                                exit(1);
                        }
                        else{
                                sizeFile=(size_t)(fileInfo.st_size);
                        }

		//	char* req=realloc(request, 1024 + sizeFile);
		//	trav= req + size + 8;
				

		//	char* buffer=NULL;
		//	size_t siz=0;	
		//	ssize_t chars;
		
		//	while( (chars = getline(&buffer, &siz, localFile )) != -1 ){
		//		memcpy(trav,buffer,chars);
		//		trav+=chars;
		//	}

		
		//	free(buffer);
		//	buffer=NULL;
			ssize_t bytesread=read(fd, request, 1024);
			reqSize= bytesread;				
			return(request);
		}
	} 
	else{
		exit(1);
		char* tofree=malloc(1);
		return tofree;	
	}
	
}



/**
 * Given commandline argc and argv, parses argv.
 *
 * argc argc from main()
 * argv argv from main()
 *
 * Returns char* array in form of {host, port, method, remote, local, NULL}
 * where `method` is ALL CAPS
 */
char **parse_args(int argc, char **argv) {
    if (argc < 3) {
        return NULL;
    }

    char *host = strtok(argv[1], ":");
    char *port = strtok(NULL, ":");
    if (port == NULL) {
        return NULL;
    }

    char **args = calloc(1, 6 * sizeof(char *));
    args[0] = host;
    args[1] = port;
    args[2] = argv[2];
    char *temp = args[2];
    while (*temp) {
        *temp = toupper((unsigned char)*temp);
        temp++;
    }
    if (argc > 3) {
        args[3] = argv[3];
    }
    if (argc > 4) {
        args[4] = argv[4];
    }

    return args;
}

/**
 * Validates args to program.  If `args` are not valid, help information for the
 * program is printed.
 *
 * args     arguments to parse
 *
 * Returns a verb which corresponds to the request method
 */
verb check_args(char **args) {
    if (args == NULL) {
        print_client_usage();
        exit(1);
    }

    char *command = args[2];

    if (strcmp(command, "LIST") == 0) {
        return LIST;
    }

    if (strcmp(command, "GET") == 0) {
        if (args[3] != NULL && args[4] != NULL) {
            return GET;
        }
        print_client_help();
        exit(1);
    }

    if (strcmp(command, "DELETE") == 0) {
        if (args[3] != NULL) {
            return DELETE;
        }
        print_client_help();
        exit(1);
    }

    if (strcmp(command, "PUT") == 0) {
        if (args[3] == NULL || args[4] == NULL) {
            print_client_help();
            exit(1);
        }
        return PUT;
    }

    // Not a valid Method
    print_client_help();
    exit(1);
}

ssize_t read_all_from_socket(int socket, char *buffer, size_t count) {
		    
	ssize_t bytesread;
	ssize_t totbytesread=0;	
	errno=0;

	while(count){
		bytesread=read(socket, buffer, count);
		//totbytesread+=bytesread;

		if(bytesread == 0)
			return totbytesread;

		if(bytesread == -1 && errno != EINTR){
			printf("%s\n",strerror(errno));
			return -1;
		}
		if( bytesread != -1){
			count -=bytesread;
			buffer += bytesread;
			totbytesread += bytesread;
		}
		else{
			errno=0;
		}
		
	}

    return totbytesread;
}

ssize_t write_all_to_socket(int socket, const char *buffer, size_t count) {
    ssize_t byteswritten;
    ssize_t totbyteswritten=0;
    errno=0;

	while(count){
		byteswritten=write(socket, buffer, count);
		//totbyteswritten+=byteswritten;		
	
		if(byteswritten == 0)
			return totbyteswritten;		

		if(byteswritten == -1 && errno !=EINTR)
			return -1;
		
		if(byteswritten != -1){
			count -= byteswritten;
			buffer +=byteswritten;
			totbyteswritten+=byteswritten;
		}
		else{
			errno=0;
		}

	}

	
    return byteswritten; 
}