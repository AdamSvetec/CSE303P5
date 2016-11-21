#include <sys/time.h>

typedef struct{
  char * filename;
  void * file_contents;
  struct timeval time_of_use;
}cache_file;

void delete_cache_file(cache_file* file){
  if(file == NULL){
    return;
  }
  if(file->filename != NULL){
    free(file->filename);
  }
  if(file->file_contents != NULL){
    free(file->file_contents);
    }
  //TODO FIX THIS, Why isn't index changing properly?
  free(file);
  file = NULL;
}

cache_file * create_cache_file(char * filename, void * file_contents, struct timeval time_of_use){
  cache_file* file = malloc(sizeof(cache_file));
  file->filename = filename;
  file->file_contents = file_contents;
  file->time_of_use = time_of_use;
  return file;
}

cache_file * create_from_disk_file(char * filename){
  FILE * file;
  file=fopen(filename, "r");
  if(file==NULL){
    fprintf(stderr, "Could not open file\n");
    return NULL;
  }
  int file_no = fileno(file);
  fseek(file, 0, SEEK_END);
  int file_size = ftell(file);
  fseek(file, 0, SEEK_SET);

  void * file_contents = malloc(file_size);
  bzero(file_contents,file_size);
  int nread = read(file_no, file_contents, file_size);
  if(nread < 0){
    fprintf(stderr, "Error on read\n");
    fclose(file);
    return NULL;
  }
  fclose(file);
  struct timeval curr;
  gettimeofday(&curr,NULL);
  write(STDOUT_FILENO, file_contents, file_size);
  char * filename_copy = malloc(strlen(filename)+1);
  strcpy(filename_copy, filename);
  return create_cache_file(filename_copy, file_contents, curr); 
}

typedef struct{
  cache_file ** files;
  int size;
}cache_wrap;

cache_wrap my_wrap;

void intialize(int size){
  my_wrap.files = malloc(size * sizeof(cache_file*));
  my_wrap.size=size;
  for(int i=0;i<size;i++){
    my_wrap.files[i] = NULL;
  }
}

cache_file * get(char * filename){
  for(int i = 0; i < my_wrap.size; i++){
    if(my_wrap.files[i] != NULL){
      if(strcmp(my_wrap.files[i]->filename, filename)==0){
	return my_wrap.files[i];
      }
    }
  }
  return NULL;
}

//keeping these two things separate hopefully until we get it working. 
int find_lru(){
  struct timeval oldest;
  gettimeofday(&oldest,NULL);
  //above just initializes the comparator time to most present
  int oldest_index = -1;
  for( int i=0; i<my_wrap.size; i++){
    if(my_wrap.files[i] != NULL){
      struct timeval a= my_wrap.files[i]->time_of_use;
      if(timercmp(&a, &oldest, <)){
	oldest= my_wrap.files[i]->time_of_use;
	oldest_index=i;
      }
    }
  }
  return oldest_index; 
}

void insert(cache_file *cf){
  //TODO: add
  /*for(int i=0;i<my_wrap.size; i++){
    if(my_wrap.files[i] != NULL && strcmp(cf->filename, my_wrap.files[i]->filename) == 0){
      delete_cache_file(my_wrap.files[i]);
      my_wrap.files[i] = cf;
      printf("It worked replacing index: %d\n", i);
      return;
    }
    }*/

  for(int i=0;i<my_wrap.size; i++){
    if(my_wrap.files[i] == NULL){
      my_wrap.files[i] = cf;
        printf("It worked and lru is index: %d\n",i);
      return;
    }
  }

  int lru=find_lru();
  delete_cache_file(my_wrap.files[lru]);
  my_wrap.files[lru] = cf;
  printf("It worked and lru is index: %d\n",lru);
}
