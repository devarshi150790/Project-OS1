#include <stdio.h>
#include<stdlib.h>
#include<fcntl.h>
#include<time.h>
#include <stdlib.h>
#include<pthread.h>
#include<sys/socket.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <string.h>
#include<arpa/inet.h>
#include "reqQueue.h"

 struct runArgs{
	int debug;
	int help;
	char *logFile;
	int portNo;
	char *directory;
	int time;
	int threadCount;
	char *schedAlg;
}*runTimeArgs;

pthread_key_t tName;
pthread_mutex_t lockLog;
void daemon_init();
void process_request(struct requestData *req, char *requestString);
void doLogging(struct requestData *req);
void createSchedulerAndSlaves(int n);
void delayScheduling(){
	int timeToDelay;
	if(runTimeArgs->debug==1)
	timeToDelay=0;

	else if(runTimeArgs->time>0)
	timeToDelay=runTimeArgs->time;

	else
	timeToDelay=60;

	sleep(timeToDelay);
}

void socketCreation();

void printHelpScreen(){
	printf("\n===========================================================================================\n");
	printf("-d : Enter debugging mode. That is, do not daemonize, only accept one connection at a\n");
	printf("     time and enable logging to stdout. Without this option, the web server should run\n");
	printf("     as a daemon process in the background.\n");
	printf("-h : Print a usage summary with all options and exit.\n");
	printf("-l file : Log all requests to the given file. See LOGGING for details.\n");
	printf("-p port : Listen on the given port. If not provided, myhttpd will listen on port 8080.\n");
	printf("-r dir : Set the root directory for the http server to dir.\n");
	printf("-t time : Set the queuing time to time seconds. The default should be 60 seconds.\n");
	printf("-n threadnum: Set number of threads waiting ready in the execution thread pool to threadnum.\n");
	printf("              The default should be 4 execution threads.\n");
	printf("-s sched : Set the scheduling policy. It can be either FCFS or SJF. The default will be FCFS.\n");
	printf("==============================================================================================\n");


}

//adding queuing logic


void addRequestToQueue(struct reQueue* rq, struct requestData* req)
{

  pthread_mutex_lock(&rq->lock);

  if(rq->capacity != -1 ){

	  while(rq->queueSize == rq->capacity){
		fflush(stdout);
		pthread_cond_wait(&rq->not_full,&rq->lock);
		fflush(stdout);
	  }
  }

  rq->queueSize++;

  if( rq->head ==NULL && rq->tail ==NULL )
      rq->head = rq->tail = req;
  else
    {
      rq->tail->next = req;
      rq->tail = req;
    }

  pthread_cond_signal(&rq->not_empty);
  pthread_mutex_unlock(&rq->lock);

}


void deleteReqFromQueue(struct reQueue* rq, struct requestData* req) {

	pthread_mutex_lock(&rq->lock);

	struct requestData* iterator = rq->head;

	if (rq->head == req) {
		  rq->head = rq->head->next;
		  if(rq->head==NULL)  rq->tail = NULL;

	} else {
		while (iterator != NULL && iterator->next != req) {
			iterator = iterator->next;
		}
		iterator->next = iterator->next->next;
	}

	rq->queueSize--;

	pthread_cond_signal(&rq->not_full);
	pthread_mutex_unlock(&rq->lock);
}

struct requestData* removeElementFromQueue( struct reQueue* rq )
{

  struct requestData* req = NULL;

  pthread_mutex_lock(&rq->lock);

  while(rq->queueSize == 0){
	  fflush(stdout);
	  pthread_cond_wait(&rq->not_empty,&rq->lock);
	fflush(stdout);
  }

  rq->queueSize--;

  if( rq->head==NULL && rq->tail==NULL )
      return NULL;
  req = rq->head;
  rq->head = req->next;

  if(rq->head==NULL )  rq->tail = NULL;

  pthread_cond_signal(&rq->not_full);
  pthread_mutex_unlock(&rq->lock);

  return req;
}


struct reQueue* newReqQueue(int cap)
{
  struct reQueue* que = malloc( 1 * sizeof(*que));

  if(que==NULL)
    //printf("malloc failed in newReqQueue\n");

  que->head = que->tail = NULL;
  que->queueSize = 0;
  que->capacity = cap;

  pthread_cond_init(&que->not_empty, NULL);
  pthread_cond_init(&que->not_full, NULL);
  pthread_mutex_init(&que->lock, NULL);
  return que;
}

