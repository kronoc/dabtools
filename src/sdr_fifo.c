#include "sdr_fifo.h"


/* http://en.wikipedia.org/wiki/Circular_buffer */



void cbInit(CircularBuffer *cb, uint32_t size) {
    cb->size  = size;
    cb->start = 0;
    cb->count = 0;
    cb->elems = (uint8_t *)calloc(cb->size, sizeof(uint8_t));
}
void cbFree(CircularBuffer *cb) {
    free(cb->elems);
	}

int cbIsFull(CircularBuffer *cb) {
    return cb->count == cb->size; 
	}
 
int cbIsEmpty(CircularBuffer *cb) {
    return cb->count == 0;
	 }
 
void cbWrite(CircularBuffer *cb, uint8_t *elem) {
    uint32_t end = (cb->start + cb->count) % cb->size;
    cb->elems[end] = *elem;
    if (cb->count == cb->size) {
        cb->start = (cb->start + 1) % cb->size; 
	fprintf(stderr,"fifo overflow!\n");
    }
    else
        ++ cb->count;
}

void cbWriteBytes(CircularBuffer *cb, uint8_t *elems, size_t len) {
	size_t space = cb->size - cb->count;
	int overflow = 0;

	// handle possible overflows
	if(len > cb->size) {
		overflow = 1;
		size_t ignore = len - cb->size;

		elems += ignore;
		len -= ignore;
	}
	if(len > space) {
		overflow = 1;
		size_t remove = len - space;

		cb->start = (cb->start + remove) % cb->size;
		cb->count -= remove;
	}
	if(overflow)
		fprintf(stderr,"fifo overflow!\n");

	uint32_t index_end = (cb->start + cb->count) % cb->size;

	// split task on index rollover
	if(len <= cb->size - index_end) {
		memcpy(cb->elems + index_end, elems, len);
	} else {
		size_t first_bytes = cb->size - index_end;
		memcpy(cb->elems + index_end, elems, first_bytes);
		memcpy(cb->elems, elems + first_bytes, len - first_bytes);
	}

	cb->count += len;
}

void cbRead(CircularBuffer *cb, uint8_t *elem) {
    *elem = cb->elems[cb->start];
    cb->start = (cb->start + 1) % cb->size;
    -- cb->count;
}

void cbReadBytes(CircularBuffer *cb, uint8_t *elems, size_t len) {
	size_t real_bytes = cb->count < len ? cb->count : len;

	// split task on index rollover
	if(real_bytes <= cb->size - cb->start) {
		memcpy(elems, cb->elems + cb->start, real_bytes);
	} else {
		size_t first_bytes = cb->size - cb->start;
		memcpy(elems, cb->elems + cb->start, first_bytes);
		memcpy(elems + first_bytes, cb->elems, real_bytes - first_bytes);
	}

	cb->start = (cb->start + real_bytes) % cb->size;
	cb->count -= real_bytes;
}

int32_t sdr_read_fifo(CircularBuffer * fifo,uint32_t bytes,int32_t shift,uint8_t * buffer)
{
  if (shift>0)
    {
	  cbReadBytes(fifo, buffer, shift);
	  cbReadBytes(fifo, buffer, bytes);
    }
  else {
	  if(bytes + shift > 0)
		  cbReadBytes(fifo, buffer, bytes + shift);
  }
  return 1;
}
