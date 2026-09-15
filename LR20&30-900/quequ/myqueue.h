#ifndef __QUEUE_H__
#define	__QUEUE_H__
 
#include <stdbool.h>
#include <stdint.h>
#include <string.h>
#include <stdio.h>





//队列长度
#define	QUEUE_MAX_LENGTH 2000 

//设置自动分包长度
#define	QUEUE_NODE_SIZE	 255

typedef struct queueNode {
	//data[QUEUE_MAX_LENGTH]结构：2Byte长度+数据域
	uint8_t	 data[QUEUE_MAX_LENGTH]; 
	uint16_t count;
	uint16_t front;
	uint16_t tail;
}*pQueue, Queue;

extern volatile Queue	UartRxQueue;
extern unsigned short queueDequeue(pQueue q, void *pData);
extern void EmptyTheQueue(pQueue q);
extern pQueue pUart1RxQueue;
extern void* queueEnqueue(pQueue q, void* Data, unsigned short bytes);
extern int queueIsEmpty(pQueue q);
extern void queueDestroy(pQueue q);
#endif	// __QUEUE_H__


