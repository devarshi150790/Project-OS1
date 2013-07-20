#ifndef __reqQueue_H
#define __reqQueue_H



struct reQueue
{
  struct requestData* head;
  struct requestData* tail;
  pthread_mutex_t lock;
  pthread_cond_t not_empty;
  pthread_cond_t not_full;
  int queueSize;
  int capacity;


};

//datastructure for storing data about request
struct requestData{
long fileSize;
char *fileType;
char *methodType;
char *clientIP;
int reqStatus;
int socketID;
char *lastModified;
//FILE *outFile;
int inFile;
char *scheduledTime;
char *queuedTime;
char *firstLine;
long responseSize;
char *absPath;
struct requestData *next;

};



/*struct reQueue* newReqQueue(int);
void addRequestToQueue(struct reQueue* rq, struct requestData* req);
struct requestData* removeRequestFQ( struct reQueue* rq );
//struct requestData* newRequest(void);
void deleteRequestFQ(struct reQueue* rq, struct requestData* req);*/



#endif
