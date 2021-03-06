
#include "logbuffer.h"
#include <stdlib.h>
#include <string.h>

void logbuffer_destroy(struct logbuffer *lb) {
  free(lb->logrecords);
  memset(lb, 0, sizeof(struct logbuffer));
};

void logbuffer_init(struct logbuffer *lb, int size, int bsize, struct timespec duration) {
  lb->logrecords = calloc(sizeof(struct log_record), size);
  lb->read_cursor = 0;
  lb->write_cursor = 0;
  lb->overrun_count = 0;
  lb->block_size = bsize;
  lb->buffer_size = size;
  lb->duration = duration;
};

void logbuffer_write(struct logbuffer *lb, struct log_record *lr) {
  int window = (lb->read_cursor - lb->write_cursor + lb->buffer_size) % lb->buffer_size;
  if (2 == window || 3 == window)
    lb->overrun_count++;
  else
    lb->logrecords[lb->write_cursor++] = *lr;
};

struct log_record *logbuffer_read(struct logbuffer *lb) {
  if (lb->read_cursor == lb->write_cursor)
    return NULL;
  else
    return (lb->logrecords) + lb->read_cursor++;
};