void changeToGivenDirectory(char *directory){
	if(chdir(directory)<0){
	printf("Cannot change to directory\n");
	}
	//else 
	//printf("I am changed\n");

}

char *getGMTTime(void){
	
	time_t tim;
	tim = time(NULL);
	return asctime(gmtime(&tim));
}

char *cwDirectory;
void logging();
FILE *lFPtr;
int main(int argc, char **argv){

	//processing commandline arguments
	const char *optString= "dhl:p:r:t:n:s:";
	int opt=0;
	//int schedFlag=1;
	//initialize structure variables to default values.
	runTimeArgs= malloc(sizeof(struct runArgs));
	runTimeArgs->debug=0;
	runTimeArgs->help=0;
	runTimeArgs->schedAlg="FCFS";
	runTimeArgs->portNo=atoi("8080");

	while((opt=getopt(argc,argv,optString))!=-1){
		switch(opt){
		case 'd':
			runTimeArgs->debug=1;
			break;
		case 'h':
			runTimeArgs->help=1;
			printHelpScreen();
			exit(0);
			break;
		case 'l':
			runTimeArgs->logFile=optarg;
			break;

		case 'p':
			runTimeArgs->portNo=atoi(optarg);
			
			break;
		case 'r':
			runTimeArgs->directory=optarg;
			break;
		case 'n':
			runTimeArgs->threadCount=atoi(optarg);
			break;
		case 't':
			runTimeArgs->time=atoi(optarg);
			break;

		case 's':
			runTimeArgs->schedAlg=optarg;
			break;
		
		case '?':
			 if (isprint (optopt))
			fprintf(stderr, "Unknown option `-%c'.\n", optopt);
			else
			fprintf(stderr, "Unknown option character `\\x%x'.\n", optopt);
			abort();
		default:
			exit(1);
			//abort();
}
	}
	if(runTimeArgs->directory!=NULL)
	changeToGivenDirectory(runTimeArgs->directory);
	cwDirectory= getcwd(NULL,0);

if(runTimeArgs->debug!=1)
	daemon_init();			
	
	socketCreation(runTimeArgs);

	return 0;
}

struct reQueue *requeue;
struct reQueue *schedQueue;

void logging(){
if(runTimeArgs->debug==0){
int pathLength = strlen(cwDirectory) + strlen(runTimeArgs->logFile) + 1;
char *lFilePath = (char *) malloc(pathLength * sizeof(char));
strcpy(lFilePath, cwDirectory);
strcat(lFilePath,"/");
strcat(lFilePath, runTimeArgs->logFile);
//printf("%s",lFilePath);
lFPtr=fopen(lFilePath,"a+");
//printf("%s",lFPtr);
}
else{
lFPtr=stdout;
}
pthread_mutex_init(&lockLog, NULL);
}

