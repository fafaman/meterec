int writer_thread(void *d);
int reader_thread(void *d);
float read_disk_buffer_level(struct meterec_s *meterec);
unsigned int set_thread_delay(jack_client_t *client);
