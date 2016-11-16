#include <sys/time.h>

typedef struct{
  char * filename;
  void * file_contents;
  struct timeval time_of_use;
}cache_file;

typedef struct{
  cache_file * files;
  int size;
}cache_wrap;

cache_wrap my_wrap;

void intialize(int size){
  my_wrap.files = malloc(size * sizeof(cache_file*));
  my_wrap.size=size;
  for( int i=0;i<size;i++){
    my_wrap.files[i].filename=NULL;
  }
}
cache_file get(int i){
  return my_wrap.files[i];
}
//keeping these two things separate hopefully until we get it working. 
int find_lru(){
  struct timeval oldest;
  gettimeofday(&oldest,NULL);
  //above just initializes the comparator time to most present
  int oldest_index;
  for( int i=0;i<my_wrap.size; i++){
    struct timeval a= my_wrap.files[i].time_of_use; 
    struct timeval b= oldest;
    if(timercmp(&a, &b,<)){
      struct timeval oldest= my_wrap.files[i].time_of_use;
      oldest_index=i;
    }
  }
  return oldest_index; 
}

void insert(cache_file cf){

  for( int i=0;i<my_wrap.size; i++){
    if(my_wrap.files[i].filename==NULL){
      my_wrap.files[i]=cf;
      return;
    }
  }
  int lru=find_lru();
  my_wrap.files[lru]=cf;
  return;
}
/*
add_file
*/