void *accept_request(void *sockid){

struct sockaddr_in client_addr;
unsigned clientLength;
int cSockfd;
int nThreads;
char *clientip;
FILE *fin;
FILE *fout;
//creating queue if debug!=1
if(runTimeArgs->debug!=1)
requeue= newReqQueue(-1);
else
requeue=newReqQueue(1);

schedQueue=newReqQueue(1);
pthread_key_create(&tName, NULL);
time_t t;
t = time(NULL);
char requestString[1024];
int sockfd = *((int*) sockid);
listen(sockfd, 5);

if(runTimeArgs->debug==1)
	nThreads=1;
	else if(runTimeArgs->threadCount>0)
	nThreads=runTimeArgs->threadCount;
	else 
	nThreads=4;

	createSchedulerAndSlaves(nThreads);

while(1){
clientLength = sizeof(client_addr);
cSockfd = accept(sockfd, (struct sockaddr *) &client_addr, &clientLength);


if(cSockfd < 0){
exit(0);
}
struct requestData *req=malloc(1* sizeof(*req));
req->next=NULL;
req->socketID=cSockfd;
clientip=inet_ntoa(client_addr.sin_addr);
req->clientIP=strdup(clientip);

read(cSockfd, requestString, sizeof(requestString));	// read request from socket
//printf("\n%s\n",requestString);
process_request(req,requestString);
req->queuedTime=strdup(asctime(gmtime(&t)));//gmtime(&t);//storing request queuing time in GMT
addRequestToQueue(requeue, req);

}
}
void *scheduleRequest(void *arg){

	delayScheduling();
	time_t tim;
	tim=time(NULL);
	struct requestData *iterator;
	struct requestData *req = NULL;
	int *id = (int *)arg;

	pthread_setspecific(tName,(void *)id);

	int minsize;

	while (1) {

		pthread_mutex_lock(&requeue->lock);

		while(requeue->head == NULL){
					fflush(stdout);
					pthread_cond_wait(&requeue -> not_empty,&requeue->lock);
					fflush(stdout);
		}

		pthread_mutex_lock(&schedQueue->lock);

		while (schedQueue->queueSize == schedQueue->capacity) {
			fflush(stdout);
			pthread_cond_wait(&schedQueue->not_full, &schedQueue->lock);
			fflush(stdout);
		}

		pthread_mutex_unlock(&schedQueue->lock);

		iterator = requeue->head;
		minsize = -1;
		req = iterator;

		if (runTimeArgs->schedAlg != NULL && strcmp(runTimeArgs->schedAlg, "sjf") == 0) {
			

			while (iterator != NULL) {

				if (minsize == -1) {
					minsize = iterator->fileSize;
				}
				if (minsize > iterator->fileSize) {
					minsize = iterator->fileSize;
					req = iterator;
				}
				iterator = iterator->next;

			}
		}

		pthread_mutex_unlock(&requeue->lock);

		if (req != NULL) {
			deleteReqFromQueue(requeue, req);
			req->scheduledTime = strdup(asctime(gmtime(&tim)));//strdup(get_current_time_gmt());
			addRequestToQueue(schedQueue, req);
		}

		fflush(stdout);
	}

}
void *processRequest(void *thrID){
	int *id = (int *) thrID;
	pthread_setspecific(tName, (void *) id);

	int cSockfd;
/*	char *cWDirectory= getcwd(NULL,0);//current working directory
	char *fpath = (char *) malloc(len * sizeof(char));
	strcpy(fpath, cWDirectory);
	strcat(fpath, );*/
	int e;
	char line[1024];
	FILE *fpntr;

	while (1) {
		struct requestData *req = removeElementFromQueue(schedQueue);
		cSockfd= req->socketID;
		int rStatus = req->reqStatus;
		if (rStatus == 404){
				write(cSockfd,"HTTP/1.0 404 Not Found\r\n",25);
				write(cSockfd,"Content-Type: text/html\r\n",25);
				write(cSockfd,"\n\n",3);				
				write(cSockfd,"Resource Not Found 404",22);
				close(cSockfd);
				
		}
		else if(rStatus==200){
		if(strcmp(req->methodType,"GET")==0){
		if(strcmp(req->fileType,"directory")!=0){

		//printf("%s\n",req->absPath);
		fpntr=fopen(req->absPath,"r");
		char *contentType="text/plain";
		char *cTime;
		if(strcmp(req->fileType,"html")==0)
		contentType="text/html";
		else if(strcmp(req->fileType,"jpeg")==0)
		contentType="image/jpeg";
		else if(strcmp(req->fileType,"gif")==0)
		contentType="image/gif";
		fseek(fpntr, 0, SEEK_END); // seek to end of file
		char si[20];
		int size = ftell(fpntr);
		rewind(fpntr);
		sprintf(si,"%d",size);	
		write(cSockfd, "HTTP/1.0 200 OK\n",16);
		write(cSockfd,"Connection: close\n",19);
		write(cSockfd, "Date: ",7);
		cTime=getGMTTime();
		write(cSockfd, cTime,strlen(cTime));
		write(cSockfd, "Server: ",9);
		write(cSockfd,"myhttpd\n",8);
		write(cSockfd,"Last-Modified: ",14);
		write(cSockfd,req->lastModified,strlen(req->lastModified));
		write(cSockfd,"Content-Length: ",17);
		write(cSockfd,si,strlen(si));
		write(cSockfd, "\nContent-Type: ",16);
		write(cSockfd, contentType,strlen(contentType));
		write(cSockfd, "\n\n",3);

		while (fgets(line,1024,fpntr)!=NULL)
		write(cSockfd,line,strlen(line));
		close(cSockfd);
		}
		else if(strcmp(req->fileType,"directory")==0){

		int pathLength = strlen(req->absPath) + strlen("index.html") + 1;
		char *indexFPath = (char *) malloc(pathLength * sizeof(char));
		strcpy(indexFPath,req->absPath);
		strcat(indexFPath,"index.html");
		FILE *fp= fopen(indexFPath,"r");

		//struct stat fS= fstat(fp,&fS);
		if(fp==NULL){

		chdir(req->absPath);
		FILE *fpntr= popen("ls -lrt","r");
		chdir(cwDirectory);
				
		//fseek(fpntr, 0, SEEK_END); // seek to end of file
		int size=0 ;//= ftell(fpntr);

		//rewind(fpntr);
		
		while (fgets(line,1024,fpntr)){
		size+=strlen(line);
		write(cSockfd,line,strlen(line));
		}
		req->fileSize=size;
		close(cSockfd);

		}
		else{
		fseek(fp, 0, SEEK_END); // seek to end of file
		char si[20];
		int size = ftell(fp);
//		printf("size=%d\n",size);
		rewind(fp);
		sprintf(si,"%d",size);	
		req->fileSize=size;
		char *contentType="text/html";
		char *cTime;
		write(cSockfd, "HTTP/1.0 200 OK\n",16);
		write(cSockfd,"Connection: close\n",19);
		write(cSockfd, "Date: ",7);
		cTime=getGMTTime();
		write(cSockfd, cTime,strlen(cTime));
		write(cSockfd, "Server: ",9);
		write(cSockfd,"myhttpd\n",8);
		write(cSockfd,"Last-Modified: ",15);
		write(cSockfd,req->lastModified,strlen(req->lastModified));
//		fseek(fp, 0, SEEK_END); // seek to end of file
//		char si[20];
//		int size = ftell(fp);
//		rewind(fp);
//		sprintf(si,"%d",size);	
		write(cSockfd,"Content-Length: ",17);
		write(cSockfd,si,strlen(si));
		write(cSockfd, "\nContent-type: ",16);
		write(cSockfd, contentType,strlen(contentType));
		write(cSockfd, "\n\n",3);
		while (fgets(line,1024,fp))
		write(cSockfd,line,strlen(line));
		close(cSockfd);
	}
		}
		}
	}
		if(strcmp(req->methodType,"HEAD")==0){
		
		char *contentType="text/plain";
		char *cTime;
		if(strcmp(req->fileType,"html")==0)
		contentType="text/html";
		else if(strcmp(req->fileType,"jpeg")==0)
		contentType="image/jpeg";
		else if(strcmp(req->fileType,"gif")==0)
		contentType="image/gif";

		write(cSockfd, "HTTP/1.0 200 OK\n",16);
		write(cSockfd, "Date: ",7);
		cTime=getGMTTime();
		write(cSockfd, cTime,strlen(cTime));
		write(cSockfd, "Server: ",8);
		write(cSockfd,"myhttpd",7);
		write(cSockfd,"\n",2);
		write(cSockfd,"Last-Modified:",14);
		write(cSockfd,req->lastModified,strlen(req->lastModified));
		write(cSockfd, "Content-type:",13);
		write(cSockfd, contentType,strlen(contentType));
		write(cSockfd, "\n\n",3);
		close(cSockfd);

		}

		doLogging(req);

	}

}
void doLogging(struct requestData *req){
pthread_mutex_lock(&lockLog);
if(lFPtr!=NULL){

char re[5];
char fsize[15];
//pthread_mutex_lock(&lockLog);

fputs(req->clientIP,lFPtr);//puts client IP
char *qTime=strtok(req->queuedTime,"\n");
char *schdTime=strtok(req->scheduledTime,"\n");
char *firsL=strtok(req->firstLine,"\n");
sprintf(re,"%d",req->reqStatus);
char *reStatus=re;
sprintf(fsize,"%ld",req->fileSize);
char *fsiz= fsize;
fputs(" - ",lFPtr);
fputs(" [",lFPtr);
fputs(qTime,lFPtr);
fputs("]",lFPtr);
fputs(" [",lFPtr);
fputs(schdTime,lFPtr);
fputs("]\n",lFPtr);
fputs(firsL,lFPtr);
fputs("\n",lFPtr);
fputs(re,lFPtr);
fputs(" ",lFPtr);
fputs(fsiz,lFPtr);
fputs(" \n",lFPtr);
fflush(lFPtr);
}
pthread_mutex_unlock(&lockLog);

}
void createSchedulerAndSlaves(int n) {
	pthread_t schedulingThread;

	pthread_t *slaveThreads = malloc(n * sizeof(pthread_t));

	int schID = -1;
	int *thrIden = malloc(n * sizeof(int));

	int i,j;
	for (i = 0; i < n; i++)
		*(thrIden + i) = i;

	if(pthread_create(&schedulingThread, NULL, scheduleRequest, &schID)!=0){
	printf("Scheduler Thread creation failed\n");
	}


	for (j = 0; j < n; j++) {

		 if(pthread_create(slaveThreads + j, NULL, processRequest,thrIden + j)!=0){
		printf("Slave Thread creation failed\n");	
		}

	}
}



