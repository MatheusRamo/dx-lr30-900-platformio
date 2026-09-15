



#include <stdbool.h>
#include <stdint.h>
#include <string.h>
#include <stdio.h>
#include "myqueue.h"







volatile Queue	UartRxQueue = {0};
pQueue pUart1RxQueue = NULL; 	

int queueIsEmpty(pQueue q) 
{
	if (  q->count <= 0 )
	{
        return true;
	}
	else
		return false;
}

bool isFullQueue(pQueue q, unsigned short length)
{
	if( (q->count + length) >= QUEUE_MAX_LENGTH )
	{
        return true;
	}
	if ((q->tail + length)>QUEUE_MAX_LENGTH)
	{
		if ( (q->tail + length)%QUEUE_MAX_LENGTH >= q->front )
		{
			return true;
		}
	}
	
    return false;
}

bool isQueueTail(pQueue q, unsigned short length)
{
	if( (q->tail + length) > QUEUE_MAX_LENGTH  )  //到达队列尾部
    {
        return true;
	}

    return false;
}

void* queueEnqueue(pQueue q, void* Data, unsigned short bytes) 
{	
	unsigned char static len = sizeof(bytes);
	{
		if(isFullQueue(q, bytes+len))
		{		
			return NULL;
		}

		if ( isQueueTail(q, bytes+len) && ((bytes+len) < q->front))
		{
			q->tail = 0;
		}
		
		if(!isQueueTail(q, bytes+len))
		{
			memcpy(&(q->data[q->tail]), (char*)&bytes, len);
			memcpy(&(q->data[q->tail+len]), Data, bytes);
			q->tail = (q->tail+bytes+len) % QUEUE_MAX_LENGTH;
					
			q->count = (q->count + len + bytes) % QUEUE_MAX_LENGTH;
		}
		
	}	
	
	return q;
}

unsigned short queueDequeue(pQueue q, void *pData) 
{
	unsigned short static len = sizeof(unsigned short);
	unsigned short validDataLen = 0;
	static unsigned short count = 0;
	uint16_t Temp = 0xaa;
#if 1
	memcpy(&validDataLen, &(q->data[q->front]), len);



	Temp = q->front;
	while(validDataLen <= 0 || validDataLen > QUEUE_NODE_SIZE)
	{		
		q->front = (q->front+1)%(QUEUE_MAX_LENGTH-3);//2字节表示数据长度+最小1字节数据域=3
		memcpy(&validDataLen, &(q->data[q->front]), len);
		if(count++ > (QUEUE_MAX_LENGTH - Temp))
		{
			q->front = 0;

		}

		if(Temp == q->front)
		{	
			goto exit;
		}
		if( validDataLen > 0 && validDataLen < QUEUE_NODE_SIZE )
		{	
			break;
		}
	}
	{
		count = 0;

		if ( (q->front + len + validDataLen) > QUEUE_MAX_LENGTH)
		{
			q->front = 0;
		}
		else if(queueIsEmpty(q))
		{
			goto exit;
		}
		memcpy(pData, &(q->data[q->front + len]), validDataLen);
		memset(&(q->data[q->front]), 0, len+validDataLen);

		q->count = (q->count - len - validDataLen)%QUEUE_MAX_LENGTH;
		q->front = (q->front + len + validDataLen)%(QUEUE_MAX_LENGTH-3);	
		
	}
	
	if(queueIsEmpty(q))
	{
exit:		

		q->front = 0;
		q->tail = 0;

	}
#endif
	return validDataLen;
}

void EmptyTheQueue(pQueue q)
{
	q->front = 0;
	q->tail = 0;
	q->count = 0;
	memset(q->data, 0, QUEUE_MAX_LENGTH);
}



