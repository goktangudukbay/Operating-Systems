


int sbmem_init(int segmentsize); 
int sbmem_remove(); 

int sbmem_open(); 
void *sbmem_alloc (int size);
void sbmem_free(void *p);
int sbmem_close(); 

/*
void print_state();
int init_experiment();
int open_experiment();
int close_experiment();
int get_free_space();
*/