void socketCreation(struct runArgs *runTimeArgs){

	if(runTimeArgs->debug==1 || runTimeArgs->logFile!=NULL)
	logging();
	pthread_t queuingThread;
	struct requestData *req= malloc( sizeof(struct requestData));
	//Declaring process variables.
	int server_sockfd, client_sockfd;
	unsigned server_len, client_len;
	struct sockaddr_in server_address;
	struct sockaddr_in client_address;
	//Remove any old socket and create an unnamed socket for the server.
	server_sockfd = socket(AF_INET, SOCK_STREAM, 0);
	server_address.sin_family = AF_INET;
	server_address.sin_addr.s_addr = htons(INADDR_ANY);
	server_address.sin_port=htons(runTimeArgs->portNo);
	server_len = sizeof(server_address);
	client_len=sizeof(client_address);

	bind(server_sockfd, (struct sockaddr *) &server_address, server_len);

	pthread_create (&queuingThread, NULL, accept_request, (void*)&server_sockfd);
	pthread_join(queuingThread,NULL);
}

void process_request(struct requestData *req,char *requestString){//,int sockFD){

int f_d = 0;
struct stat st;
char *reqType;
char *filePath;
char *firstLine;
long fileSize;
char *fLine;
char *findUser;
firstLine=strtok(requestString,"\n");
fLine=strdup(firstLine);
reqType=strtok(fLine," ");
req->firstLine=firstLine;
req->methodType=reqType;
filePath=strtok(NULL," ");
findUser=strchr(filePath,'~');
if(findUser!=NULL){
findUser=findUser+1;
char * user= strtok(strdup(findUser),"/");
char *dPath= strdup(findUser);
dPath= dPath+strlen(user)+1;
int l= strlen("/home/") + strlen(user)+strlen("/myhttpd/")+strlen(dPath)+1;//+strlen(rFPath)+1;
char *nFilePath= (char *) malloc(l * sizeof(char));
strcpy(nFilePath,"/home/");//make it /home/
strcat(nFilePath,user);
strcat(nFilePath,"/myhttpd/");
//printf("%s\n",nFilePath);
chdir(nFilePath);
strcat(nFilePath,dPath);
filePath=dPath;
req->absPath=nFilePath;


}
else{
//req->firstLine=firstLine;

//req->methodType=reqType;
//printf("I am here\n");
int lenPath= strlen(cwDirectory)+strlen(filePath)+1;
char *entireFilePath=(char *) malloc(lenPath * sizeof(char));
strcpy(entireFilePath,cwDirectory);
strcat(entireFilePath,filePath);
req->absPath=entireFilePath;
filePath=filePath+1;
//printf("filePath=%s\n",filePath);
}

//filePath=filePath+1;//to avoid starting /
//gathering file information
//printf("%s\n",filePath);
//printf("filepath= %s\n",filePath);


f_d = open(filePath,O_RDONLY,0);
//printf("%s\n",cwDirectory);
chdir(cwDirectory);
if(f_d>=0){

req->reqStatus=200;
if (fstat(f_d, &st) < 0) {
perror("fstat failed");
exit(1);
}

time_t t = st.st_mtim.tv_sec;

req->lastModified = strdup(asctime(gmtime(&t)));



		if (S_ISDIR(st.st_mode)){
			req->fileType = "directory";
			req->fileSize = 0;
			if(close(f_d) < 0)
				perror("close failed");
		}
		else {
			
			char *fExtension = strrchr(filePath, '.');
			
				if (!fExtension || fExtension == filePath)
				fExtension="";
				else
				fExtension=fExtension+1;

			req->fileType = strdup(fExtension);
			
			if(strcmp("GET",req->methodType)==0){
			
			req->fileSize=st.st_size;
			
		}
			else{
				req->fileSize = 0;
				if(close(f_d) < 0)
					perror("close failed");
			}
		}

	}

else{
//printf("I am in 404\n");
req->reqStatus=404;
req->lastModified = "";
req->fileType="";
req->fileSize=0;
}
}
void daemon_init(){
	pthread_t  qThread;
	int processID;
	int retValue;
	char *message="Thread1";
	int sessionID=0;
	processID=fork();
	if(processID<0){
		printf("Fork Failed!\n");
		exit(1);
	}
	if(processID!=0) {

		exit(0);
	}

	else if(processID==0){

		//setup new session
		sessionID=setsid();
		umask(0);
		


	}


}

